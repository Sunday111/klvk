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

    [[nodiscard]] virtual vk::Format GetFormat() const noexcept = 0;
    [[nodiscard]] virtual vk::Format GetDepthStencilFormat() const noexcept = 0;
    [[nodiscard]] virtual vk::Extent2D GetExtent() const noexcept = 0;
    [[nodiscard]] virtual size_t GetImageCount() const noexcept = 0;
    [[nodiscard]] virtual vk::Image GetImage(size_t index) const = 0;
    [[nodiscard]] virtual vk::ImageView GetImageView(size_t index) const = 0;
    [[nodiscard]] virtual vk::Image GetDepthImage(size_t index) const = 0;
    [[nodiscard]] virtual vk::ImageView GetDepthImageView(size_t index) const = 0;
};

}  // namespace klvk
