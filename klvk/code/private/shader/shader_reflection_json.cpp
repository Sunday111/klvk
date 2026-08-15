#include "shader_reflection_json.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

#include <array>
#include <nlohmann/json.hpp>
#include <ranges>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{
namespace
{

std::string BlobText(slang::IBlob* blob)
{
    if (blob == nullptr || blob->getBufferSize() == 0) return {};
    return {static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize()};
}

vk::ShaderStageFlagBits ParseStage(std::string_view stage)
{
    if (stage == "vertex") return vk::ShaderStageFlagBits::eVertex;
    if (stage == "fragment") return vk::ShaderStageFlagBits::eFragment;
    if (stage == "geometry") return vk::ShaderStageFlagBits::eGeometry;
    if (stage == "hull") return vk::ShaderStageFlagBits::eTessellationControl;
    if (stage == "domain") return vk::ShaderStageFlagBits::eTessellationEvaluation;
    if (stage == "compute") return vk::ShaderStageFlagBits::eCompute;
    ErrorHandling::ThrowWithMessage("Unsupported reflected Slang stage '{}'", stage);
    return {};
}

ShaderScalarType ParseScalarType(std::string_view type)
{
    if (type == "bool") return ShaderScalarType::Bool;
    if (type == "int32") return ShaderScalarType::Int32;
    if (type == "uint32") return ShaderScalarType::UInt32;
    if (type == "float16") return ShaderScalarType::Float16;
    if (type == "float32") return ShaderScalarType::Float32;
    if (type == "float64") return ShaderScalarType::Float64;
    return ShaderScalarType::Unknown;
}

ShaderValueType ParseValueType(const nlohmann::json& type)
{
    const std::string kind = type.value("kind", "");
    if (kind == "array")
    {
        ShaderValueType result = ParseValueType(type.at("elementType"));
        if (type.contains("elementCount"))
        {
            if (result.unbounded) return result;
            const u64 outer_count = type.at("elementCount").get<u32>();
            const u64 nested_count = result.array_count;
            const u64 combined_count = outer_count * nested_count;
            ErrorHandling::Ensure(
                combined_count <= std::numeric_limits<u32>::max(),
                "Reflected shader array is too large");
            result.array_count = static_cast<u32>(combined_count);
        }
        else
        {
            result.unbounded = true;
            result.array_count = 0;
        }
        return result;
    }

    const nlohmann::json* scalar = &type;
    ShaderValueType result;
    if (kind == "vector")
    {
        result.rows = type.value("elementCount", 1u);
        scalar = &type.at("elementType");
    }
    else if (kind == "matrix")
    {
        result.rows = type.value("rowCount", 1u);
        result.columns = type.value("columnCount", 1u);
        result.matrix_layout = ShaderMatrixLayout::ColumnMajor;
        scalar = &type.at("elementType");
    }
    result.scalar = ParseScalarType(scalar->value("scalarType", ""));
    return result;
}

ShaderMemoryMember ParseMemoryMember(const nlohmann::json& field)
{
    const nlohmann::json& binding = field.value("binding", nlohmann::json::object());
    const nlohmann::json& type = field.at("type");
    ShaderMemoryMember result{
        .name = field.value("name", ""),
        .type = ParseValueType(type),
        .offset = binding.value("offset", 0ull),
        .size = binding.value("size", 0ull),
        .array_stride = type.value("elementStride", binding.value("elementStride", 0ull)),
        .matrix_stride = type.value("matrixStride", 0ull),
        .members = {},
    };
    const nlohmann::json* fields = nullptr;
    if (type.value("kind", "") == "struct")
    {
        if (type.contains("fields")) fields = &type.at("fields");
    }
    else if (
        type.value("kind", "") == "array" && type.contains("elementType") &&
        type.at("elementType").value("kind", "") == "struct")
    {
        if (type.at("elementType").contains("fields")) fields = &type.at("elementType").at("fields");
    }
    if (fields != nullptr)
    {
        for (const auto& nested : *fields) result.members.push_back(ParseMemoryMember(nested));
    }
    return result;
}

ShaderMemoryLayout
ParseMemoryLayout(std::string name, const nlohmann::json& type, vk::ShaderStageFlags stages, u64 offset = 0)
{
    const nlohmann::json* element_type = &type;
    const nlohmann::json* element_binding = nullptr;
    if (type.value("kind", "") == "constantBuffer")
    {
        element_type = &type.at("elementType");
        if (type.contains("elementVarLayout") && type.at("elementVarLayout").contains("binding"))
        {
            element_binding = &type.at("elementVarLayout").at("binding");
        }
    }
    ShaderMemoryLayout result{
        .name = std::move(name),
        .offset = offset,
        .size = element_binding == nullptr ? 0 : element_binding->value("size", 0ull),
        .alignment = element_binding == nullptr ? 0 : element_binding->value("alignment", 0ull),
        .stages = stages,
        .members = {},
    };
    for (const auto& field : element_type->value("fields", nlohmann::json::array()))
    {
        result.members.push_back(ParseMemoryMember(field));
        result.size = std::max(result.size, result.members.back().offset + result.members.back().size);
    }
    return result;
}

vk::DescriptorType ParseDescriptorType(const nlohmann::json& type)
{
    const std::string kind = type.value("kind", "");
    if (kind == "constantBuffer") return vk::DescriptorType::eUniformBuffer;
    if (kind == "samplerState") return vk::DescriptorType::eSampler;
    if (kind == "resource")
    {
        const std::string shape = type.value("baseShape", "");
        if (shape == "structuredBuffer" || shape == "byteAddressBuffer")
        {
            return vk::DescriptorType::eStorageBuffer;
        }
        if (shape == "textureBuffer") return vk::DescriptorType::eUniformTexelBuffer;
        if (shape == "accelerationStructure") return vk::DescriptorType::eAccelerationStructureKHR;
        if (type.value("combined", false)) return vk::DescriptorType::eCombinedImageSampler;
        return type.value("access", "") == "readWrite" ? vk::DescriptorType::eStorageImage
                                                       : vk::DescriptorType::eSampledImage;
    }
    ErrorHandling::ThrowWithMessage("Unsupported reflected Slang descriptor kind '{}'", kind);
    return {};
}

void AppendInterfaceVariables(
    std::vector<ShaderInterfaceVariable>& result,
    const nlohmann::json& variable,
    u32 inherited_location = 0)
{
    const auto append_type = [&](auto&& self,
                                 const nlohmann::json& reflected_variable,
                                 const nlohmann::json& reflected_type,
                                 u32 fallback_location) -> void
    {
        const std::string kind = reflected_type.value("kind", "");
        if (kind == "outputStream")
        {
            self(self, reflected_variable, reflected_type.at("elementType"), fallback_location);
            return;
        }
        if (kind == "array" && reflected_type.contains("elementType"))
        {
            const nlohmann::json* element = &reflected_type.at("elementType");
            while (element->value("kind", "") == "array" || element->value("kind", "") == "outputStream")
            {
                element = &element->at("elementType");
            }
            // Geometry inputs are primitive arrays and geometry outputs are
            // stream wrappers. Their array/stream cardinality is not part of
            // the inter-stage varying type.
            if (element->value("kind", "") == "struct")
            {
                self(self, reflected_variable, *element, fallback_location);
                return;
            }
        }
        if (kind == "struct")
        {
            u32 next_location = fallback_location;
            for (const auto& field : reflected_type.value("fields", nlohmann::json::array()))
            {
                const size_t previous_size = result.size();
                self(self, field, field.at("type"), next_location);
                for (size_t index = previous_size; index != result.size(); ++index)
                {
                    next_location = std::max(next_location, result[index].location + result[index].location_count);
                }
            }
            return;
        }

        const nlohmann::json& binding = reflected_variable.value("binding", nlohmann::json::object());
        const std::string semantic = reflected_variable.value("semanticName", "");
        ShaderValueType value_type = ParseValueType(reflected_type);
        const u32 location = binding.value("index", fallback_location);
        result.push_back({
            .name = reflected_variable.value("name", ""),
            .semantic = semantic,
            .location = location,
            .location_count = std::max(ShaderValueLocationCount(value_type), 1u),
            .type = value_type,
            .built_in = semantic.starts_with("SV_"),
        });
    };
    append_type(append_type, variable, variable.at("type"), inherited_location);
}

}  // namespace

ShaderInterface ShaderReflectionJson::Read(slang::ProgramLayout& layout)
{
    Slang::ComPtr<slang::IBlob> reflection_blob;
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(layout.toJson(reflection_blob.writeRef())) && reflection_blob,
        "Failed to serialize Slang reflection");
    const nlohmann::json reflection = nlohmann::json::parse(BlobText(reflection_blob));
    ErrorHandling::Ensure(
        reflection.contains("entryPoints") && reflection.at("entryPoints").size() == 1,
        "A Slang stage file must expose exactly one entry point");
    const nlohmann::json& entry_point = reflection.at("entryPoints").front();
    ShaderInterface result{};
    result.language = ShaderSourceLanguage::Slang;
    result.stage = ParseStage(entry_point.at("stage").get<std::string>());
    result.entry_point = entry_point.at("name").get<std::string>();
    if (entry_point.contains("threadGroupSize"))
    {
        result.workgroup_size = entry_point.at("threadGroupSize").get<std::array<u32, 3>>();
    }

    for (const auto& parameter : reflection.value("parameters", nlohmann::json::array()))
    {
        const nlohmann::json& binding = parameter.value("binding", nlohmann::json::object());
        const std::string binding_kind = binding.value("kind", "");
        const nlohmann::json& type = parameter.at("type");
        if (binding_kind == "pushConstantBuffer")
        {
            result.push_constants.push_back(
                ParseMemoryLayout(parameter.value("name", ""), type, result.stage, binding.value("offset", 0ull)));
        }
        else if (binding_kind == "descriptorTableSlot")
        {
            const nlohmann::json* descriptor_type = &type;
            u32 count = 1;
            bool unbounded = false;
            if (type.value("kind", "") == "array")
            {
                descriptor_type = &type.at("elementType");
                if (type.contains("elementCount"))
                {
                    count = type.at("elementCount").get<u32>();
                }
                else
                {
                    unbounded = true;
                    count = 0;
                }
            }
            ShaderDescriptorBinding descriptor{
                .name = parameter.value("name", ""),
                .set = binding.value("space", 0u),
                .binding = binding.value("index", 0u),
                .type = ParseDescriptorType(*descriptor_type),
                .count = count,
                .unbounded = unbounded,
                .access = descriptor_type->value("access", "") == "readWrite" ? ShaderResourceAccess::ReadWrite
                                                                              : ShaderResourceAccess::ReadOnly,
                .stages = result.stage,
                .memory_layout = std::nullopt,
            };
            if (descriptor_type->value("kind", "") == "constantBuffer")
            {
                descriptor.memory_layout = ParseMemoryLayout(descriptor.name, *descriptor_type, {});
            }
            else if (
                descriptor_type->value("kind", "") == "resource" &&
                descriptor_type->value("baseShape", "") == "structuredBuffer")
            {
                descriptor.memory_layout = ParseMemoryLayout(descriptor.name, descriptor_type->at("resultType"), {});
            }
            result.descriptors.push_back(std::move(descriptor));
        }
        else if (binding_kind == "specializationConstant")
        {
            result.specialization_constants.push_back({
                .name = parameter.value("name", ""),
                .id = binding.value("index", 0u),
                .type = ParseValueType(type).scalar,
                .byte_size = ShaderScalarByteSize(ParseValueType(type).scalar),
                .default_value = {},
            });
        }
    }

    for (const auto& parameter : entry_point.value("parameters", nlohmann::json::array()))
    {
        const auto& parameter_type = parameter.at("type");
        const auto& parameter_binding = parameter.value("binding", nlohmann::json::object());
        // Slang serializes InputPatch/OutputPatch wrapper parameters as an
        // opaque `None` type. Their element varyings are represented by the
        // neighboring tessellation stage's result and cannot be decoded here.
        const bool is_opaque_patch = (result.stage == vk::ShaderStageFlagBits::eTessellationControl ||
                                      result.stage == vk::ShaderStageFlagBits::eTessellationEvaluation) &&
                                     parameter_type.value("kind", "") == "None" &&
                                     parameter_binding.value("kind", "") == "varyingInput";
        if (is_opaque_patch) continue;
        if (result.stage == vk::ShaderStageFlagBits::eTessellationEvaluation &&
            parameter_type.value("kind", "") == "struct")
        {
            const auto fields = parameter_type.value("fields", nlohmann::json::array());
            const auto is_tessellation_builtin = [](const auto& field)
            {
                const std::string semantic = field.value("semanticName", "");
                return semantic == "SV_TESSFACTOR" || semantic == "SV_INSIDETESSFACTOR";
            };
            const bool has_tessellation_builtin = std::ranges::any_of(fields, is_tessellation_builtin);
            if (has_tessellation_builtin)
            {
                ErrorHandling::Ensure(
                    std::ranges::all_of(fields, is_tessellation_builtin),
                    "Slang JSON reflection cannot represent domain interfaces with user patch constants");
                // Built-in-only patch constants are not per-vertex stage inputs.
                continue;
            }
        }
        const bool is_output_stream = parameter.at("type").value("kind", "") == "outputStream";
        AppendInterfaceVariables(is_output_stream ? result.outputs : result.inputs, parameter);
    }
    if (entry_point.contains("result")) AppendInterfaceVariables(result.outputs, entry_point.at("result"));

    for (u32 index = 0; index != layout.getParameterCount(); ++index)
    {
        slang::VariableLayoutReflection* parameter = layout.getParameterByIndex(index);
        if (parameter->getCategory() != slang::ParameterCategory::SpecializationConstant) continue;
        const std::string_view name = parameter->getName() == nullptr ? "" : parameter->getName();
        auto constant = std::ranges::find(result.specialization_constants, name, &ShaderSpecializationConstant::name);
        if (constant == result.specialization_constants.end()) continue;
        Slang::ComPtr<slang::IBlob> default_blob;
        if (SLANG_SUCCEEDED(parameter->getVariable()->getDefaultValueBlob(default_blob.writeRef())) && default_blob)
        {
            const auto* begin = static_cast<const u8*>(default_blob->getBufferPointer());
            constant->default_value.assign(begin, begin + default_blob->getBufferSize());
        }
    }
    return result;
}

}  // namespace klvk
