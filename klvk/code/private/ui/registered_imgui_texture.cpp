#include "klvk/ui/registered_imgui_texture.hpp"

#include <backends/imgui_impl_vulkan.h>

#include <array>

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
    const vk::Device device = context.GetDevice();
    sampler_ = device.createSamplerUnique(sampler_info);

    const std::array bindings{vk::DescriptorSetLayoutBinding{}
                                  .setBinding(0)
                                  .setDescriptorType(vk::DescriptorType::eSampler)
                                  .setDescriptorCount(1)
                                  .setStageFlags(vk::ShaderStageFlagBits::eFragment)};
    sampler_descriptor_set_layout_ =
        device.createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings));

    const std::array pool_sizes{vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1}};
    sampler_descriptor_pool_ =
        device.createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo{}.setMaxSets(1).setPoolSizes(pool_sizes));

    const std::array layouts{sampler_descriptor_set_layout_.get()};
    sampler_descriptor_ = device
                              .allocateDescriptorSets(
                                  vk::DescriptorSetAllocateInfo{}
                                      .setDescriptorPool(sampler_descriptor_pool_.get())
                                      .setSetLayouts(layouts))
                              .front();
    const std::array image_info{vk::DescriptorImageInfo{}.setSampler(sampler_.get())};
    const std::array write{vk::WriteDescriptorSet{}
                               .setDstSet(sampler_descriptor_)
                               .setDstBinding(0)
                               .setDescriptorType(vk::DescriptorType::eSampler)
                               .setImageInfo(image_info)};
    device.updateDescriptorSets(write, {});

    image_descriptor_ = vk::DescriptorSet{
        ImGui_ImplVulkan_AddTexture(static_cast<VkImageView>(image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)};
}

RegisteredImGuiTexture::~RegisteredImGuiTexture()
{
    if (image_descriptor_ != nullptr)
    {
        ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(image_descriptor_));
    }
}

void RegisteredImGuiTexture::Draw(ImVec2 display_size, ImVec2 uv0, ImVec2 uv1)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddCallback(SetSampler, &sampler_descriptor_);
    const auto descriptor = static_cast<VkDescriptorSet>(image_descriptor_);
    ImGui::Image(reinterpret_cast<ImTextureID>(descriptor), display_size, uv0, uv1);
    draw_list->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);
}

void RegisteredImGuiTexture::SetSampler(const ImDrawList*, const ImDrawCmd* command)
{
    const auto* descriptor = static_cast<const VkDescriptorSet*>(command->UserCallbackData);
    const auto* render_state =
        static_cast<const ImGui_ImplVulkan_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBindDescriptorSets(
        render_state->CommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        render_state->PipelineLayout,
        1,
        1,
        descriptor,
        0,
        nullptr);
}

}  // namespace klvk
