#include "klvk/vulkan/swapchain.hpp"

#include <vk_mem_alloc.h>

#include <algorithm>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/depth_stencil_format.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

namespace
{

bool IsDiagnosticCaptureFormat(vk::Format format)
{
    return format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb ||
           format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eR8G8B8A8Srgb;
}

vk::SurfaceFormatKHR ChooseSurfaceFormat(vk::PhysicalDevice device, vk::SurfaceKHR surface, bool diagnostic_capture)
{
    const std::vector<vk::SurfaceFormatKHR> formats =
        VulkanValue(device.getSurfaceFormatsKHR(surface), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    ErrorHandling::Ensure(!formats.empty(), "Surface reports no formats");

    for (const auto& format : formats)
    {
        if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            return format;
        }
    }
    if (diagnostic_capture)
    {
        for (const auto& format : formats)
        {
            if (IsDiagnosticCaptureFormat(format.format)) return format;
        }
        ErrorHandling::ThrowWithMessage("Surface provides no RGBA8/BGRA8 format supported by diagnostic capture");
    }
    return formats.front();
}

vk::PresentModeKHR ChoosePresentMode(vk::PhysicalDevice device, vk::SurfaceKHR surface)
{
    const std::vector<vk::PresentModeKHR> modes =
        VulkanValue(device.getSurfacePresentModesKHR(surface), "vkGetPhysicalDeviceSurfacePresentModesKHR");

    // Application paces frames itself (SetTargetFramerate), so prefer modes that do not block on vsync
    // to mirror klgl's glfwSwapInterval(0) behavior.
    for (const vk::PresentModeKHR preferred : {vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eImmediate})
    {
        if (std::ranges::find(modes, preferred) != modes.end()) return preferred;
    }
    return vk::PresentModeKHR::eFifo;
}

}  // namespace

Swapchain::Swapchain(
    DeviceContext& context,
    edt::Vec2<u32> framebuffer_size,
    vk::ImageUsageFlags additional_image_usage)
    : context_(&context),
      additional_image_usage_(additional_image_usage)
{
    Create(framebuffer_size, nullptr);
}

Swapchain::~Swapchain()
{
    DestroyImageViews();
}

void Swapchain::Recreate(edt::Vec2<u32> framebuffer_size)
{
    context_->WaitIdle();
    vk::UniqueSwapchainKHR old_swapchain = std::move(swapchain_);
    DestroyImageViews();
    Create(framebuffer_size, old_swapchain.get());
}

void Swapchain::Create(edt::Vec2<u32> framebuffer_size, vk::SwapchainKHR old_swapchain)
{
    vk::PhysicalDevice physical_device = context_->GetPhysicalDevice();
    vk::SurfaceKHR surface = context_->GetSurface();

    const vk::SurfaceCapabilitiesKHR capabilities =
        VulkanValue(physical_device.getSurfaceCapabilitiesKHR(surface), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    format_ = ChooseSurfaceFormat(
        physical_device,
        surface,
        static_cast<bool>(additional_image_usage_ & vk::ImageUsageFlagBits::eTransferSrc));

    if (capabilities.currentExtent.width != std::numeric_limits<u32>::max())
    {
        extent_ = capabilities.currentExtent;
    }
    else
    {
        extent_ = {
            .width =
                std::clamp(framebuffer_size.x(), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            .height = std::clamp(
                framebuffer_size.y(),
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height),
        };
    }

    u32 image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0)
    {
        image_count = std::min(image_count, capabilities.maxImageCount);
    }

    const vk::ImageUsageFlags image_usage = vk::ImageUsageFlagBits::eColorAttachment | additional_image_usage_;
    ErrorHandling::Ensure(
        (capabilities.supportedUsageFlags & image_usage) == image_usage,
        "Surface does not support required swapchain image usage flags {}",
        vk::to_string(image_usage));

    const vk::SwapchainCreateInfoKHR create_info = vk::SwapchainCreateInfoKHR{}
                                                       .setSurface(surface)
                                                       .setMinImageCount(image_count)
                                                       .setImageFormat(format_.format)
                                                       .setImageColorSpace(format_.colorSpace)
                                                       .setImageExtent(extent_)
                                                       .setImageArrayLayers(1)
                                                       .setImageUsage(image_usage)
                                                       .setImageSharingMode(vk::SharingMode::eExclusive)
                                                       .setPreTransform(capabilities.currentTransform)
                                                       .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                                                       .setPresentMode(ChoosePresentMode(physical_device, surface))
                                                       .setClipped(true)
                                                       .setOldSwapchain(old_swapchain);

    swapchain_ = VulkanValue(context_->GetDevice().createSwapchainKHRUnique(create_info), "vkCreateSwapchainKHR");
    images_ = VulkanValue(context_->GetDevice().getSwapchainImagesKHR(swapchain_.get()), "vkGetSwapchainImagesKHR");

    image_views_.clear();
    image_views_.reserve(images_.size());
    for (auto& image : images_)
    {
        const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                .setLevelCount(1)
                                                                .setLayerCount(1);
        const vk::ImageViewCreateInfo view_info = vk::ImageViewCreateInfo{}
                                                      .setImage(image)
                                                      .setViewType(vk::ImageViewType::e2D)
                                                      .setFormat(format_.format)
                                                      .setSubresourceRange(subresource_range);
        image_views_.push_back(
            VulkanValue(context_->GetDevice().createImageViewUnique(view_info), "vkCreateImageView"));
    }

    depth_stencil_format_ = SelectDepthStencilFormat(context_->GetPhysicalDevice());
    const vk::ImageAspectFlags depth_stencil_aspect = DepthStencilAspectMask(depth_stencil_format_);

    depth_images_.resize(images_.size(), nullptr);
    depth_allocations_.resize(images_.size(), nullptr);
    depth_image_views_.reserve(images_.size());
    for (size_t index = 0; index != images_.size(); ++index)
    {
        const vk::ImageCreateInfo image_info = vk::ImageCreateInfo{}
                                                   .setImageType(vk::ImageType::e2D)
                                                   .setFormat(depth_stencil_format_)
                                                   .setExtent(vk::Extent3D{extent_.width, extent_.height, 1})
                                                   .setMipLevels(1)
                                                   .setArrayLayers(1)
                                                   .setSamples(vk::SampleCountFlagBits::e1)
                                                   .setTiling(vk::ImageTiling::eOptimal)
                                                   .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
                                                   .setSharingMode(vk::SharingMode::eExclusive)
                                                   .setInitialLayout(vk::ImageLayout::eUndefined);
        const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        const VkImageCreateInfo& raw_image_info = image_info;
        VkImage raw_depth_image = nullptr;
        VulkanCheck(
            static_cast<vk::Result>(vmaCreateImage(
                context_->GetAllocator(),
                &raw_image_info,
                &allocation_info,
                &raw_depth_image,
                &depth_allocations_[index],
                nullptr)),
            "vmaCreateImage(depth)");
        depth_images_[index] = raw_depth_image;

        const vk::ImageSubresourceRange subresource_range =
            vk::ImageSubresourceRange{}.setAspectMask(depth_stencil_aspect).setLevelCount(1).setLayerCount(1);
        const vk::ImageViewCreateInfo view_info = vk::ImageViewCreateInfo{}
                                                      .setImage(depth_images_[index])
                                                      .setViewType(vk::ImageViewType::e2D)
                                                      .setFormat(depth_stencil_format_)
                                                      .setSubresourceRange(subresource_range);
        depth_image_views_.push_back(
            VulkanValue(context_->GetDevice().createImageViewUnique(view_info), "vkCreateImageView(depth)"));
    }
}

void Swapchain::DestroyImageViews()
{
    image_views_.clear();

    depth_image_views_.clear();
    for (size_t index = 0; index != depth_images_.size(); ++index)
    {
        if (depth_images_[index])
        {
            const VkImage raw_depth_image = depth_images_[index];
            vmaDestroyImage(context_->GetAllocator(), raw_depth_image, depth_allocations_[index]);
        }
    }
    depth_images_.clear();
    depth_allocations_.clear();
}

}  // namespace klvk
