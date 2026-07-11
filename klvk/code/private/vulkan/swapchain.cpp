#include "klvk/vulkan/swapchain.hpp"

#include <algorithm>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"

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
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats.data());
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
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes.data());

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
    if (swapchain_) vkDestroySwapchainKHR(context_->GetDevice(), swapchain_, nullptr);
}

void Swapchain::Recreate(edt::Vec2<uint32_t> framebuffer_size)
{
    context_->WaitIdle();
    DestroyImageViews();
    VkSwapchainKHR old_swapchain = swapchain_;
    Create(framebuffer_size, old_swapchain);
    if (old_swapchain) vkDestroySwapchainKHR(context_->GetDevice(), old_swapchain, nullptr);
}

void Swapchain::Create(edt::Vec2<uint32_t> framebuffer_size, VkSwapchainKHR old_swapchain)
{
    VkPhysicalDevice physical_device = context_->GetPhysicalDevice();
    VkSurfaceKHR surface = context_->GetSurface();

    VkSurfaceCapabilitiesKHR capabilities{};
    CheckVkResult(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

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

    CheckVkResult(
        vkCreateSwapchainKHR(context_->GetDevice(), &create_info, nullptr, &swapchain_),
        "vkCreateSwapchainKHR");

    uint32_t actual_count = 0;
    vkGetSwapchainImagesKHR(context_->GetDevice(), swapchain_, &actual_count, nullptr);
    images_.resize(actual_count);
    vkGetSwapchainImagesKHR(context_->GetDevice(), swapchain_, &actual_count, images_.data());

    image_views_.resize(actual_count);
    for (size_t index = 0; index != images_.size(); ++index)
    {
        const VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images_[index],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format_.format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        };
        CheckVkResult(
            vkCreateImageView(context_->GetDevice(), &view_info, nullptr, &image_views_[index]),
            "vkCreateImageView");
    }
}

void Swapchain::DestroyImageViews()
{
    for (VkImageView view : image_views_)
    {
        vkDestroyImageView(context_->GetDevice(), view, nullptr);
    }
    image_views_.clear();
}

}  // namespace klvk
