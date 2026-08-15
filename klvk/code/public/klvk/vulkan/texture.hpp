#pragma once

#include <memory>
#include <span>

#include "edt/math/matrix.hpp"
#include "klvk/integral_aliases.hpp"
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
    [[nodiscard]] static std::unique_ptr<Texture>
    CreateR8(DeviceContext& context, edt::Vec2<u32> size, std::span<const u8> pixels);

    // Four channel texture, one byte per channel, in the order the pixels are
    // given. Not an sRGB format: what is uploaded is what a shader samples.
    [[nodiscard]] static std::unique_ptr<Texture>
    CreateRgba8(DeviceContext& context, edt::Vec2<u32> size, std::span<const u8> pixels);

    // Decodes an encoded image - PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PIC or PNM -
    // and uploads it as four channels, whatever the file held. Null when the bytes
    // are not an image this can read, which a truncated or unrecognized file is.
    //
    // The decoder is an implementation detail: a caller hands over the bytes it
    // read and gets back a texture, and never links or includes one itself.
    // Which decoder that is lives behind DecodeImage in klvk/image.
    [[nodiscard]] static std::unique_ptr<Texture> CreateFromEncoded(
        DeviceContext& context,
        std::span<const u8> encoded);

    // Zeroed single channel texture, for one that is filled in later by
    // RecordRegionUpdates rather than all at once.
    [[nodiscard]] static std::unique_ptr<Texture> CreateEmptyR8(DeviceContext& context, edt::Vec2<u32> size);

    // A rectangle to overwrite, and where its rows start in the staging buffer.
    struct RegionUpdate
    {
        vk::DeviceSize buffer_offset = 0;
        edt::Vec2<u32> offset{};
        edt::Vec2<u32> size{};
    };

    // Records the copies and the barriers that make them visible to sampling.
    // Everything shares one barrier pair, so a frame that adds several regions
    // pays for one transition rather than one per region.
    //
    // The regions must not be ones an unfinished frame is still sampling. Writing
    // only into space no earlier frame knew about - as an append-only packer does
    // - satisfies that; overwriting occupied space does not.
    void
    RecordRegionUpdates(vk::CommandBuffer command_buffer, vk::Buffer staging, std::span<const RegionUpdate> regions);

    Texture(const Texture&) = delete;
    Texture(Texture&&) = delete;
    ~Texture();

    [[nodiscard]] vk::ImageView GetView() const noexcept { return view_; }
    [[nodiscard]] vk::Sampler GetSampler() const noexcept { return sampler_; }
    [[nodiscard]] edt::Vec2<u32> GetSize() const noexcept { return size_; }

private:
    Texture() = default;

    [[nodiscard]] static std::unique_ptr<Texture> Create(
        DeviceContext& context,
        edt::Vec2<u32> size,
        std::span<const u8> pixels,
        vk::Format format,
        u32 bytes_per_pixel);

    DeviceContext* context_ = nullptr;
    vk::Image image_ = nullptr;
    VmaAllocation allocation_ = nullptr;
    vk::ImageView view_ = nullptr;
    vk::Sampler sampler_ = nullptr;
    edt::Vec2<u32> size_{};
};

}  // namespace klvk
