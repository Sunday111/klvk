#pragma once

#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

// Images and views used by Application's dynamic-rendering pass. A target may
// be backed by a presentation swapchain or by ordinary offscreen images.
class RenderTarget
{
public:
    virtual ~RenderTarget() = default;

    [[nodiscard]] virtual VkFormat GetFormat() const noexcept = 0;
    [[nodiscard]] virtual VkExtent2D GetExtent() const noexcept = 0;
    [[nodiscard]] virtual size_t GetImageCount() const noexcept = 0;
    [[nodiscard]] virtual VkImage GetImage(size_t index) const = 0;
    [[nodiscard]] virtual VkImageView GetImageView(size_t index) const = 0;
    [[nodiscard]] virtual VkImage GetDepthImage(size_t index) const = 0;
    [[nodiscard]] virtual VkImageView GetDepthImageView(size_t index) const = 0;
};

}  // namespace klvk
