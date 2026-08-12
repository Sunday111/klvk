#pragma once

#include <optional>
#include <utility>

#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/detail/vulkan_enum_converter.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

enum class SamplerAddressMode : u8
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};

namespace detail
{
inline constexpr auto kSamplerAddressModeConverter = []
{
    VulkanEnumConverter<SamplerAddressMode, VkSamplerAddressMode> converter;
    converter.Add(SamplerAddressMode::Repeat, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    converter.Add(SamplerAddressMode::MirroredRepeat, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
    converter.Add(SamplerAddressMode::ClampToEdge, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    converter.Add(SamplerAddressMode::ClampToBorder, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    return std::move(converter).Complete();
}();
}  // namespace detail

[[nodiscard]] constexpr VkSamplerAddressMode ToVulkan(SamplerAddressMode value) noexcept
{
    return detail::kSamplerAddressModeConverter.ToVulkan(value);
}

[[nodiscard]] constexpr std::optional<SamplerAddressMode> FromVulkan(VkSamplerAddressMode value) noexcept
{
    return detail::kSamplerAddressModeConverter.FromVulkan(value);
}

}  // namespace klvk
