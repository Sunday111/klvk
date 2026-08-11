#pragma once

#include <imgui.h>

#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class DeviceContext;
class RegisteredImGuiTexture
{
public:
    [[nodiscard]] static VkSamplerCreateInfo DefaultSamplerCreateInfo() noexcept;

    RegisteredImGuiTexture(
        DeviceContext& context,
        VkImageView image_view,
        const VkSamplerCreateInfo& sampler_info = DefaultSamplerCreateInfo());
    RegisteredImGuiTexture(const RegisteredImGuiTexture&) = delete;
    RegisteredImGuiTexture(RegisteredImGuiTexture&&) = delete;
    ~RegisteredImGuiTexture();

    [[nodiscard]] ImTextureID GetId() const noexcept { return reinterpret_cast<ImTextureID>(descriptor_); }

private:
    DeviceContext* context_ = nullptr;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_ = VK_NULL_HANDLE;
};

}  // namespace klvk
