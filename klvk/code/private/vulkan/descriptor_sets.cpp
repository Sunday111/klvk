#include "klvk/vulkan/descriptor_sets.hpp"

#include <algorithm>
#include <array>
#include <span>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

DescriptorSets::Builder&
DescriptorSets::Builder::Binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages, u32 count)
{
    bindings_.push_back({.binding = binding, .descriptorType = type, .descriptorCount = count, .stageFlags = stages});
    return *this;
}

DescriptorSets DescriptorSets::Builder::Build(u32 set_count)
{
    ErrorHandling::Ensure(!bindings_.empty(), "DescriptorSets::Builder: no bindings were added");
    ErrorHandling::Ensure(set_count != 0, "DescriptorSets::Builder: set_count must be non-zero");

    DeviceContext& context = *context_;
    const VkDevice device = context.GetDevice();

    VkObject<VkDescriptorSetLayout> layout{
        device,
        Vulkan::CreateDescriptorSetLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<u32>(bindings_.size()),
                .pBindings = bindings_.data(),
            })};

    // The pool must hold set_count copies of every binding, aggregated per type.
    std::vector<VkDescriptorPoolSize> pool_sizes;
    for (const VkDescriptorSetLayoutBinding& b : bindings_)
    {
        const u32 needed = b.descriptorCount * set_count;
        auto it = std::find_if(
            pool_sizes.begin(),
            pool_sizes.end(),
            [&](const VkDescriptorPoolSize& size) { return size.type == b.descriptorType; });
        if (it == pool_sizes.end())
        {
            pool_sizes.push_back({.type = b.descriptorType, .descriptorCount = needed});
        }
        else
        {
            it->descriptorCount += needed;
        }
    }

    VkObject<VkDescriptorPool> pool{
        device,
        Vulkan::CreateDescriptorPool(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = set_count,
                .poolSizeCount = static_cast<u32>(pool_sizes.size()),
                .pPoolSizes = pool_sizes.data(),
            })};

    const std::vector<VkDescriptorSetLayout> layouts(set_count, layout.GetHandle());
    std::vector<VkDescriptorSet> sets = Vulkan::AllocateDescriptorSets(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool.GetHandle(),
            .descriptorSetCount = set_count,
            .pSetLayouts = layouts.data(),
        });

    return DescriptorSets{context, std::move(layout), std::move(pool), std::move(sets), std::move(bindings_)};
}

DescriptorSets::DescriptorSets(
    DeviceContext& context,
    VkObject<VkDescriptorSetLayout> layout,
    VkObject<VkDescriptorPool> pool,
    std::vector<VkDescriptorSet> sets,
    std::vector<VkDescriptorSetLayoutBinding> bindings)
    : context_(&context),
      layout_(std::move(layout)),
      pool_(std::move(pool)),
      sets_(std::move(sets)),
      bindings_(std::move(bindings))
{
}

VkDescriptorType DescriptorSets::TypeOfBinding(u32 binding) const
{
    const auto it = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [&](const VkDescriptorSetLayoutBinding& b) { return b.binding == binding; });
    ErrorHandling::Ensure(it != bindings_.end(), "DescriptorSets: unknown binding {}", binding);
    return it->descriptorType;
}

void DescriptorSets::WriteBuffer(
    size_t set_index,
    u32 binding,
    VkBuffer buffer,
    VkDeviceSize range,
    VkDeviceSize offset)
{
    const std::array buffer_info{VkDescriptorBufferInfo{.buffer = buffer, .offset = offset, .range = range}};
    const std::array write{VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = sets_.at(set_index),
        .dstBinding = binding,
        .descriptorCount = buffer_info.size(),
        .descriptorType = TypeOfBinding(binding),
        .pBufferInfo = buffer_info.data(),
    }};
    Vulkan::UpdateDescriptorSets(context_->GetDevice(), std::span{write});
}

void DescriptorSets::WriteImage(
    size_t set_index,
    u32 binding,
    VkImageView view,
    VkSampler sampler,
    VkImageLayout layout)
{
    const std::array image_info{VkDescriptorImageInfo{.sampler = sampler, .imageView = view, .imageLayout = layout}};
    const std::array write{VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = sets_.at(set_index),
        .dstBinding = binding,
        .descriptorCount = image_info.size(),
        .descriptorType = TypeOfBinding(binding),
        .pImageInfo = image_info.data(),
    }};
    Vulkan::UpdateDescriptorSets(context_->GetDevice(), std::span{write});
}

}  // namespace klvk
