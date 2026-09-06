#include "klvk/vulkan/texture.hpp"

#include <vk_mem_alloc.h>

#include "klvk/error_handling.hpp"
#include "klvk/image/image_decoder.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

std::unique_ptr<Texture> Texture::CreateR8(DeviceContext& context, edt::Vec2<u32> size, std::span<const u8> pixels)
{
    return Create(context, size, pixels, vk::Format::eR8Unorm, 1);
}

std::unique_ptr<Texture> Texture::CreateRgba8(DeviceContext& context, edt::Vec2<u32> size, std::span<const u8> pixels)
{
    return Create(context, size, pixels, vk::Format::eR8G8B8A8Unorm, 4);
}

std::unique_ptr<Texture> Texture::CreateFromEncoded(DeviceContext& context, std::span<const u8> encoded)
{
    const std::optional<DecodedImage> image = DecodeImage(encoded);
    if (!image.has_value()) return nullptr;
    return CreateRgba8(context, image->size, image->pixels);
}

std::unique_ptr<Texture> Texture::CreateEmptyR8(DeviceContext& context, edt::Vec2<u32> size)
{
    // Zeroed rather than left undefined: the padding between packed regions is
    // sampled at their edges, and undefined memory there would show as fringing.
    const std::vector<u8> zeros(static_cast<size_t>(size.x()) * size.y(), 0);
    return Create(context, size, zeros, vk::Format::eR8Unorm, 1);
}

void Texture::RecordRegionUpdates(
    vk::CommandBuffer command_buffer,
    vk::Buffer staging,
    std::span<const RegionUpdate> regions)
{
    if (regions.empty()) return;

    const vk::ImageSubresourceRange subresource_range =
        vk::ImageSubresourceRange{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLevelCount(1).setLayerCount(1);
    std::array barriers{vk::ImageMemoryBarrier2{}
                            .setSrcStageMask(vk::PipelineStageFlagBits2::eFragmentShader)
                            .setSrcAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                            .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)
                            .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                            .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                            .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                            .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                            .setImage(image_)
                            .setSubresourceRange(subresource_range)};
    const vk::DependencyInfo dependency = vk::DependencyInfo{}.setImageMemoryBarriers(barriers);
    command_buffer.pipelineBarrier2(dependency);

    std::vector<vk::BufferImageCopy> copies;
    copies.reserve(regions.size());
    for (const RegionUpdate& region : regions)
    {
        const vk::ImageSubresourceLayers image_subresource =
            vk::ImageSubresourceLayers{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLayerCount(1);
        copies.push_back(
            vk::BufferImageCopy{}
                .setBufferOffset(region.buffer_offset)
                .setImageSubresource(image_subresource)
                .setImageOffset(
                    vk::Offset3D{static_cast<i32>(region.offset.x()), static_cast<i32>(region.offset.y()), 0})
                .setImageExtent(vk::Extent3D{region.size.x(), region.size.y(), 1}));
    }
    command_buffer.copyBufferToImage(staging, image_, vk::ImageLayout::eTransferDstOptimal, copies);

    // Back to being sampled, and not before the copies have landed.
    barriers[0].srcStageMask = vk::PipelineStageFlagBits2::eCopy;
    barriers[0].srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barriers[0].dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    barriers[0].dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
    barriers[0].oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barriers[0].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    command_buffer.pipelineBarrier2(dependency);
}

std::unique_ptr<Texture> Texture::Create(
    DeviceContext& context,
    edt::Vec2<u32> size,
    std::span<const u8> pixels,
    vk::Format format,
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

    const vk::ImageCreateInfo image_info =
        vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setFormat(format)
            .setExtent(vk::Extent3D{size.x(), size.y(), 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    const VkImageCreateInfo& raw_image_info = image_info;
    VkImage raw_image = nullptr;
    VulkanCheck(
        static_cast<vk::Result>(vmaCreateImage(
            context.GetAllocator(),
            &raw_image_info,
            &allocation_info,
            &raw_image,
            &texture->allocation_,
            nullptr)));
    texture->image_ = raw_image;

    GpuBuffer staging(context, vk::BufferUsageFlagBits::eTransferSrc, pixels.size(), true);
    staging.Write(std::as_bytes(pixels));

    context.SubmitOneTimeCommands(
        [&](vk::CommandBuffer command_buffer)
        {
            const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                    .setLevelCount(1)
                                                                    .setLayerCount(1);
            std::array barriers{vk::ImageMemoryBarrier2{}
                                    .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                                    .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                    .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)
                                    .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                    .setOldLayout(vk::ImageLayout::eUndefined)
                                    .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                                    .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                                    .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                                    .setImage(texture->image_)
                                    .setSubresourceRange(subresource_range)};
            // Reused below: the barrier is rewritten in place and re-issued through dependency.
            const vk::DependencyInfo dependency = vk::DependencyInfo{}.setImageMemoryBarriers(barriers);
            command_buffer.pipelineBarrier2(dependency);

            const vk::ImageSubresourceLayers image_subresource =
                vk::ImageSubresourceLayers{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLayerCount(1);
            const std::array regions{vk::BufferImageCopy{}
                                         .setImageSubresource(image_subresource)
                                         .setImageExtent(vk::Extent3D{size.x(), size.y(), 1})};
            command_buffer
                .copyBufferToImage(staging.GetHandle(), texture->image_, vk::ImageLayout::eTransferDstOptimal, regions);

            vk::ImageMemoryBarrier2& barrier = barriers.front();
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            command_buffer.pipelineBarrier2(dependency);
        });

    const vk::ImageSubresourceRange view_subresource_range =
        vk::ImageSubresourceRange{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLevelCount(1).setLayerCount(1);
    const vk::ImageViewCreateInfo view_info = vk::ImageViewCreateInfo{}
                                                  .setImage(texture->image_)
                                                  .setViewType(vk::ImageViewType::e2D)
                                                  .setFormat(format)
                                                  .setSubresourceRange(view_subresource_range);
    texture->view_ = context.GetDevice().createImageViewUnique(view_info);

    // Same filtering verlet uses for the circle mask: nearest when minified, linear when magnified.
    const vk::SamplerCreateInfo sampler_info = vk::SamplerCreateInfo{}
                                                   .setMagFilter(vk::Filter::eLinear)
                                                   .setMinFilter(vk::Filter::eNearest)
                                                   .setMipmapMode(vk::SamplerMipmapMode::eNearest)
                                                   .setAddressModeU(vk::SamplerAddressMode::eRepeat)
                                                   .setAddressModeV(vk::SamplerAddressMode::eRepeat)
                                                   .setAddressModeW(vk::SamplerAddressMode::eRepeat)
                                                   .setBorderColor(vk::BorderColor::eIntOpaqueBlack);
    texture->sampler_ = context.GetDevice().createSamplerUnique(sampler_info);

    return texture;
}

Texture::~Texture()
{
    if (!context_) return;
    sampler_.reset();
    view_.reset();
    if (image_)
    {
        const VkImage raw_image = image_;
        vmaDestroyImage(context_->GetAllocator(), raw_image, allocation_);
    }
}

}  // namespace klvk
