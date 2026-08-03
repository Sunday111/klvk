#include "klvk/vulkan/texture.hpp"

#include <stb_image.h>
#include <vk_mem_alloc.h>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
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

std::unique_ptr<Texture> Texture::CreateR8(DeviceContext& context, edt::Vec2<u32> size, std::span<const u8> pixels)
{
    return Create(context, size, pixels, VK_FORMAT_R8_UNORM, 1);
}

std::unique_ptr<Texture> Texture::CreateRgba8(DeviceContext& context, edt::Vec2<u32> size, std::span<const u8> pixels)
{
    return Create(context, size, pixels, VK_FORMAT_R8G8B8A8_UNORM, 4);
}

std::unique_ptr<Texture> Texture::CreateFromEncoded(DeviceContext& context, std::span<const u8> encoded)
{
    // Four channels whatever the file held, so the sampler reads one layout and
    // nothing downstream branches on what arrived.
    constexpr int kChannels = 4;

    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    u8* pixels = stbi_load_from_memory(
        encoded.data(),
        static_cast<int>(encoded.size()),
        &width,
        &height,
        &channels_in_file,
        kChannels);
    if (pixels == nullptr) return nullptr;

    const auto size = edt::Vec2<u32>{static_cast<u32>(width), static_cast<u32>(height)};
    const auto count = static_cast<size_t>(width) * static_cast<size_t>(height) * kChannels;
    std::unique_ptr<Texture> texture = CreateRgba8(context, size, std::span{pixels, count});
    stbi_image_free(pixels);
    return texture;
}

std::unique_ptr<Texture> Texture::CreateEmptyR8(DeviceContext& context, edt::Vec2<u32> size)
{
    // Zeroed rather than left undefined: the padding between packed regions is
    // sampled at their edges, and undefined memory there would show as fringing.
    const std::vector<u8> zeros(static_cast<size_t>(size.x()) * size.y(), 0);
    return Create(context, size, zeros, VK_FORMAT_R8_UNORM, 1);
}

void Texture::RecordRegionUpdates(
    VkCommandBuffer command_buffer,
    VkBuffer staging,
    std::span<const RegionUpdate> regions)
{
    if (regions.empty()) return;

    std::array barriers{VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    }};
    VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = barriers.size(),
        .pImageMemoryBarriers = barriers.data(),
    };
    Vulkan::CmdPipelineBarrier2(command_buffer, dependency);

    std::vector<VkBufferImageCopy> copies;
    copies.reserve(regions.size());
    for (const RegionUpdate& region : regions)
    {
        copies.push_back({
            .bufferOffset = region.buffer_offset,
            .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
            .imageOffset = {.x = static_cast<i32>(region.offset.x()), .y = static_cast<i32>(region.offset.y())},
            .imageExtent = {.width = region.size.x(), .height = region.size.y(), .depth = 1},
        });
    }
    Vulkan::CmdCopyBufferToImage(command_buffer, staging, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copies);

    // Back to being sampled, and not before the copies have landed.
    barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
}

std::unique_ptr<Texture> Texture::Create(
    DeviceContext& context,
    edt::Vec2<u32> size,
    std::span<const u8> pixels,
    VkFormat format,
    u32 bytes_per_pixel)
{
    ErrorHandling::Ensure(
        pixels.size() == static_cast<size_t>(size.x()) * size.y() * bytes_per_pixel,
        "Pixel count {} does not match texture size {}x{} at {} bytes per pixel",
        pixels.size(),
        size.x(),
        size.y(),
        bytes_per_pixel);

    auto texture = std::unique_ptr<Texture>(new Texture());
    texture->context_ = &context;
    texture->size_ = size;

    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = size.x(), .height = size.y(), .depth = 1},
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
            std::array barriers{VkImageMemoryBarrier2{
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
            }};
            // Reused below: the barrier is rewritten in place and re-issued through dependency.
            VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = barriers.size(),
                .pImageMemoryBarriers = barriers.data(),
            };
            Vulkan::CmdPipelineBarrier2NE(command_buffer, dependency);

            const std::array regions{VkBufferImageCopy{
                .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                .imageExtent = {.width = size.x(), .height = size.y(), .depth = 1},
            }};
            Vulkan::CmdCopyBufferToImageNE(
                command_buffer,
                staging.GetHandle(),
                texture->image_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                regions);

            VkImageMemoryBarrier2& barrier = barriers.front();
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
