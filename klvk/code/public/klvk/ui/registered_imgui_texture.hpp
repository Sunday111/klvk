#pragma once

#include <imgui.h>

#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class DeviceContext;
class RegisteredImGuiTexture
{
public:
    [[nodiscard]] static vk::SamplerCreateInfo DefaultSamplerCreateInfo() noexcept;

    RegisteredImGuiTexture(
        DeviceContext& context,
        vk::ImageView image_view,
        const vk::SamplerCreateInfo& sampler_info = DefaultSamplerCreateInfo());
    RegisteredImGuiTexture(const RegisteredImGuiTexture&) = delete;
    RegisteredImGuiTexture(RegisteredImGuiTexture&&) = delete;
    ~RegisteredImGuiTexture();

    void Draw(ImVec2 display_size, ImVec2 uv0 = {}, ImVec2 uv1 = {1.f, 1.f});

private:
    static void SetSampler(const ImDrawList*, const ImDrawCmd* command);

    vk::UniqueSampler sampler_;
    vk::UniqueDescriptorSetLayout sampler_descriptor_set_layout_;
    vk::UniqueDescriptorPool sampler_descriptor_pool_;
    vk::DescriptorSet sampler_descriptor_ = nullptr;
    vk::DescriptorSet image_descriptor_ = nullptr;
};

}  // namespace klvk
