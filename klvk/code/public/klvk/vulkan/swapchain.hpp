#pragma once

#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class DeviceContext;

class Swapchain
{
public:
    Swapchain(DeviceContext& context, edt::Vec2<uint32_t> framebuffer_size);
    Swapchain(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    ~Swapchain();

    void Recreate(edt::Vec2<uint32_t> framebuffer_size);

    [[nodiscard]] VkSwapchainKHR GetHandle() const noexcept { return swapchain_; }
    [[nodiscard]] VkFormat GetFormat() const noexcept { return format_.format; }
    [[nodiscard]] VkExtent2D GetExtent() const noexcept { return extent_; }
    [[nodiscard]] size_t GetImageCount() const noexcept { return images_.size(); }
    [[nodiscard]] VkImage GetImage(size_t index) const { return images_[index]; }
    [[nodiscard]] VkImageView GetImageView(size_t index) const { return image_views_[index]; }

private:
    void Create(edt::Vec2<uint32_t> framebuffer_size, VkSwapchainKHR old_swapchain);
    void DestroyImageViews();

    DeviceContext* context_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR format_{};
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
};

}  // namespace klvk
