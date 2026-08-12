#pragma once

#include <optional>
#include <utility>

#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/detail/vulkan_enum_converter.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

enum class SamplerFilter : u8
{
    Nearest,
    Linear,
};

namespace detail
{
inline constexpr auto kSamplerFilterConverter = []
{
    VulkanEnumConverter<SamplerFilter, VkFilter> converter;
    converter.Add(SamplerFilter::Nearest, VK_FILTER_NEAREST);
    converter.Add(SamplerFilter::Linear, VK_FILTER_LINEAR);
    return std::move(converter).Complete();
}();
}  // namespace detail

[[nodiscard]] constexpr VkFilter ToVulkan(SamplerFilter value) noexcept
{
    return detail::kSamplerFilterConverter.ToVulkan(value);
}

[[nodiscard]] constexpr std::optional<SamplerFilter> FromVulkan(VkFilter value) noexcept
{
    return detail::kSamplerFilterConverter.FromVulkan(value);
}

}  // namespace klvk
