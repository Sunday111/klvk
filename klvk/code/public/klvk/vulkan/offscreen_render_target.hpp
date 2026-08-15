#pragma once

#include <vector>

#include "edt/math/matrix.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/render_target.hpp"

VK_DEFINE_HANDLE(VmaAllocation)

namespace klvk
{

class DeviceContext;

class OffscreenRenderTarget final : public RenderTarget
{
public:
    static constexpr vk::Format kColorFormat = vk::Format::eR8G8B8A8Unorm;

    OffscreenRenderTarget(DeviceContext& context, edt::Vec2<u32> size, size_t image_count);
    OffscreenRenderTarget(const OffscreenRenderTarget&) = delete;
    OffscreenRenderTarget(OffscreenRenderTarget&&) = delete;
    ~OffscreenRenderTarget() override;

    [[nodiscard]] vk::Format GetFormat() const noexcept override { return kColorFormat; }
    [[nodiscard]] vk::Format GetDepthStencilFormat() const noexcept override { return depth_stencil_format_; }
    [[nodiscard]] vk::Extent2D GetExtent() const noexcept override { return extent_; }
    [[nodiscard]] size_t GetImageCount() const noexcept override { return color_images_.size(); }
    [[nodiscard]] vk::Image GetImage(size_t index) const override { return color_images_[index]; }
    [[nodiscard]] vk::ImageView GetImageView(size_t index) const override { return color_image_views_[index]; }
    [[nodiscard]] vk::Image GetDepthImage(size_t index) const override { return depth_images_[index]; }
    [[nodiscard]] vk::ImageView GetDepthImageView(size_t index) const override { return depth_image_views_[index]; }

private:
    void CreateImages(size_t image_count);
    void DestroyImages();

    DeviceContext* context_ = nullptr;
    vk::Format depth_stencil_format_ = vk::Format::eUndefined;
    vk::Extent2D extent_{};
    std::vector<vk::Image> color_images_;
    std::vector<VmaAllocation> color_allocations_;
    std::vector<vk::ImageView> color_image_views_;
    std::vector<vk::Image> depth_images_;
    std::vector<VmaAllocation> depth_allocations_;
    std::vector<vk::ImageView> depth_image_views_;
};

}  // namespace klvk
