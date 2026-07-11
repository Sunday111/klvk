#pragma once

#include <memory>
#include <span>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

VK_DEFINE_HANDLE(VmaAllocation)

namespace klvk
{

class DeviceContext;

// A sampled 2d image with its view and sampler, uploaded once through a staging buffer.
class Texture
{
public:
    // Single channel texture. In shaders the red component holds the pixel value.
    [[nodiscard]] static std::unique_ptr<Texture> CreateR8(
        DeviceContext& context,
        edt::Vec2<uint32_t> size,
        std::span<const uint8_t> pixels);

    Texture(const Texture&) = delete;
    Texture(Texture&&) = delete;
    ~Texture();

    [[nodiscard]] VkImageView GetView() const noexcept { return view_; }
    [[nodiscard]] VkSampler GetSampler() const noexcept { return sampler_; }
    [[nodiscard]] edt::Vec2<uint32_t> GetSize() const noexcept { return size_; }

private:
    Texture() = default;

    DeviceContext* context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    edt::Vec2<uint32_t> size_{};
};

}  // namespace klvk
