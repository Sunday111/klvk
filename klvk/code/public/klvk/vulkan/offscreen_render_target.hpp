#pragma once

#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/render_target.hpp"

VK_DEFINE_HANDLE(VmaAllocation)

namespace klvk
{

class DeviceContext;

class OffscreenRenderTarget final : public RenderTarget
{
public:
    static constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    OffscreenRenderTarget(DeviceContext& context, edt::Vec2<u32> size, size_t image_count);
    OffscreenRenderTarget(const OffscreenRenderTarget&) = delete;
    OffscreenRenderTarget(OffscreenRenderTarget&&) = delete;
    ~OffscreenRenderTarget() override;

    [[nodiscard]] VkFormat GetFormat() const noexcept override { return kColorFormat; }
    [[nodiscard]] VkExtent2D GetExtent() const noexcept override { return extent_; }
    [[nodiscard]] size_t GetImageCount() const noexcept override { return color_images_.size(); }
    [[nodiscard]] VkImage GetImage(size_t index) const override { return color_images_[index]; }
    [[nodiscard]] VkImageView GetImageView(size_t index) const override { return color_image_views_[index]; }
    [[nodiscard]] VkImage GetDepthImage(size_t index) const override { return depth_images_[index]; }
    [[nodiscard]] VkImageView GetDepthImageView(size_t index) const override { return depth_image_views_[index]; }

private:
    void CreateImages(size_t image_count);
    void DestroyImages();

    DeviceContext* context_ = nullptr;
    VkExtent2D extent_{};
    std::vector<VkImage> color_images_;
    std::vector<VmaAllocation> color_allocations_;
    std::vector<VkImageView> color_image_views_;
    std::vector<VkImage> depth_images_;
    std::vector<VmaAllocation> depth_allocations_;
    std::vector<VkImageView> depth_image_views_;
};

}  // namespace klvk
