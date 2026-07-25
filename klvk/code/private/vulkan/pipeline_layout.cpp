#include "klvk/vulkan/pipeline_layout.hpp"

#include <algorithm>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

// Vulkan create-info structs are designed for partial designated initialization.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

PipelineLayout::PipelineLayout(
    DeviceContext& context,
    std::span<const DescriptorSetLayoutView> set_layouts,
    std::span<const VkPushConstantRange> push_constants)
{
    std::vector<VkDescriptorSetLayout> handles;
    handles.reserve(set_layouts.size());
    description_.sets.reserve(set_layouts.size());
    for (const DescriptorSetLayoutView& set : set_layouts)
    {
        ErrorHandling::Ensure(
            set.handle != VK_NULL_HANDLE && set.description != nullptr,
            "PipelineLayout requires a handle and description for every descriptor set");
        for (const VkDescriptorSetLayoutBinding& binding : set.description->bindings)
        {
            ErrorHandling::Ensure(
                binding.pImmutableSamplers == nullptr,
                "PipelineLayout cannot retain descriptor layout bindings with immutable samplers");
        }
        handles.push_back(set.handle);
        description_.sets.push_back(*set.description);
    }
    description_.push_constants.assign(push_constants.begin(), push_constants.end());
    layout_ = VkObject<VkPipelineLayout>{
        context.GetDevice(),
        Vulkan::CreatePipelineLayout(
            context.GetDevice(),
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = static_cast<u32>(handles.size()),
                .pSetLayouts = handles.data(),
                .pushConstantRangeCount = static_cast<u32>(description_.push_constants.size()),
                .pPushConstantRanges = description_.push_constants.data(),
            })};
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
        const auto binding = std::ranges::find(bindings, descriptor.binding, &VkDescriptorSetLayoutBinding::binding);
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
            const VkShaderStageFlags stage = VkShaderStageFlags{1} << stage_bit;
            if (!(reflected.stages & stage)) continue;
            const auto range = std::ranges::find_if(
                description_.push_constants,
                [&](const VkPushConstantRange& value)
                {
                    const u64 declared_end = static_cast<u64>(value.offset) + value.size;
                    const u64 reflected_end = reflected.offset + required_size;
                    return value.offset <= reflected.offset && declared_end >= reflected_end &&
                           (value.stageFlags & stage) != 0;
                });
            ErrorHandling::Ensure(
                range != description_.push_constants.end(),
                "Push-constant layout does not cover '{}' (offset {}, size {}) for stage {}",
                reflected.name,
                reflected.offset,
                required_size,
                stage);
        }
    }
    return program;
}

}  // namespace klvk
