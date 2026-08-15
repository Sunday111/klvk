#include "klvk/vulkan/pipeline_layout.hpp"

#include <algorithm>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

PipelineLayout::PipelineLayout(
    DeviceContext& context,
    std::span<const DescriptorSetLayoutView> set_layouts,
    std::span<const vk::PushConstantRange> push_constants)
{
    std::vector<vk::DescriptorSetLayout> handles;
    handles.reserve(set_layouts.size());
    description_.sets.reserve(set_layouts.size());
    for (const DescriptorSetLayoutView& set : set_layouts)
    {
        ErrorHandling::Ensure(
            set.handle != nullptr && set.description != nullptr,
            "PipelineLayout requires a handle and description for every descriptor set");
        for (const vk::DescriptorSetLayoutBinding& binding : set.description->bindings)
        {
            ErrorHandling::Ensure(
                binding.pImmutableSamplers == nullptr,
                "PipelineLayout cannot retain descriptor layout bindings with immutable samplers");
        }
        handles.push_back(set.handle);
        description_.sets.push_back(*set.description);
    }
    description_.push_constants.assign(push_constants.begin(), push_constants.end());
    const auto create_info =
        vk::PipelineLayoutCreateInfo{}.setSetLayouts(handles).setPushConstantRanges(description_.push_constants);
    layout_ = context.GetDevice().createPipelineLayoutUnique(create_info);
}

ShaderProgramInterface PipelineLayout::Validate(const ShaderStages& stages) const
{
    ShaderProgramInterface program = stages.MergeInterfaces();
    for (const ShaderDescriptorBinding& descriptor : program.descriptors)
    {
        ErrorHandling::Ensure(
            !descriptor.unbounded,
            "Unbounded descriptor set {} binding {} requires variable descriptor flags, which are not modeled",
            descriptor.set,
            descriptor.binding);
        ErrorHandling::Ensure(
            descriptor.set < description_.sets.size(),
            "Shader requires missing descriptor set {}",
            descriptor.set);
        const auto& bindings = description_.sets[descriptor.set].bindings;
        const auto binding = std::ranges::find(bindings, descriptor.binding, &vk::DescriptorSetLayoutBinding::binding);
        ErrorHandling::Ensure(
            binding != bindings.end(),
            "Shader requires missing descriptor set {} binding {}",
            descriptor.set,
            descriptor.binding);
        ErrorHandling::Ensure(
            binding->descriptorType == descriptor.type && binding->descriptorCount >= descriptor.count &&
                (binding->stageFlags & descriptor.stages) == descriptor.stages,
            "Descriptor layout mismatch at set {} binding {}",
            descriptor.set,
            descriptor.binding);
    }

    for (const ShaderMemoryLayout& reflected : program.push_constants)
    {
        u64 required_size = reflected.size;
        if (!reflected.members.empty())
        {
            required_size = 0;
            for (const ShaderMemoryMember& member : reflected.members)
            {
                required_size = std::max(required_size, member.offset + member.size);
            }
        }
        for (u32 stage_bit = 0; stage_bit != 32; ++stage_bit)
        {
            const vk::ShaderStageFlags stage{
                static_cast<vk::ShaderStageFlags::MaskType>(vk::ShaderStageFlags::MaskType{1} << stage_bit)};
            if (!(reflected.stages & stage)) continue;
            const auto range = std::ranges::find_if(
                description_.push_constants,
                [&](const vk::PushConstantRange& value)
                {
                    const u64 declared_end = static_cast<u64>(value.offset) + value.size;
                    const u64 reflected_end = reflected.offset + required_size;
                    return value.offset <= reflected.offset && declared_end >= reflected_end &&
                           static_cast<bool>(value.stageFlags & stage);
                });
            ErrorHandling::Ensure(
                range != description_.push_constants.end(),
                "Push-constant layout does not cover '{}' (offset {}, size {}) for stage {}",
                reflected.name,
                reflected.offset,
                required_size,
                static_cast<vk::ShaderStageFlags::MaskType>(stage));
        }
    }
    return program;
}

}  // namespace klvk
