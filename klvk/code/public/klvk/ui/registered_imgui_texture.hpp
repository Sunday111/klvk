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

    [[nodiscard]] ImTextureID GetId() const noexcept
    {
        const VkDescriptorSet descriptor = static_cast<VkDescriptorSet>(descriptor_);
        return reinterpret_cast<ImTextureID>(descriptor);
    }

private:
    vk::UniqueSampler sampler_;
    vk::DescriptorSet descriptor_ = nullptr;
};

}  // namespace klvk
