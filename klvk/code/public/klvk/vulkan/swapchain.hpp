#pragma once

#include <vector>

#include "edt/math/matrix.hpp"
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
    Swapchain(DeviceContext& context, edt::Vec2<u32> framebuffer_size, vk::ImageUsageFlags additional_image_usage = {});
    Swapchain(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    ~Swapchain() override;

    void Recreate(edt::Vec2<u32> framebuffer_size);

    [[nodiscard]] vk::SwapchainKHR GetHandle() const noexcept { return swapchain_; }
    [[nodiscard]] vk::Format GetFormat() const noexcept override { return format_.format; }
    [[nodiscard]] vk::Format GetDepthStencilFormat() const noexcept override { return depth_stencil_format_; }
    [[nodiscard]] vk::Extent2D GetExtent() const noexcept override { return extent_; }
    [[nodiscard]] size_t GetImageCount() const noexcept override { return images_.size(); }
    [[nodiscard]] vk::Image GetImage(size_t index) const override { return images_[index]; }
    [[nodiscard]] vk::ImageView GetImageView(size_t index) const override { return image_views_[index]; }
    [[nodiscard]] vk::Image GetDepthImage(size_t index) const override { return depth_images_[index]; }
    [[nodiscard]] vk::ImageView GetDepthImageView(size_t index) const override { return depth_image_views_[index]; }

private:
    void Create(edt::Vec2<u32> framebuffer_size, vk::SwapchainKHR old_swapchain);
    void DestroyImageViews();

    DeviceContext* context_ = nullptr;
    vk::ImageUsageFlags additional_image_usage_{};
    vk::SwapchainKHR swapchain_ = nullptr;
    vk::SurfaceFormatKHR format_{};
    vk::Format depth_stencil_format_ = vk::Format::eUndefined;
    vk::Extent2D extent_{};
    std::vector<vk::Image> images_;
    std::vector<vk::ImageView> image_views_;
    std::vector<vk::Image> depth_images_;
    std::vector<VmaAllocation> depth_allocations_;
    std::vector<vk::ImageView> depth_image_views_;
};

}  // namespace klvk
