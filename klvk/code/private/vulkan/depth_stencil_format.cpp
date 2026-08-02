#include "klvk/vulkan/depth_stencil_format.hpp"

#include <array>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

namespace klvk
{

bool FormatHasStencil(VkFormat format) noexcept
{
    switch (format)
    {
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

VkImageAspectFlags DepthStencilAspectMask(VkFormat format) noexcept
{
    VkImageAspectFlags aspect = format == VK_FORMAT_S8_UINT ? 0u : VK_IMAGE_ASPECT_DEPTH_BIT;
    if (FormatHasStencil(format)) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    return aspect;
}

VkFormat SelectDepthStencilFormat(VkPhysicalDevice physical_device)
{
    constexpr std::array candidates{
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
    };

    for (VkFormat candidate : candidates)
    {
        const auto properties = Vulkan::GetPhysicalDeviceFormatProperties(physical_device, candidate);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        {
            return candidate;
        }
    }

    ErrorHandling::ThrowWithMessage("No supported depth-stencil attachment format");
}

}  // namespace klvk
