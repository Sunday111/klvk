#include "klvk/shader/shader_cache_manager.hpp"

#include <fmt/format.h>
#include <slang-com-ptr.h>
#include <slang-tag-version.h>
#include <slang.h>

#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <ranges>
#include <nlohmann/json.hpp>
#include <shaderc/shaderc.hpp>
#include <span>
#include <sstream>
#include <utility>

#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/shader/shader_interface.hpp"

namespace klvk
{
namespace
{

constexpr u64 kFnvOffset = 14695981039346656037ull;
constexpr u64 kFnvPrime = 1099511628211ull;
constexpr u32 kCacheFormatVersion = 2;
constexpr u32 kReflectionMetadataVersion = 4;
constexpr u32 kSpirvMagic = 0x07230203;
constexpr size_t kMaximumSpirvBytes = 256 * 1024 * 1024;
constexpr size_t kMaximumMetadataBytes = 16 * 1024 * 1024;
constexpr std::array<char, 8> kCacheMagic{'K', 'L', 'V', 'K', 'S', 'P', 'V', '2'};

struct CacheHeader
{
    std::array<char, 8> magic{};
    u32 format_version = 0;
    u32 spirv_version = 0;
    u32 spirv_revision = 0;
    u32 reserved = 0;
    u64 key = 0;
    u64 word_count = 0;
    u64 metadata_size = 0;
    u64 spirv_hash = 0;
    u64 metadata_hash = 0;
};

static_assert(std::is_trivially_copyable_v<CacheHeader>);

u64 HashBytes(u64 hash, const void* bytes, size_t size)
{
    const auto* data = static_cast<const u8*>(bytes);
    for (size_t i = 0; i != size; ++i)
    {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

template <typename T>
u64 HashValue(u64 hash, const T& value)
{
    return HashBytes(hash, &value, sizeof(value));
}

u64 HashWords(std::span<const u32> words)
{
    return HashBytes(kFnvOffset, words.data(), words.size_bytes());
}

u64 HashString(std::string_view text)
{
    return HashBytes(kFnvOffset, text.data(), text.size());
}

std::filesystem::path CachePath(const std::filesystem::path& root, u64 key)
{
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << key << ".spv.cache";
    return root / name.str();
}

shaderc_shader_kind ShaderKind(const std::filesystem::path& path)
{
    const std::string extension = path.extension().string();
    if (extension == ".vert") return shaderc_glsl_vertex_shader;
    if (extension == ".frag") return shaderc_glsl_fragment_shader;
    if (extension == ".geom") return shaderc_glsl_geometry_shader;
    if (extension == ".comp") return shaderc_glsl_compute_shader;
    if (extension == ".tesc") return shaderc_glsl_tess_control_shader;
    if (extension == ".tese") return shaderc_glsl_tess_evaluation_shader;
    ErrorHandling::ThrowWithMessage("Unsupported shader stage extension '{}' for {}", extension, path.string());
    return shaderc_glsl_infer_from_source;
}

// Distinguishes a Slang cache entry from a GLSL one so identical bytes under the
// two languages never collide.
constexpr u32 kSlangLanguageTag = 0x5'1A;
constexpr std::string_view kSlangTargetProfile = "spirv_1_6";
constexpr SlangCompileTarget kSlangTargetFormat = SLANG_SPIRV;
constexpr SlangTargetFlags kSlangTargetFlags = kDefaultTargetFlags;
constexpr SlangFloatingPointMode kSlangFloatingPointMode = SLANG_FLOATING_POINT_MODE_DEFAULT;
constexpr SlangLineDirectiveMode kSlangLineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_DEFAULT;
constexpr bool kSlangForceScalarBufferLayout = false;
constexpr slang::SessionFlags kSlangSessionFlags = slang::kSessionFlags_None;
constexpr SlangMatrixLayoutMode kSlangMatrixLayout = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
constexpr bool kSlangEnableEffectAnnotations = false;
constexpr bool kSlangAllowGlslSyntax = false;
constexpr bool kSlangSkipSpirvValidation = false;

u64 MakeKey(std::string_view source, shaderc_shader_kind kind, u32 spirv_version, u32 spirv_revision)
{
    u64 hash = HashBytes(kFnvOffset, source.data(), source.size());
    hash = HashValue(hash, kind);
    hash = HashValue(hash, spirv_version);
    hash = HashValue(hash, spirv_revision);
    hash = HashValue(hash, kCacheFormatVersion);
#ifdef NDEBUG
    constexpr bool optimize = true;
#else
    constexpr bool optimize = false;
#endif
    return HashValue(hash, optimize);
}

// Slang cache entries are currently restricted to self-contained source files.
// Hash every output-affecting target/session option used below as well as the
// compiler version. Dependency-bearing modules are rejected at the compiler
// boundary until their transitive contents can participate in this key.
u64 MakeSlangKey(std::string_view source)
{
    u64 hash = HashBytes(kFnvOffset, source.data(), source.size());
    hash = HashValue(hash, kSlangLanguageTag);
    constexpr std::string_view toolchain = SLANG_TAG_VERSION;
    hash = HashBytes(hash, toolchain.data(), toolchain.size());
    hash = HashValue(hash, kSlangTargetFormat);
    hash = HashBytes(hash, kSlangTargetProfile.data(), kSlangTargetProfile.size());
    hash = HashValue(hash, kSlangTargetFlags);
    hash = HashValue(hash, kSlangFloatingPointMode);
    hash = HashValue(hash, kSlangLineDirectiveMode);
    hash = HashValue(hash, kSlangForceScalarBufferLayout);
    hash = HashValue(hash, kSlangSessionFlags);
    hash = HashValue(hash, kSlangMatrixLayout);
    hash = HashValue(hash, kSlangEnableEffectAnnotations);
    hash = HashValue(hash, kSlangAllowGlslSyntax);
    hash = HashValue(hash, kSlangSkipSpirvValidation);
    hash = HashValue(hash, kReflectionMetadataVersion);
    hash = HashValue(hash, kCacheFormatVersion);
    return hash;
}

slang::IGlobalSession& SlangGlobalSession()
{
    // The Slang global session is a process-wide, reusable compiler. Created on
    // first use so a run that compiles only GLSL never pays to spin it up.
    static Slang::ComPtr<slang::IGlobalSession> session = []
    {
        Slang::ComPtr<slang::IGlobalSession> created;
        ErrorHandling::Ensure(
            SLANG_SUCCEEDED(slang::createGlobalSession(created.writeRef())) && created,
            "Failed to create Slang global session");
        return created;
    }();
    return *session;
}

std::string BlobText(slang::IBlob* blob)
{
    if (blob == nullptr || blob->getBufferSize() == 0) return {};
    std::string text(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
    return text;
}

VkShaderStageFlagBits ParseStage(std::string_view stage)
{
    if (stage == "vertex") return VK_SHADER_STAGE_VERTEX_BIT;
    if (stage == "fragment") return VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stage == "geometry") return VK_SHADER_STAGE_GEOMETRY_BIT;
    if (stage == "hull") return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (stage == "domain") return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (stage == "compute") return VK_SHADER_STAGE_COMPUTE_BIT;
    ErrorHandling::ThrowWithMessage("Unsupported reflected Slang stage '{}'", stage);
    return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
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
ParseMemoryLayout(std::string name, const nlohmann::json& type, VkShaderStageFlags stages, u64 offset = 0)
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

VkDescriptorType ParseDescriptorType(const nlohmann::json& type)
{
    const std::string kind = type.value("kind", "");
    if (kind == "constantBuffer") return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    if (kind == "samplerState") return VK_DESCRIPTOR_TYPE_SAMPLER;
    if (kind == "resource")
    {
        const std::string shape = type.value("baseShape", "");
        if (shape == "structuredBuffer" || shape == "byteAddressBuffer")
        {
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        if (shape == "textureBuffer") return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        if (shape == "accelerationStructure") return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        if (type.value("combined", false)) return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        return type.value("access", "") == "readWrite" ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                       : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    }
    ErrorHandling::ThrowWithMessage("Unsupported reflected Slang descriptor kind '{}'", kind);
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
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

nlohmann::json ValueTypeToJson(const ShaderValueType& type)
{
    return {
        {"scalar", static_cast<u32>(type.scalar)},
        {"rows", type.rows},
        {"columns", type.columns},
        {"array_count", type.array_count},
        {"unbounded", type.unbounded},
        {"matrix_layout", static_cast<u32>(type.matrix_layout)},
    };
}

ShaderValueType ValueTypeFromJson(const nlohmann::json& json)
{
    return {
        .scalar = static_cast<ShaderScalarType>(json.at("scalar").get<u32>()),
        .rows = json.at("rows").get<u32>(),
        .columns = json.at("columns").get<u32>(),
        .array_count = json.at("array_count").get<u32>(),
        .unbounded = json.at("unbounded").get<bool>(),
        .matrix_layout = static_cast<ShaderMatrixLayout>(json.at("matrix_layout").get<u32>()),
    };
}

nlohmann::json MemoryMemberToJson(const ShaderMemoryMember& member)
{
    nlohmann::json nested = nlohmann::json::array();
    for (const auto& value : member.members) nested.push_back(MemoryMemberToJson(value));
    return {
        {"name", member.name},
        {"type", ValueTypeToJson(member.type)},
        {"offset", member.offset},
        {"size", member.size},
        {"alignment", member.alignment},
        {"array_stride", member.array_stride},
        {"matrix_stride", member.matrix_stride},
        {"members", std::move(nested)},
    };
}

ShaderMemoryMember MemoryMemberFromJson(const nlohmann::json& json)
{
    ShaderMemoryMember result{
        .name = json.at("name").get<std::string>(),
        .type = ValueTypeFromJson(json.at("type")),
        .offset = json.at("offset").get<u64>(),
        .size = json.at("size").get<u64>(),
        .alignment = json.at("alignment").get<u64>(),
        .array_stride = json.at("array_stride").get<u64>(),
        .matrix_stride = json.at("matrix_stride").get<u64>(),
        .members = {},
    };
    for (const auto& nested : json.at("members")) result.members.push_back(MemoryMemberFromJson(nested));
    return result;
}

nlohmann::json MemoryLayoutToJson(const ShaderMemoryLayout& layout)
{
    nlohmann::json members = nlohmann::json::array();
    for (const auto& value : layout.members) members.push_back(MemoryMemberToJson(value));
    return {
        {"name", layout.name},
        {"offset", layout.offset},
        {"size", layout.size},
        {"alignment", layout.alignment},
        {"stages", layout.stages},
        {"members", std::move(members)},
    };
}

ShaderMemoryLayout MemoryLayoutFromJson(const nlohmann::json& json)
{
    ShaderMemoryLayout result{
        .name = json.at("name").get<std::string>(),
        .offset = json.at("offset").get<u64>(),
        .size = json.at("size").get<u64>(),
        .alignment = json.at("alignment").get<u64>(),
        .stages = json.at("stages").get<VkShaderStageFlags>(),
        .members = {},
    };
    for (const auto& member : json.at("members")) result.members.push_back(MemoryMemberFromJson(member));
    return result;
}

std::string SerializeInterface(const ShaderInterface& interface)
{
    nlohmann::json descriptors = nlohmann::json::array();
    for (const auto& descriptor : interface.descriptors)
    {
        descriptors.push_back({
            {"name", descriptor.name},
            {"set", descriptor.set},
            {"binding", descriptor.binding},
            {"type", static_cast<u32>(descriptor.type)},
            {"count", descriptor.count},
            {"unbounded", descriptor.unbounded},
            {"access", static_cast<u32>(descriptor.access)},
            {"stages", descriptor.stages},
            {"memory_layout",
             descriptor.memory_layout ? MemoryLayoutToJson(*descriptor.memory_layout) : nlohmann::json(nullptr)},
        });
    }
    nlohmann::json push_constants = nlohmann::json::array();
    for (const auto& value : interface.push_constants) push_constants.push_back(MemoryLayoutToJson(value));
    auto variables = [](const std::vector<ShaderInterfaceVariable>& values)
    {
        nlohmann::json json = nlohmann::json::array();
        for (const auto& value : values)
        {
            json.push_back({
                {"name", value.name},
                {"semantic", value.semantic},
                {"location", value.location},
                {"location_count", value.location_count},
                {"type", ValueTypeToJson(value.type)},
                {"built_in", value.built_in},
            });
        }
        return json;
    };
    nlohmann::json constants = nlohmann::json::array();
    for (const auto& value : interface.specialization_constants)
    {
        constants.push_back({
            {"name", value.name},
            {"id", value.id},
            {"type", static_cast<u32>(value.type)},
            {"byte_size", value.byte_size},
            {"default_value", value.default_value},
        });
    }
    return nlohmann::json{
        {"version", kReflectionMetadataVersion},
        {"language", static_cast<u32>(interface.language)},
        {"stage", static_cast<u32>(interface.stage)},
        {"entry_point", interface.entry_point},
        {"descriptors", std::move(descriptors)},
        {"push_constants", std::move(push_constants)},
        {"inputs", variables(interface.inputs)},
        {"outputs", variables(interface.outputs)},
        {"specialization_constants", std::move(constants)},
        {"workgroup_size", interface.workgroup_size},
    }
        .dump();
}

ShaderInterface DeserializeInterface(std::string_view text)
{
    const nlohmann::json json = nlohmann::json::parse(text);
    ErrorHandling::Ensure(
        json.at("version") == kReflectionMetadataVersion,
        "Unsupported shader interface metadata version");
    ShaderInterface result{};
    result.language = static_cast<ShaderSourceLanguage>(json.at("language").get<u32>());
    result.stage = static_cast<VkShaderStageFlagBits>(json.at("stage").get<u32>());
    result.entry_point = json.at("entry_point").get<std::string>();
    result.workgroup_size = json.at("workgroup_size").get<std::array<u32, 3>>();
    for (const auto& descriptor : json.at("descriptors"))
    {
        ShaderDescriptorBinding value{
            .name = descriptor.at("name").get<std::string>(),
            .set = descriptor.at("set").get<u32>(),
            .binding = descriptor.at("binding").get<u32>(),
            .type = static_cast<VkDescriptorType>(descriptor.at("type").get<u32>()),
            .count = descriptor.at("count").get<u32>(),
            .unbounded = descriptor.at("unbounded").get<bool>(),
            .access = static_cast<ShaderResourceAccess>(descriptor.at("access").get<u32>()),
            .stages = descriptor.at("stages").get<VkShaderStageFlags>(),
            .memory_layout = std::nullopt,
        };
        if (!descriptor.at("memory_layout").is_null())
        {
            value.memory_layout = MemoryLayoutFromJson(descriptor.at("memory_layout"));
        }
        result.descriptors.push_back(std::move(value));
    }
    for (const auto& value : json.at("push_constants"))
    {
        result.push_constants.push_back(MemoryLayoutFromJson(value));
    }
    auto parse_variables = [](const nlohmann::json& values)
    {
        return values | std::views::transform([](const nlohmann::json& value) {
                   return ShaderInterfaceVariable{
                       .name = value.at("name").get<std::string>(),
                       .semantic = value.at("semantic").get<std::string>(),
                       .location = value.at("location").get<u32>(),
                       .location_count = value.at("location_count").get<u32>(),
                       .type = ValueTypeFromJson(value.at("type")),
                       .built_in = value.at("built_in").get<bool>(),
                   };
               }) |
               std::ranges::to<std::vector>();
    };
    result.inputs = parse_variables(json.at("inputs"));
    result.outputs = parse_variables(json.at("outputs"));
    for (const auto& value : json.at("specialization_constants"))
    {
        result.specialization_constants.push_back({
            .name = value.at("name").get<std::string>(),
            .id = value.at("id").get<u32>(),
            .type = static_cast<ShaderScalarType>(value.at("type").get<u32>()),
            .byte_size = value.at("byte_size").get<u32>(),
            .default_value = value.at("default_value").get<std::vector<u8>>(),
        });
    }
    return result;
}

ShaderInterface ParseSlangInterface(slang::ProgramLayout& layout)
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
                descriptor.memory_layout = ParseMemoryLayout(descriptor.name, *descriptor_type, 0);
            }
            else if (
                descriptor_type->value("kind", "") == "resource" &&
                descriptor_type->value("baseShape", "") == "structuredBuffer")
            {
                descriptor.memory_layout = ParseMemoryLayout(descriptor.name, descriptor_type->at("resultType"), 0);
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
        const bool is_opaque_patch = (result.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
                                      result.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) &&
                                     parameter_type.value("kind", "") == "None" &&
                                     parameter_binding.value("kind", "") == "varyingInput";
        if (is_opaque_patch) continue;
        if (result.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT && parameter_type.value("kind", "") == "struct")
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

struct SlangCompileResult
{
    std::vector<u32> spirv;
    ShaderInterface interface;
};

SlangCompileResult CompileSlangToSpirv(const std::string& source, const std::filesystem::path& source_path)
{
    slang::IGlobalSession& global = SlangGlobalSession();

    slang::TargetDesc target{};
    target.format = kSlangTargetFormat;
    target.profile = global.findProfile(kSlangTargetProfile.data());
    target.flags = kSlangTargetFlags;
    target.floatingPointMode = kSlangFloatingPointMode;
    target.lineDirectiveMode = kSlangLineDirectiveMode;
    target.forceGLSLScalarBufferLayout = kSlangForceScalarBufferLayout;

    slang::SessionDesc session_desc{};
    session_desc.targets = &target;
    session_desc.targetCount = 1;
    session_desc.flags = kSlangSessionFlags;
    session_desc.defaultMatrixLayoutMode = kSlangMatrixLayout;
    session_desc.enableEffectAnnotations = kSlangEnableEffectAnnotations;
    session_desc.allowGLSLSyntax = kSlangAllowGlslSyntax;
    session_desc.skipSPIRVValidation = kSlangSkipSpirvValidation;

    Slang::ComPtr<slang::ISession> session;
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(global.createSession(session_desc, session.writeRef())) && session,
        "Failed to create Slang session for '{}'",
        source_path.string());

    const std::string module_name = source_path.stem().string();
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModuleFromSourceString(
        module_name.c_str(),
        source_path.string().c_str(),
        source.c_str(),
        diagnostics.writeRef());
    ErrorHandling::Ensure(
        module != nullptr,
        "Failed to compile Slang shader '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));
    const SlangInt32 dependency_count = module->getDependencyFileCount();
    bool has_external_dependency = false;
    std::string external_dependencies;
    for (SlangInt32 index = 0; index != dependency_count; ++index)
    {
        const char* dependency_path = module->getDependencyFilePath(index);
        if (dependency_path == nullptr ||
            std::filesystem::weakly_canonical(dependency_path) != std::filesystem::weakly_canonical(source_path))
        {
            has_external_dependency = true;
            if (!external_dependencies.empty()) external_dependencies += ", ";
            external_dependencies += dependency_path == nullptr ? "<unknown>" : dependency_path;
        }
    }
    ErrorHandling::Ensure(
        !has_external_dependency,
        "Slang shader '{}' references external dependency files ({}); imports and includes are unsupported until "
        "shader "
        "cache keys track transitive dependency contents",
        source_path.string(),
        external_dependencies);

    // One stage per file, entry point named main so the pipeline's hardcoded
    // "main" entry name resolves in the emitted SPIR-V.
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(module->findEntryPointByName("main", entry_point.writeRef())) && entry_point,
        "Slang shader '{}' must define an entry point named 'main'",
        source_path.string());

    const std::array<slang::IComponentType*, 2> components{module, entry_point.get()};
    Slang::ComPtr<slang::IComponentType> composed;
    diagnostics.setNull();
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(session->createCompositeComponentType(
            components.data(),
            components.size(),
            composed.writeRef(),
            diagnostics.writeRef())) &&
            composed,
        "Failed to compose Slang program '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));

    Slang::ComPtr<slang::IComponentType> linked;
    diagnostics.setNull();
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(composed->link(linked.writeRef(), diagnostics.writeRef())) && linked,
        "Failed to link Slang program '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));

    Slang::ComPtr<slang::IBlob> spirv;
    diagnostics.setNull();
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(linked->getEntryPointCode(0, 0, spirv.writeRef(), diagnostics.writeRef())) && spirv,
        "Failed to emit SPIR-V for Slang shader '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));

    const size_t byte_size = spirv->getBufferSize();
    ErrorHandling::Ensure(
        byte_size != 0 && byte_size % sizeof(u32) == 0,
        "Slang produced an invalid SPIR-V size for '{}'",
        source_path.string());
    const auto* words = static_cast<const u32*>(spirv->getBufferPointer());
    const size_t word_count = byte_size / sizeof(u32);
    diagnostics.setNull();
    slang::ProgramLayout* layout = linked->getLayout(0, diagnostics.writeRef());
    ErrorHandling::Ensure(
        layout != nullptr,
        "Failed to reflect Slang shader '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));
    return {
        .spirv = std::vector<u32>(words, words + word_count),
        .interface = ParseSlangInterface(*layout),
    };
}

}  // namespace

ShaderCacheManager::ShaderCacheManager(const std::filesystem::path& source_root, std::filesystem::path cache_root)
    : ShaderCacheManager(source_root, std::move(cache_root), Settings{})
{
}

ShaderCacheManager::ShaderCacheManager(
    const std::filesystem::path& source_root,
    std::filesystem::path cache_root,
    Settings settings)
    : source_root_(std::filesystem::weakly_canonical(source_root)),
      cache_root_(std::move(cache_root)),
      settings_(settings)
{
    ErrorHandling::Ensure(settings_.flush_interval.count() > 0, "Shader cache flush interval must be positive");
    if (cache_root_.empty()) cache_root_ = source_root_.parent_path().parent_path() / "shader_cache";
    std::filesystem::create_directories(cache_root_);
    shaderc_get_spv_version(&compiler_spirv_version_, &compiler_spirv_revision_);
    worker_ = std::thread(&ShaderCacheManager::WorkerMain, this);
}

ShaderCacheManager::~ShaderCacheManager()
{
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::shared_ptr<const CompiledShader> ShaderCacheManager::GetOrCompile(const std::filesystem::path& source_path)
{
    const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(source_path);
    const auto relative = canonical_path.lexically_relative(source_root_);
    ErrorHandling::Ensure(
        !relative.empty() && *relative.begin() != "..",
        "Shader source '{}' is outside shader root '{}'",
        canonical_path.string(),
        source_root_.string());

    std::string source;
    Filesystem::ReadFile(canonical_path, source);
    // A .slang source routes to the Slang compiler; every other extension stays
    // on shaderc. Both emit SPIR-V, so nothing downstream of the cache changes.
    const bool is_slang = canonical_path.extension() == ".slang";
    const u64 key =
        is_slang ? MakeSlangKey(source)
                 : MakeKey(source, ShaderKind(canonical_path), compiler_spirv_version_, compiler_spirv_revision_);

    std::unique_lock lock(mutex_);
    auto iterator = entries_.find(key);
    if (iterator == entries_.end())
    {
        auto entry = std::make_shared<Entry>();
        jobs_.push({.key = key, .source_path = canonical_path, .source = std::move(source), .entry = entry});
        iterator = entries_.emplace(key, std::move(entry)).first;
        condition_.notify_one();
    }

    const std::shared_ptr<Entry> entry = iterator->second;
    condition_.wait(lock, [&] { return entry->state != EntryState::Pending; });
    if (entry->state == EntryState::Failed) std::rethrow_exception(entry->failure);
    return entry->shader;
}

void ShaderCacheManager::WorkerMain()
{
    auto next_flush = std::chrono::steady_clock::now() + settings_.flush_interval;
    for (;;)
    {
        std::optional<CompileJob> job;
        bool should_flush = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait_until(lock, next_flush, [&] { return stopping_ || !jobs_.empty(); });
            if (!jobs_.empty())
            {
                job = std::move(jobs_.front());
                jobs_.pop();
            }
            should_flush = std::chrono::steady_clock::now() >= next_flush || (stopping_ && jobs_.empty());
            if (stopping_ && !job && jobs_.empty() && !should_flush) should_flush = true;
        }

        if (job) Compile(*job);
        if (should_flush)
        {
            FlushDirtyEntries();
            next_flush = std::chrono::steady_clock::now() + settings_.flush_interval;
        }

        std::scoped_lock lock(mutex_);
        // Shutdown makes one final best-effort flush. A read-only/full disk must
        // never turn application destruction into an infinite join.
        if (stopping_ && jobs_.empty()) break;
    }
}

void ShaderCacheManager::Compile(const CompileJob& job)
{
    try
    {
        if (auto cached = TryLoad(job.key))
        {
            {
                std::scoped_lock lock(mutex_);
                job.entry->shader = std::move(cached);
                job.entry->state = EntryState::Ready;
            }
            condition_.notify_all();
            return;
        }

        std::vector<u32> spirv_words;
        std::shared_ptr<const ShaderInterface> interface;
        if (job.source_path.extension() == ".slang")
        {
            SlangCompileResult result = CompileSlangToSpirv(job.source, job.source_path);
            spirv_words = std::move(result.spirv);
            interface = std::make_shared<const ShaderInterface>(std::move(result.interface));
        }
        else
        {
            shaderc::Compiler compiler;
            shaderc::CompileOptions options;
            options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
            options.SetTargetSpirv(shaderc_spirv_version_1_6);
#ifdef NDEBUG
            options.SetOptimizationLevel(shaderc_optimization_level_performance);
#else
            options.SetOptimizationLevel(shaderc_optimization_level_zero);
            options.SetGenerateDebugInfo();
#endif
            const auto result = compiler.CompileGlslToSpv(
                job.source,
                ShaderKind(job.source_path),
                job.source_path.string().c_str(),
                options);
            ErrorHandling::Ensure(
                result.GetCompilationStatus() == shaderc_compilation_status_success,
                "Failed to compile shader '{}':\n{}",
                job.source_path.string(),
                result.GetErrorMessage());
            spirv_words.assign(result.cbegin(), result.cend());
        }
        auto words = std::make_shared<const std::vector<u32>>(std::move(spirv_words));
        ErrorHandling::Ensure(!words->empty() && words->front() == kSpirvMagic, "Compiler returned invalid SPIR-V");
        auto shader = std::make_shared<const CompiledShader>(CompiledShader{
            .spirv = std::move(words),
            .interface = std::move(interface),
        });
        {
            std::scoped_lock lock(mutex_);
            job.entry->shader = std::move(shader);
            job.entry->generation = next_generation_++;
            job.entry->state = EntryState::Ready;
        }
    }
    catch (...)
    {
        std::scoped_lock lock(mutex_);
        job.entry->failure = std::current_exception();
        job.entry->state = EntryState::Failed;
    }
    condition_.notify_all();
}

void ShaderCacheManager::FlushDirtyEntries()
{
    struct Snapshot
    {
        u64 key = 0;
        u64 generation = 0;
        std::shared_ptr<const CompiledShader> shader;
        std::shared_ptr<Entry> entry;
    };
    std::vector<Snapshot> snapshots;
    {
        std::scoped_lock lock(mutex_);
        for (const auto& [key, entry] : entries_)
        {
            if (entry->state == EntryState::Ready && entry->shader && entry->generation != entry->persisted_generation)
            {
                snapshots.push_back(
                    {.key = key, .generation = entry->generation, .shader = entry->shader, .entry = entry});
            }
        }
    }

    for (const Snapshot& snapshot : snapshots)
    {
        std::filesystem::path temporary;
        try
        {
            const std::filesystem::path destination = CachePath(cache_root_, snapshot.key);
            temporary = destination;
            static std::atomic<u64> temporary_sequence = 0;
            temporary += fmt::format(
                ".tmp.{}.{}",
                os::GetProcessId(),
                temporary_sequence.fetch_add(1, std::memory_order_relaxed));
            const std::string metadata =
                snapshot.shader->interface ? SerializeInterface(*snapshot.shader->interface) : std::string{};
            const CacheHeader header{
                .magic = kCacheMagic,
                .format_version = kCacheFormatVersion,
                .spirv_version = compiler_spirv_version_,
                .spirv_revision = compiler_spirv_revision_,
                .key = snapshot.key,
                .word_count = snapshot.shader->spirv->size(),
                .metadata_size = metadata.size(),
                .spirv_hash = HashWords(*snapshot.shader->spirv),
                .metadata_hash = HashString(metadata),
            };
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                ErrorHandling::Ensure(file.is_open(), "Failed to open shader cache file '{}'", temporary.string());
                file.write(reinterpret_cast<const char*>(&header), sizeof(header));
                file.write(
                    reinterpret_cast<const char*>(snapshot.shader->spirv->data()),
                    static_cast<std::streamsize>(snapshot.shader->spirv->size() * sizeof(u32)));
                file.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
                file.flush();
                ErrorHandling::Ensure(file.good(), "Failed to write shader cache file '{}'", temporary.string());
                file.close();
                ErrorHandling::Ensure(!file.fail(), "Failed to close shader cache file '{}'", temporary.string());
            }
            Filesystem::InstallFileAtomically(temporary, destination);
            std::scoped_lock lock(mutex_);
            if (snapshot.entry->generation == snapshot.generation)
            {
                snapshot.entry->persisted_generation = snapshot.generation;
            }
        }
        catch (const std::exception& exception)
        {
            if (!temporary.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            }
            fmt::println(stderr, "[shader cache] {}", exception.what());
        }
    }
}

std::shared_ptr<const CompiledShader> ShaderCacheManager::TryLoad(u64 key) const
{
    const std::filesystem::path path = CachePath(cache_root_, key);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    const std::streamsize size = file.tellg();
    if (std::cmp_less(size, sizeof(CacheHeader)) ||
        std::cmp_greater(size, sizeof(CacheHeader) + kMaximumSpirvBytes + kMaximumMetadataBytes))
    {
        return {};
    }
    file.seekg(0);
    CacheHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != kCacheMagic || header.format_version != kCacheFormatVersion || header.key != key ||
        header.spirv_version != compiler_spirv_version_ || header.spirv_revision != compiler_spirv_revision_ ||
        header.word_count == 0 || header.word_count > kMaximumSpirvBytes / sizeof(u32) ||
        header.metadata_size > kMaximumMetadataBytes ||
        std::cmp_not_equal(size, sizeof(CacheHeader) + header.word_count * sizeof(u32) + header.metadata_size))
    {
        return {};
    }
    auto words = std::make_shared<std::vector<u32>>(static_cast<size_t>(header.word_count));
    file.read(reinterpret_cast<char*>(words->data()), static_cast<std::streamsize>(words->size() * sizeof(u32)));
    std::string metadata(static_cast<size_t>(header.metadata_size), '\0');
    file.read(metadata.data(), static_cast<std::streamsize>(metadata.size()));
    if (!file || words->front() != kSpirvMagic || HashWords(*words) != header.spirv_hash ||
        HashString(metadata) != header.metadata_hash)
    {
        return {};
    }
    std::shared_ptr<const ShaderInterface> interface;
    if (!metadata.empty())
    {
        try
        {
            interface = std::make_shared<const ShaderInterface>(DeserializeInterface(metadata));
        }
        catch (const std::exception&)
        {
            return {};
        }
    }
    return std::make_shared<const CompiledShader>(CompiledShader{
        .spirv = std::move(words),
        .interface = std::move(interface),
    });
}

}  // namespace klvk
