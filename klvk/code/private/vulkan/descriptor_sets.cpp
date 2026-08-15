#include "klvk/vulkan/descriptor_sets.hpp"

#include <algorithm>
#include <array>
#include <span>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"

namespace klvk
{

DescriptorSets::Builder&
DescriptorSets::Builder::Binding(u32 binding, vk::DescriptorType type, vk::ShaderStageFlags stages, u32 count)
{
    bindings_.push_back(vk::DescriptorSetLayoutBinding{binding, type, count, stages});
    return *this;
}

DescriptorSets DescriptorSets::Builder::Build(u32 set_count)
{
    ErrorHandling::Ensure(!bindings_.empty(), "DescriptorSets::Builder: no bindings were added");
    ErrorHandling::Ensure(set_count != 0, "DescriptorSets::Builder: set_count must be non-zero");

    DeviceContext& context = *context_;
    const vk::Device device = context.GetDevice();

    vk::UniqueDescriptorSetLayout layout =
        device.createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings_));

    // The pool must hold set_count copies of every binding, aggregated per type.
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    for (const vk::DescriptorSetLayoutBinding& b : bindings_)
    {
        const u32 needed = b.descriptorCount * set_count;
        auto it = std::ranges::find_if(
            pool_sizes,
            [&](const vk::DescriptorPoolSize& size) { return size.type == b.descriptorType; });
        if (it == pool_sizes.end())
        {
            pool_sizes.push_back(vk::DescriptorPoolSize{b.descriptorType, needed});
        }
        else
        {
            it->descriptorCount += needed;
        }
    }

    vk::UniqueDescriptorPool pool = device.createDescriptorPoolUnique(
        vk::DescriptorPoolCreateInfo{}.setMaxSets(set_count).setPoolSizes(pool_sizes));

    const std::vector<vk::DescriptorSetLayout> layouts(set_count, layout.get());
    std::vector<vk::DescriptorSet> sets = device.allocateDescriptorSets(
        vk::DescriptorSetAllocateInfo{}.setDescriptorPool(pool.get()).setSetLayouts(layouts));

    return DescriptorSets{
        context,
        std::move(layout),
        std::move(pool),
        std::move(sets),
        DescriptorSetLayoutDescription{.bindings = std::move(bindings_)}};
}

DescriptorSets::DescriptorSets(
    DeviceContext& context,
    vk::UniqueDescriptorSetLayout layout,
    vk::UniqueDescriptorPool pool,
    std::vector<vk::DescriptorSet> sets,
    DescriptorSetLayoutDescription layout_description)
    : context_(&context),
      layout_(std::move(layout)),
      pool_(std::move(pool)),
      sets_(std::move(sets)),
      layout_description_(std::move(layout_description))
{
}

vk::DescriptorType DescriptorSets::TypeOfBinding(u32 binding) const
{
    const auto it = std::ranges::find_if(
        layout_description_.bindings,
        [&](const vk::DescriptorSetLayoutBinding& b) { return b.binding == binding; });
    ErrorHandling::Ensure(it != layout_description_.bindings.end(), "DescriptorSets: unknown binding {}", binding);
    return it->descriptorType;
}

void DescriptorSets::ValidateAgainst(const ShaderProgramInterface& program, u32 set_index) const
{
    std::vector<const ShaderDescriptorBinding*> reflected;
    for (const auto& descriptor : program.descriptors)
    {
        if (descriptor.set == set_index) reflected.push_back(&descriptor);
    }
    for (const ShaderDescriptorBinding* descriptor : reflected)
    {
        ErrorHandling::Ensure(
            !descriptor->unbounded,
            "Unbounded descriptor set {} binding {} requires variable descriptor flags, which are not modeled",
            descriptor->set,
            descriptor->binding);
        const auto binding = std::ranges::find(
            layout_description_.bindings,
            descriptor->binding,
            &vk::DescriptorSetLayoutBinding::binding);
        ErrorHandling::Ensure(
            binding != layout_description_.bindings.end() && binding->descriptorType == descriptor->type &&
                binding->descriptorCount >= descriptor->count &&
                (binding->stageFlags & descriptor->stages) == descriptor->stages,
            "Descriptor set {} binding {} does not match shader reflection",
            set_index,
            descriptor->binding);
    }
}

void DescriptorSets::WriteBuffer(
    size_t set_index,
    u32 binding,
    vk::Buffer buffer,
    vk::DeviceSize range,
    vk::DeviceSize offset)
{
    const std::array buffer_info{vk::DescriptorBufferInfo{buffer, offset, range}};
    const std::array write{vk::WriteDescriptorSet{}
                               .setDstSet(sets_.at(set_index))
                               .setDstBinding(binding)
                               .setDescriptorType(TypeOfBinding(binding))
                               .setBufferInfo(buffer_info)};
    context_->GetDevice().updateDescriptorSets(write, {});
}

void DescriptorSets::WriteImage(
    size_t set_index,
    u32 binding,
    vk::ImageView view,
    vk::Sampler sampler,
    vk::ImageLayout layout,
    u32 array_element)
{
    const std::array image_info{vk::DescriptorImageInfo{sampler, view, layout}};
    const std::array write{vk::WriteDescriptorSet{}
                               .setDstSet(sets_.at(set_index))
                               .setDstBinding(binding)
                               .setDstArrayElement(array_element)
                               .setDescriptorType(TypeOfBinding(binding))
                               .setImageInfo(image_info)};
    context_->GetDevice().updateDescriptorSets(write, {});
}

}  // namespace klvk
