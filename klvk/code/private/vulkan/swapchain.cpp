#include "klvk/vulkan/swapchain.hpp"

#include <vk_mem_alloc.h>

#include <algorithm>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

namespace
{

VkSurfaceFormatKHR ChooseSurfaceFormat(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    const std::vector<VkSurfaceFormatKHR> formats = Vulkan::GetPhysicalDeviceSurfaceFormatsKHR(device, surface);
    ErrorHandling::Ensure(!formats.empty(), "Surface reports no formats");

    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR ChoosePresentMode(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    const std::vector<VkPresentModeKHR> modes = Vulkan::GetPhysicalDeviceSurfacePresentModesKHR(device, surface);

    // Application paces frames itself (SetTargetFramerate), so prefer modes that do not block on vsync
    // to mirror klgl's glfwSwapInterval(0) behavior.
    for (const VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
    {
        if (std::ranges::find(modes, preferred) != modes.end()) return preferred;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

}  // namespace

Swapchain::Swapchain(DeviceContext& context, edt::Vec2<uint32_t> framebuffer_size) : context_(&context)
{
    Create(framebuffer_size, VK_NULL_HANDLE);
}

Swapchain::~Swapchain()
{
    DestroyImageViews();
    if (swapchain_) Vulkan::DestroySwapchainKHRNE(context_->GetDevice(), swapchain_);
}

void Swapchain::Recreate(edt::Vec2<uint32_t> framebuffer_size)
{
    context_->WaitIdle();
    DestroyImageViews();
    VkSwapchainKHR old_swapchain = swapchain_;
    Create(framebuffer_size, old_swapchain);
    if (old_swapchain) Vulkan::DestroySwapchainKHRNE(context_->GetDevice(), old_swapchain);
}

void Swapchain::Create(edt::Vec2<uint32_t> framebuffer_size, VkSwapchainKHR old_swapchain)
{
    VkPhysicalDevice physical_device = context_->GetPhysicalDevice();
    VkSurfaceKHR surface = context_->GetSurface();

    const VkSurfaceCapabilitiesKHR capabilities =
        Vulkan::GetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface);

    format_ = ChooseSurfaceFormat(physical_device, surface);

    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        extent_ = capabilities.currentExtent;
    }
    else
    {
        extent_ = {
            std::clamp(framebuffer_size.x(), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(framebuffer_size.y(), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0)
    {
        image_count = std::min(image_count, capabilities.maxImageCount);
    }

    const VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = format_.format,
        .imageColorSpace = format_.colorSpace,
        .imageExtent = extent_,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = ChoosePresentMode(physical_device, surface),
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain,
    };

    swapchain_ = Vulkan::CreateSwapchainKHR(context_->GetDevice(), create_info);
    images_ = Vulkan::GetSwapchainImagesKHR(context_->GetDevice(), swapchain_);

    image_views_.clear();
    image_views_.reserve(images_.size());
    for (size_t index = 0; index != images_.size(); ++index)
    {
        const VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images_[index],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format_.format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        };
        image_views_.push_back(Vulkan::CreateImageView(context_->GetDevice(), view_info));
    }

    depth_images_.resize(images_.size(), VK_NULL_HANDLE);
    depth_allocations_.resize(images_.size(), VK_NULL_HANDLE);
    depth_image_views_.reserve(images_.size());
    for (size_t index = 0; index != images_.size(); ++index)
    {
        const VkImageCreateInfo image_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = kDepthFormat,
            .extent = {extent_.width, extent_.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        CheckVkResult(
            vmaCreateImage(
                context_->GetAllocator(),
                &image_info,
                &allocation_info,
                &depth_images_[index],
                &depth_allocations_[index],
                nullptr),
            "vmaCreateImage(depth)");

        depth_image_views_.push_back(
            Vulkan::CreateImageView(
                context_->GetDevice(),
                {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .image = depth_images_[index],
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = kDepthFormat,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1},
                }));
    }
}

void Swapchain::DestroyImageViews()
{
    for (VkImageView view : image_views_)
    {
        Vulkan::DestroyImageViewNE(context_->GetDevice(), view);
    }
    image_views_.clear();

    for (VkImageView view : depth_image_views_)
    {
        Vulkan::DestroyImageViewNE(context_->GetDevice(), view);
    }
    depth_image_views_.clear();
    for (size_t index = 0; index != depth_images_.size(); ++index)
    {
        if (depth_images_[index])
        {
            vmaDestroyImage(context_->GetAllocator(), depth_images_[index], depth_allocations_[index]);
        }
    }
    depth_images_.clear();
    depth_allocations_.clear();
}

}  // namespace klvk
