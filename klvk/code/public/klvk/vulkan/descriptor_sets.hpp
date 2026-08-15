#pragma once

#include <vector>

#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_interface.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"
#include "klvk/vulkan/vulkan.hpp"

namespace klvk
{

class DeviceContext;

// One descriptor set layout replicated across N identical sets - the shape every
// example hand-rolled as layout -> pool -> allocate -> write (~40-60 lines). The
// layout and pool are uniquely owned, so a DescriptorSets member needs no
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
        Builder& Binding(u32 binding, vk::DescriptorType type, vk::ShaderStageFlags stages, u32 count = 1);

        // Creates the layout and a pool, then allocates set_count identical sets.
        [[nodiscard]] DescriptorSets Build(u32 set_count = 1);

    private:
        DeviceContext* context_ = nullptr;
        std::vector<vk::DescriptorSetLayoutBinding> bindings_;
    };

    DescriptorSets() = default;

    [[nodiscard]] vk::DescriptorSetLayout GetLayout() const noexcept { return layout_.get(); }
    [[nodiscard]] const DescriptorSetLayoutDescription& GetLayoutDescription() const noexcept
    {
        return layout_description_;
    }
    [[nodiscard]] DescriptorSetLayoutView GetLayoutView() const noexcept
    {
        return {.handle = GetLayout(), .description = &layout_description_};
    }
    [[nodiscard]] vk::DescriptorSet Get(size_t set_index) const { return sets_.at(set_index); }
    [[nodiscard]] size_t Count() const noexcept { return sets_.size(); }
    void ValidateAgainst(const ShaderProgramInterface& program, u32 set_index) const;

    // Points a binding of the given set at a buffer. The descriptor type comes
    // from the binding declared in the builder (uniform or storage buffer).
    void WriteBuffer(size_t set_index, u32 binding, vk::Buffer buffer, vk::DeviceSize range, vk::DeviceSize offset = 0);

    // Points a combined-image-sampler binding of the given set at an image view.
    // `array_element` selects the slot when the binding was declared with a count
    // above one. Without it an array binding could only ever hold its first image.
    void WriteImage(
        size_t set_index,
        u32 binding,
        vk::ImageView view,
        vk::Sampler sampler,
        vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        u32 array_element = 0);

private:
    DescriptorSets(
        DeviceContext& context,
        vk::UniqueDescriptorSetLayout layout,
        vk::UniqueDescriptorPool pool,
        std::vector<vk::DescriptorSet> sets,
        DescriptorSetLayoutDescription layout_description);

    [[nodiscard]] vk::DescriptorType TypeOfBinding(u32 binding) const;

    DeviceContext* context_ = nullptr;
    vk::UniqueDescriptorSetLayout layout_;
    vk::UniqueDescriptorPool pool_;
    std::vector<vk::DescriptorSet> sets_;
    DescriptorSetLayoutDescription layout_description_;
};

}  // namespace klvk
