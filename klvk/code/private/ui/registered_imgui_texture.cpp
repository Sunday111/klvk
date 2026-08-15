#include "klvk/ui/registered_imgui_texture.hpp"

#include <backends/imgui_impl_vulkan.h>

#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

vk::SamplerCreateInfo RegisteredImGuiTexture::DefaultSamplerCreateInfo() noexcept
{
    return vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setBorderColor(vk::BorderColor::eIntOpaqueBlack);
}

RegisteredImGuiTexture::RegisteredImGuiTexture(
    DeviceContext& context,
    vk::ImageView image_view,
    const vk::SamplerCreateInfo& sampler_info)
{
    sampler_ = VulkanValue(context.GetDevice().createSamplerUnique(sampler_info), "vkCreateSampler");
    const VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
        static_cast<VkSampler>(sampler_.get()),
        static_cast<VkImageView>(image_view),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    descriptor_ = vk::DescriptorSet{descriptor};
}

RegisteredImGuiTexture::~RegisteredImGuiTexture()
{
    if (descriptor_ != nullptr) ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(descriptor_));
}

}  // namespace klvk
