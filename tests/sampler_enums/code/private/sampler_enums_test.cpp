#include <fmt/format.h>

#include <exception>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/sampler_address_mode.hpp"
#include "klvk/vulkan/sampler_border_color.hpp"
#include "klvk/vulkan/sampler_filter.hpp"
#include "magic_enum/magic_enum.hpp"

namespace klvk
{
namespace
{

static_assert(ToVulkan(SamplerFilter::Nearest) == VK_FILTER_NEAREST);
static_assert(ToVulkan(SamplerFilter::Linear) == VK_FILTER_LINEAR);
static_assert(ToVulkan(SamplerAddressMode::Repeat) == VK_SAMPLER_ADDRESS_MODE_REPEAT);
static_assert(ToVulkan(SamplerAddressMode::MirroredRepeat) == VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
static_assert(ToVulkan(SamplerAddressMode::ClampToEdge) == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
static_assert(ToVulkan(SamplerAddressMode::ClampToBorder) == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
static_assert(ToVulkan(SamplerBorderColor::TransparentBlack) == VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
static_assert(ToVulkan(SamplerBorderColor::TransparentBlackInteger) == VK_BORDER_COLOR_INT_TRANSPARENT_BLACK);
static_assert(ToVulkan(SamplerBorderColor::OpaqueBlack) == VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
static_assert(ToVulkan(SamplerBorderColor::OpaqueBlackInteger) == VK_BORDER_COLOR_INT_OPAQUE_BLACK);
static_assert(ToVulkan(SamplerBorderColor::OpaqueWhite) == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
static_assert(ToVulkan(SamplerBorderColor::OpaqueWhiteInteger) == VK_BORDER_COLOR_INT_OPAQUE_WHITE);

void TestRoundTrips()
{
    for (SamplerFilter value : magic_enum::enum_values<SamplerFilter>())
    {
        ErrorHandling::Ensure(FromVulkan(ToVulkan(value)) == value, "Sampler filter did not round trip");
    }
    for (SamplerAddressMode value : magic_enum::enum_values<SamplerAddressMode>())
    {
        ErrorHandling::Ensure(FromVulkan(ToVulkan(value)) == value, "Sampler address mode did not round trip");
    }
    for (SamplerBorderColor value : magic_enum::enum_values<SamplerBorderColor>())
    {
        ErrorHandling::Ensure(FromVulkan(ToVulkan(value)) == value, "Sampler border color did not round trip");
    }
}

void TestUnknownVulkanValues()
{
    ErrorHandling::Ensure(!FromVulkan(VK_FILTER_MAX_ENUM), "Unknown sampler filter was accepted");
    ErrorHandling::Ensure(!FromVulkan(VK_SAMPLER_ADDRESS_MODE_MAX_ENUM), "Unknown sampler address mode was accepted");
    ErrorHandling::Ensure(!FromVulkan(VK_BORDER_COLOR_MAX_ENUM), "Unknown sampler border color was accepted");
}

}  // namespace
}  // namespace klvk

int main()
{
    try
    {
        klvk::TestRoundTrips();
        klvk::TestUnknownVulkanValues();
        fmt::println("sampler enum tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
}
