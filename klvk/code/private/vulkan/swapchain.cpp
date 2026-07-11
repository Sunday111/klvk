#include "klvk/vulkan/swapchain.hpp"

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
}

void Swapchain::DestroyImageViews()
{
    for (VkImageView view : image_views_)
    {
        Vulkan::DestroyImageViewNE(context_->GetDevice(), view);
    }
    image_views_.clear();
}

}  // namespace klvk
