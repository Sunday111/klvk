#pragma once

#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

[[nodiscard]] bool FormatHasStencil(VkFormat format) noexcept;

// Aspects a render pass must name when it binds a view of this format as a
// depth-stencil attachment. A combined format needs both, even when only one
// plane is ever read or written.
[[nodiscard]] VkImageAspectFlags DepthStencilAspectMask(VkFormat format) noexcept;

// Highest-preference depth-stencil format the device supports as an optimally
// tiled attachment. Combined formats come first so that a stencil plane is
// available without a second image; the depth plane keeps the precision the
// preferred combined format offers.
[[nodiscard]] VkFormat SelectDepthStencilFormat(VkPhysicalDevice physical_device);

}  // namespace klvk
