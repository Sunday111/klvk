#include "klvk/ui/registered_imgui_texture.hpp"

#include <backends/imgui_impl_vulkan.h>

#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

namespace klvk
{

VkSamplerCreateInfo RegisteredImGuiTexture::DefaultSamplerCreateInfo() noexcept
{
    return {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    };
}

RegisteredImGuiTexture::RegisteredImGuiTexture(
    DeviceContext& context,
    VkImageView image_view,
    const VkSamplerCreateInfo& sampler_info)
    : context_{&context}
{
    sampler_ = Vulkan::CreateSampler(context_->GetDevice(), sampler_info);
    descriptor_ = ImGui_ImplVulkan_AddTexture(sampler_, image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

RegisteredImGuiTexture::~RegisteredImGuiTexture()
{
    if (descriptor_ != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(descriptor_);
    if (sampler_ != VK_NULL_HANDLE) Vulkan::DestroySamplerNE(context_->GetDevice(), sampler_);
}

}  // namespace klvk
