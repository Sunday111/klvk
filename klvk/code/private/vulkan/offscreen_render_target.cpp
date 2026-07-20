#include "klvk/vulkan/offscreen_render_target.hpp"

#include <vk_mem_alloc.h>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

namespace
{

struct AllocatedImage
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

AllocatedImage CreateImage(
    DeviceContext& context,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect)
{
    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = extent.width, .height = extent.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    AllocatedImage result;
    CheckVkResult(
        vmaCreateImage(
            context.GetAllocator(),
            &image_info,
            &allocation_info,
            &result.image,
            &result.allocation,
            nullptr),
        "vmaCreateImage(offscreen)");
    try
    {
        result.view = Vulkan::CreateImageView(
            context.GetDevice(),
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = result.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = format,
                .subresourceRange = {.aspectMask = aspect, .levelCount = 1, .layerCount = 1},
            });
    }
    catch (...)
    {
        vmaDestroyImage(context.GetAllocator(), result.image, result.allocation);
        throw;
    }
    return result;
}

}  // namespace

OffscreenRenderTarget::OffscreenRenderTarget(DeviceContext& context, edt::Vec2<u32> size, size_t image_count)
    : context_(&context),
      extent_{.width = size.x(), .height = size.y()}
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
            const AllocatedImage color = CreateImage(
                *context_,
                extent_,
                kColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            color_images_.push_back(color.image);
            color_allocations_.push_back(color.allocation);
            color_image_views_.push_back(color.view);

            const AllocatedImage depth = CreateImage(
                *context_,
                extent_,
                kDepthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT);
            depth_images_.push_back(depth.image);
            depth_allocations_.push_back(depth.allocation);
            depth_image_views_.push_back(depth.view);
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
    for (VkImageView view : color_image_views_) Vulkan::DestroyImageViewNE(context_->GetDevice(), view);
    for (VkImageView view : depth_image_views_) Vulkan::DestroyImageViewNE(context_->GetDevice(), view);
    for (size_t index = 0; index != color_images_.size(); ++index)
    {
        vmaDestroyImage(context_->GetAllocator(), color_images_[index], color_allocations_[index]);
    }
    for (size_t index = 0; index != depth_images_.size(); ++index)
    {
        vmaDestroyImage(context_->GetAllocator(), depth_images_[index], depth_allocations_[index]);
    }
    color_images_.clear();
    color_allocations_.clear();
    color_image_views_.clear();
    depth_images_.clear();
    depth_allocations_.clear();
    depth_image_views_.clear();
}

}  // namespace klvk
