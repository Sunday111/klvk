#include "klvk/vulkan/depth_stencil_format.hpp"

#include <array>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

bool FormatHasStencil(vk::Format format) noexcept
{
    switch (format)
    {
    case vk::Format::eS8Uint:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
        return true;
    default:
        return false;
    }
}

vk::ImageAspectFlags DepthStencilAspectMask(vk::Format format) noexcept
{
    vk::ImageAspectFlags aspect =
        format == vk::Format::eS8Uint ? vk::ImageAspectFlags{} : vk::ImageAspectFlagBits::eDepth;
    if (FormatHasStencil(format)) aspect |= vk::ImageAspectFlagBits::eStencil;
    return aspect;
}

vk::Format SelectDepthStencilFormat(vk::PhysicalDevice physical_device)
{
    constexpr std::array candidates{
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
        vk::Format::eD32Sfloat,
    };

    for (vk::Format candidate : candidates)
    {
        const auto properties = physical_device.getFormatProperties(candidate);
        if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
        {
            return candidate;
        }
    }

    ErrorHandling::ThrowWithMessage("No supported depth-stencil attachment format");
}

}  // namespace klvk
