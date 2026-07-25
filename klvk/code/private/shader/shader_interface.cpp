#include "klvk/shader/shader_interface.hpp"

#include <algorithm>
#include <array>
#include <tuple>
#include <unordered_set>

#include "klvk/error_handling.hpp"

namespace klvk
{
namespace
{

u32 StageOrder(VkShaderStageFlagBits stage)
{
    switch (stage)
    {
    case VK_SHADER_STAGE_VERTEX_BIT:
        return 0;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
        return 1;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
        return 2;
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        return 3;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        return 4;
    case VK_SHADER_STAGE_COMPUTE_BIT:
        return 5;
    default:
        return 6;
    }
}

void ValidateStageLink(const ShaderInterface& producer, const ShaderInterface& consumer)
{
    for (const ShaderInterfaceVariable& input : consumer.inputs)
    {
        if (input.built_in) continue;
        const auto output = std::ranges::find_if(
            producer.outputs,
            [&](const ShaderInterfaceVariable& variable)
            { return !variable.built_in && variable.location == input.location; });
        ErrorHandling::Ensure(
            output != producer.outputs.end(),
            "Shader stage interface mismatch: stage {} does not produce location {} required by stage {} ('{}')",
            static_cast<u32>(producer.stage),
            input.location,
            static_cast<u32>(consumer.stage),
            input.name);
        ErrorHandling::Ensure(
            output->location_count == input.location_count && output->type == input.type,
            "Shader stage interface mismatch at location {}: '{}' and '{}' have incompatible types",
            input.location,
            output->name,
            input.name);
    }
}

void ValidateInterface(const ShaderInterface& interface)
{
    auto validate_variables = [&](const std::vector<ShaderInterfaceVariable>& variables, std::string_view kind)
    {
        std::unordered_set<u32> occupied_locations;
        for (const ShaderInterfaceVariable& variable : variables)
        {
            if (variable.built_in) continue;
            ErrorHandling::Ensure(
                variable.type.scalar != ShaderScalarType::Unknown && variable.location_count != 0,
                "Shader stage {} has unsupported {} variable '{}'",
                static_cast<u32>(interface.stage),
                kind,
                variable.name);
            for (u32 offset = 0; offset != variable.location_count; ++offset)
            {
                ErrorHandling::Ensure(
                    occupied_locations.insert(variable.location + offset).second,
                    "Shader stage {} has overlapping {} location {}",
                    static_cast<u32>(interface.stage),
                    kind,
                    variable.location + offset);
            }
        }
    };
    validate_variables(interface.inputs, "input");
    validate_variables(interface.outputs, "output");

    std::unordered_set<u32> specialization_ids;
    std::unordered_set<std::string_view> specialization_names;
    for (const ShaderSpecializationConstant& constant : interface.specialization_constants)
    {
        ErrorHandling::Ensure(
            specialization_ids.insert(constant.id).second,
            "Shader stage {} has duplicate specialization constant id {}",
            static_cast<u32>(interface.stage),
            constant.id);
        ErrorHandling::Ensure(
            specialization_names.insert(constant.name).second,
            "Shader stage {} has duplicate specialization constant name '{}'",
            static_cast<u32>(interface.stage),
            constant.name);
        ErrorHandling::Ensure(
            constant.type != ShaderScalarType::Unknown && constant.byte_size == ShaderScalarByteSize(constant.type),
            "Shader specialization constant '{}' has an unsupported type or size",
            constant.name);
    }
    if (interface.stage == VK_SHADER_STAGE_COMPUTE_BIT)
    {
        ErrorHandling::Ensure(
            std::ranges::all_of(interface.workgroup_size, [](u32 value) { return value != 0; }),
            "Compute shader '{}' has an invalid workgroup size",
            interface.entry_point);
    }
}

}  // namespace

u32 ShaderScalarByteSize(ShaderScalarType type)
{
    switch (type)
    {
    case ShaderScalarType::Bool:
    case ShaderScalarType::Int32:
    case ShaderScalarType::UInt32:
    case ShaderScalarType::Float32:
        return 4;
    case ShaderScalarType::Float16:
        return 2;
    case ShaderScalarType::Float64:
        return 8;
    case ShaderScalarType::Unknown:
        return 0;
    }
    return 0;
}

u32 ShaderValueLocationCount(const ShaderValueType& type)
{
    const u32 columns = std::max(type.columns, 1u);
    const u32 array_count = type.unbounded ? 0 : std::max(type.array_count, 1u);
    const u32 scalar_bytes = ShaderScalarByteSize(type.scalar);
    if (scalar_bytes == 0 || array_count == 0) return 0;
    const u32 column_bytes = std::max(type.rows, 1u) * scalar_bytes;
    const u32 locations_per_column = (column_bytes + 15u) / 16u;
    return columns * array_count * locations_per_column;
}

ShaderProgramInterface MergeShaderInterfaces(const std::vector<std::shared_ptr<const ShaderInterface>>& interfaces)
{
    ErrorHandling::Ensure(!interfaces.empty(), "Cannot merge an empty shader interface set");

    std::vector<const ShaderInterface*> ordered;
    ordered.reserve(interfaces.size());
    VkShaderStageFlags stage_mask = 0;
    bool has_compute = false;
    for (const auto& interface : interfaces)
    {
        ErrorHandling::Ensure(interface != nullptr, "Cannot merge an unreflected shader stage");
        ValidateInterface(*interface);
        ErrorHandling::Ensure(
            !(stage_mask & interface->stage),
            "Shader program contains duplicate stage {}",
            static_cast<u32>(interface->stage));
        stage_mask |= interface->stage;
        has_compute |= interface->stage == VK_SHADER_STAGE_COMPUTE_BIT;
        ordered.push_back(interface.get());
    }
    ErrorHandling::Ensure(
        !has_compute || stage_mask == VK_SHADER_STAGE_COMPUTE_BIT,
        "Compute and graphics shader stages cannot be combined");
    std::ranges::sort(ordered, {}, [](const ShaderInterface* interface) { return StageOrder(interface->stage); });
    if (!has_compute)
    {
        for (size_t index = 1; index != ordered.size(); ++index)
        {
            ValidateStageLink(*ordered[index - 1], *ordered[index]);
        }
    }

    ShaderProgramInterface result{};
    result.stages = stage_mask;
    for (const ShaderInterface* interface : ordered)
    {
        for (const ShaderDescriptorBinding& descriptor : interface->descriptors)
        {
            auto existing = std::ranges::find_if(
                result.descriptors,
                [&](const ShaderDescriptorBinding& value)
                { return value.set == descriptor.set && value.binding == descriptor.binding; });
            if (existing == result.descriptors.end())
            {
                result.descriptors.push_back(descriptor);
                continue;
            }
            ErrorHandling::Ensure(
                existing->type == descriptor.type && existing->count == descriptor.count &&
                    existing->unbounded == descriptor.unbounded && existing->access == descriptor.access &&
                    existing->memory_layout == descriptor.memory_layout,
                "Shader descriptor conflict at set {} binding {}",
                descriptor.set,
                descriptor.binding);
            existing->stages |= descriptor.stages;
        }

        for (const ShaderMemoryLayout& range : interface->push_constants)
        {
            auto identical = std::ranges::find_if(
                result.push_constants,
                [&](const ShaderMemoryLayout& value)
                {
                    return value.offset == range.offset && value.size == range.size && value.name == range.name &&
                           value.alignment == range.alignment && value.members == range.members;
                });
            if (identical != result.push_constants.end())
            {
                identical->stages |= range.stages;
                continue;
            }
            for (const ShaderMemoryLayout& existing : result.push_constants)
            {
                const bool overlaps =
                    range.offset < existing.offset + existing.size && existing.offset < range.offset + range.size;
                ErrorHandling::Ensure(
                    !overlaps || !(range.stages & existing.stages),
                    "Incompatible push-constant overlap between '{}' and '{}'",
                    existing.name,
                    range.name);
            }
            result.push_constants.push_back(range);
        }
    }

    std::ranges::sort(
        result.descriptors,
        {},
        [](const ShaderDescriptorBinding& descriptor) { return std::pair{descriptor.set, descriptor.binding}; });
    std::ranges::sort(
        result.push_constants,
        {},
        [](const ShaderMemoryLayout& range) { return std::pair{range.offset, range.size}; });
    return result;
}

}  // namespace klvk
