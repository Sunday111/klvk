#pragma once

#include <optional>
#include <utility>

#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/detail/vulkan_enum_converter.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

enum class SamplerBorderColor : u8
{
    TransparentBlack,
    TransparentBlackInteger,
    OpaqueBlack,
    OpaqueBlackInteger,
    OpaqueWhite,
    OpaqueWhiteInteger,
};

namespace detail
{
inline constexpr auto kSamplerBorderColorConverter = []
{
    VulkanEnumConverter<SamplerBorderColor, VkBorderColor> converter;
    converter.Add(SamplerBorderColor::TransparentBlack, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
    converter.Add(SamplerBorderColor::TransparentBlackInteger, VK_BORDER_COLOR_INT_TRANSPARENT_BLACK);
    converter.Add(SamplerBorderColor::OpaqueBlack, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
    converter.Add(SamplerBorderColor::OpaqueBlackInteger, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    converter.Add(SamplerBorderColor::OpaqueWhite, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
    converter.Add(SamplerBorderColor::OpaqueWhiteInteger, VK_BORDER_COLOR_INT_OPAQUE_WHITE);
    return std::move(converter).Complete();
}();
}  // namespace detail

[[nodiscard]] constexpr VkBorderColor ToVulkan(SamplerBorderColor value) noexcept
{
    return detail::kSamplerBorderColorConverter.ToVulkan(value);
}

[[nodiscard]] constexpr std::optional<SamplerBorderColor> FromVulkan(VkBorderColor value) noexcept
{
    return detail::kSamplerBorderColorConverter.FromVulkan(value);
}

}  // namespace klvk
