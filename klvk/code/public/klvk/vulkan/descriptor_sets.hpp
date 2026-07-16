#pragma once

#include <vector>

#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vk_object.hpp"

namespace klvk
{

class DeviceContext;

// One descriptor set layout replicated across N identical sets - the shape every
// example hand-rolled as layout -> pool -> allocate -> write (~40-60 lines). The
// layout and pool are owned as VkObjects, so a DescriptorSets member needs no
// teardown. Build it once from the bindings, then fill each set with
// WriteBuffer/WriteImage.
//
// N is usually 1 (a single set) or Application::kFramesInFlight (one set per
// frame in flight, each pointing at that frame's buffer).
class DescriptorSets
{
public:
    class Builder
    {
    public:
        explicit Builder(DeviceContext& context) : context_(&context) {}

        // Adds a binding to the shared layout. Repeat for multiple bindings.
        Builder& Binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages, u32 count = 1);

        // Creates the layout and a pool, then allocates set_count identical sets.
        [[nodiscard]] DescriptorSets Build(u32 set_count = 1);

    private:
        DeviceContext* context_ = nullptr;
        std::vector<VkDescriptorSetLayoutBinding> bindings_;
    };

    DescriptorSets() = default;

    [[nodiscard]] VkDescriptorSetLayout GetLayout() const noexcept { return layout_.GetHandle(); }
    [[nodiscard]] VkDescriptorSet Get(size_t set_index) const { return sets_.at(set_index); }
    [[nodiscard]] size_t Count() const noexcept { return sets_.size(); }

    // Points a binding of the given set at a buffer. The descriptor type comes
    // from the binding declared in the builder (uniform or storage buffer).
    void WriteBuffer(size_t set_index, u32 binding, VkBuffer buffer, VkDeviceSize range, VkDeviceSize offset = 0);

    // Points a combined-image-sampler binding of the given set at an image view.
    void WriteImage(
        size_t set_index,
        u32 binding,
        VkImageView view,
        VkSampler sampler,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

private:
    DescriptorSets(
        DeviceContext& context,
        VkObject<VkDescriptorSetLayout> layout,
        VkObject<VkDescriptorPool> pool,
        std::vector<VkDescriptorSet> sets,
        std::vector<VkDescriptorSetLayoutBinding> bindings);

    [[nodiscard]] VkDescriptorType TypeOfBinding(u32 binding) const;

    DeviceContext* context_ = nullptr;
    VkObject<VkDescriptorSetLayout> layout_;
    VkObject<VkDescriptorPool> pool_;
    std::vector<VkDescriptorSet> sets_;
    std::vector<VkDescriptorSetLayoutBinding> bindings_;
};

}  // namespace klvk
