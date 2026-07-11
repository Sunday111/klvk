#include "klvk/vulkan/texture.hpp"

#include <vk_mem_alloc.h>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

std::unique_ptr<Texture>
Texture::CreateR8(DeviceContext& context, edt::Vec2<uint32_t> size, std::span<const uint8_t> pixels)
{
    ErrorHandling::Ensure(
        pixels.size() == static_cast<size_t>(size.x()) * size.y(),
        "Pixel count {} does not match texture size {}x{}",
        pixels.size(),
        size.x(),
        size.y());

    auto texture = std::unique_ptr<Texture>(new Texture());
    texture->context_ = &context;
    texture->size_ = size;

    constexpr VkFormat format = VK_FORMAT_R8_UNORM;
    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {size.x(), size.y(), 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO};
    CheckVkResult(
        vmaCreateImage(
            context.GetAllocator(),
            &image_info,
            &allocation_info,
            &texture->image_,
            &texture->allocation_,
            nullptr),
        "vmaCreateImage");

    GpuBuffer staging(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels.size(), true);
    staging.Write(std::as_bytes(pixels));

    context.SubmitOneTimeCommands(
        [&](VkCommandBuffer command_buffer)
        {
            VkImageMemoryBarrier2 barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = texture->image_,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
            };
            VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier,
            };
            Vulkan::CmdPipelineBarrier2NE(command_buffer, dependency);

            const VkBufferImageCopy region{
                .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                .imageExtent = {size.x(), size.y(), 1},
            };
            Vulkan::CmdCopyBufferToImageNE(
                command_buffer,
                staging.GetHandle(),
                texture->image_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                std::span{&region, 1});

            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Vulkan::CmdPipelineBarrier2NE(command_buffer, dependency);
        });

    const VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    texture->view_ = Vulkan::CreateImageView(context.GetDevice(), view_info);

    // Same filtering verlet uses for the circle mask: nearest when minified, linear when magnified.
    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    };
    texture->sampler_ = Vulkan::CreateSampler(context.GetDevice(), sampler_info);

    return texture;
}

Texture::~Texture()
{
    if (!context_) return;
    if (sampler_) Vulkan::DestroySamplerNE(context_->GetDevice(), sampler_);
    if (view_) Vulkan::DestroyImageViewNE(context_->GetDevice(), view_);
    if (image_) vmaDestroyImage(context_->GetAllocator(), image_, allocation_);
}

}  // namespace klvk
