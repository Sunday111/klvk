#pragma once

#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/render_target.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

VK_DEFINE_HANDLE(VmaAllocation)

namespace klvk
{

class DeviceContext;

class Swapchain final : public RenderTarget
{
public:
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    Swapchain(DeviceContext& context, edt::Vec2<u32> framebuffer_size, VkImageUsageFlags additional_image_usage = 0);
    Swapchain(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    ~Swapchain() override;

    void Recreate(edt::Vec2<u32> framebuffer_size);

    [[nodiscard]] VkSwapchainKHR GetHandle() const noexcept { return swapchain_; }
    [[nodiscard]] VkFormat GetFormat() const noexcept override { return format_.format; }
    [[nodiscard]] VkExtent2D GetExtent() const noexcept override { return extent_; }
    [[nodiscard]] size_t GetImageCount() const noexcept override { return images_.size(); }
    [[nodiscard]] VkImage GetImage(size_t index) const override { return images_[index]; }
    [[nodiscard]] VkImageView GetImageView(size_t index) const override { return image_views_[index]; }
    [[nodiscard]] VkImage GetDepthImage(size_t index) const override { return depth_images_[index]; }
    [[nodiscard]] VkImageView GetDepthImageView(size_t index) const override { return depth_image_views_[index]; }

private:
    void Create(edt::Vec2<u32> framebuffer_size, VkSwapchainKHR old_swapchain);
    void DestroyImageViews();

    DeviceContext* context_ = nullptr;
    VkImageUsageFlags additional_image_usage_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR format_{};
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
    std::vector<VkImage> depth_images_;
    std::vector<VmaAllocation> depth_allocations_;
    std::vector<VkImageView> depth_image_views_;
};

}  // namespace klvk
