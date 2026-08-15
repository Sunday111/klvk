#include "klvk/vulkan/offscreen_render_target.hpp"

#include <vk_mem_alloc.h>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/depth_stencil_format.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

namespace
{

struct AllocatedImage
{
    vk::Image image = nullptr;
    VmaAllocation allocation = nullptr;
    vk::UniqueImageView view;
};

AllocatedImage CreateImage(
    DeviceContext& context,
    vk::Extent2D extent,
    vk::Format format,
    vk::ImageUsageFlags usage,
    vk::ImageAspectFlags aspect)
{
    const auto image_info = vk::ImageCreateInfo{}
                                .setImageType(vk::ImageType::e2D)
                                .setFormat(format)
                                .setExtent(vk::Extent3D{extent.width, extent.height, 1})
                                .setMipLevels(1)
                                .setArrayLayers(1)
                                .setSamples(vk::SampleCountFlagBits::e1)
                                .setTiling(vk::ImageTiling::eOptimal)
                                .setUsage(usage)
                                .setSharingMode(vk::SharingMode::eExclusive)
                                .setInitialLayout(vk::ImageLayout::eUndefined);
    const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    const VkImageCreateInfo& raw_image_info = image_info;
    AllocatedImage result;
    VkImage image = VK_NULL_HANDLE;
    VulkanCheck(
        static_cast<vk::Result>(vmaCreateImage(
            context.GetAllocator(),
            &raw_image_info,
            &allocation_info,
            &image,
            &result.allocation,
            nullptr)));
    result.image = vk::Image{image};
    try
    {
        const auto range = vk::ImageSubresourceRange{}.setAspectMask(aspect).setLevelCount(1).setLayerCount(1);
        const auto view_info = vk::ImageViewCreateInfo{}
                                   .setImage(result.image)
                                   .setViewType(vk::ImageViewType::e2D)
                                   .setFormat(format)
                                   .setSubresourceRange(range);
        result.view = context.GetDevice().createImageViewUnique(view_info);
    }
    catch (...)
    {
        vmaDestroyImage(context.GetAllocator(), static_cast<VkImage>(result.image), result.allocation);
        throw;
    }
    return result;
}

}  // namespace

OffscreenRenderTarget::OffscreenRenderTarget(DeviceContext& context, edt::Vec2<u32> size, size_t image_count)
    : context_(&context),
      depth_stencil_format_(SelectDepthStencilFormat(context.GetPhysicalDevice())),
      extent_{size.x(), size.y()}
{
    ErrorHandling::Ensure(extent_.width != 0 && extent_.height != 0, "Offscreen render target size must be positive");
    ErrorHandling::Ensure(image_count != 0, "Offscreen render target requires at least one image");
    CreateImages(image_count);
}

OffscreenRenderTarget::~OffscreenRenderTarget()
{
    DestroyImages();
}

void OffscreenRenderTarget::CreateImages(size_t image_count)
{
    color_images_.reserve(image_count);
    color_allocations_.reserve(image_count);
    color_image_views_.reserve(image_count);
    depth_images_.reserve(image_count);
    depth_allocations_.reserve(image_count);
    depth_image_views_.reserve(image_count);
    try
    {
        for (size_t index = 0; index != image_count; ++index)
        {
            AllocatedImage color = CreateImage(
                *context_,
                extent_,
                kColorFormat,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
                vk::ImageAspectFlagBits::eColor);
            color_images_.push_back(color.image);
            color_allocations_.push_back(color.allocation);
            color_image_views_.push_back(std::move(color.view));

            AllocatedImage depth = CreateImage(
                *context_,
                extent_,
                depth_stencil_format_,
                vk::ImageUsageFlagBits::eDepthStencilAttachment,
                DepthStencilAspectMask(depth_stencil_format_));
            depth_images_.push_back(depth.image);
            depth_allocations_.push_back(depth.allocation);
            depth_image_views_.push_back(std::move(depth.view));
        }
    }
    catch (...)
    {
        DestroyImages();
        throw;
    }
}

void OffscreenRenderTarget::DestroyImages()
{
    color_image_views_.clear();
    depth_image_views_.clear();
    for (size_t index = 0; index != color_images_.size(); ++index)
    {
        vmaDestroyImage(
            context_->GetAllocator(),
            static_cast<VkImage>(color_images_[index]),
            color_allocations_[index]);
    }
    for (size_t index = 0; index != depth_images_.size(); ++index)
    {
        vmaDestroyImage(
            context_->GetAllocator(),
            static_cast<VkImage>(depth_images_[index]),
            depth_allocations_[index]);
    }
    color_images_.clear();
    color_allocations_.clear();
    depth_images_.clear();
    depth_allocations_.clear();
}

}  // namespace klvk
