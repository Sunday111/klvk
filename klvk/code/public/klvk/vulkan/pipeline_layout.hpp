#pragma once

#include <span>
#include <vector>

#include "klvk/shader/shader_interface.hpp"
#include "klvk/shader/shader_stages.hpp"
#include "klvk/vulkan/vk_object.hpp"

namespace klvk
{

class DeviceContext;

struct DescriptorSetLayoutDescription
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    bool operator==(const DescriptorSetLayoutDescription&) const = default;
};

struct PipelineLayoutDescription
{
    std::vector<DescriptorSetLayoutDescription> sets;
    std::vector<VkPushConstantRange> push_constants;
};

struct DescriptorSetLayoutView
{
    VkDescriptorSetLayout handle = VK_NULL_HANDLE;
    const DescriptorSetLayoutDescription* description = nullptr;
};

class PipelineLayout
{
public:
    PipelineLayout() = default;
    PipelineLayout(
        DeviceContext& context,
        std::span<const DescriptorSetLayoutView> set_layouts,
        std::span<const VkPushConstantRange> push_constants = {});
    PipelineLayout(const PipelineLayout&) = delete;
    PipelineLayout(PipelineLayout&&) noexcept = default;
    PipelineLayout& operator=(const PipelineLayout&) = delete;
    PipelineLayout& operator=(PipelineLayout&&) noexcept = default;

    [[nodiscard]] VkPipelineLayout GetHandle() const noexcept { return layout_.GetHandle(); }
    [[nodiscard]] const PipelineLayoutDescription& GetDescription() const noexcept { return description_; }

    [[nodiscard]] ShaderProgramInterface Validate(const ShaderStages& stages) const;

private:
    VkObject<VkPipelineLayout> layout_;
    PipelineLayoutDescription description_;
};

}  // namespace klvk
