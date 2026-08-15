#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "klvk/text/font_face.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/texture.hpp"

namespace klvk
{

class DeviceContext;

// Glyphs of one face at one pixel size, packed into a single coverage texture as
// they are first asked for.
//
// A glyph that is not in the atlas yet is rasterized and packed by Add, and the
// copy into the texture is recorded by RecordPendingUploads before the pass that
// samples it. Packing is append-only, which is what makes it safe to write while
// an earlier frame is still sampling: no frame can be reading space that no frame
// before it knew about. Reclaiming space would break that and needs more than a
// barrier.
//
// Rasterizing costs real time, so a caller that knows what it will draw should
// Add it up front rather than discovering it a glyph at a time.
class GlyphAtlas
{
public:
    struct Glyph
    {
        // Texture coordinates of the glyph's box.
        edt::Vec2f uv_min{};
        edt::Vec2f uv_max{};
        // Size in pixels, and the offset from the pen to the box's top-left, y up.
        edt::Vec2f size{};
        edt::Vec2f bearing{};
        f32 advance = 0.f;
    };

    GlyphAtlas(
        DeviceContext& context,
        const FontFace& face,
        u32 pixel_size,
        edt::Vec2<u32> texture_size,
        size_t frames_in_flight);
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas(GlyphAtlas&&) = delete;
    ~GlyphAtlas();

    // Rasterizes and packs whatever is not packed already. Returns false when the
    // texture had no room left, which is a size to raise rather than a glyph to
    // quietly lose.
    bool Add(std::u32string_view codepoints);

    // Records the copies for everything added since the last call, and the barrier
    // that makes them visible to sampling. Call before beginning the pass that
    // draws the text, with the frame slot being recorded.
    void RecordPendingUploads(vk::CommandBuffer command_buffer, size_t frame_index);

    [[nodiscard]] std::optional<Glyph> Find(char32_t codepoint) const;
    [[nodiscard]] bool Contains(char32_t codepoint) const { return glyphs_.contains(codepoint); }
    [[nodiscard]] const Texture& GetTexture() const noexcept { return *texture_; }
    [[nodiscard]] f32 GetLineHeight() const noexcept { return line_height_; }
    [[nodiscard]] u32 GetPixelSize() const noexcept { return pixel_size_; }

private:
    // Shelf packing: a row is filled left to right and the next starts above the
    // tallest glyph of the one below. Glyphs of one size are near enough uniform
    // in height for that to waste little, and it needs no bookkeeping beyond a pen
    // and a shelf height.
    bool Place(edt::Vec2<u32> size, edt::Vec2<u32>& position);
    void EnsureStagingCapacity(size_t frame_index, size_t bytes);

    struct PendingGlyph
    {
        edt::Vec2<u32> offset{};
        edt::Vec2<u32> size{};
        std::vector<u8> coverage;
    };

    DeviceContext* context_ = nullptr;
    const FontFace* face_ = nullptr;
    u32 pixel_size_ = 0;
    f32 line_height_ = 0.f;
    edt::Vec2<u32> texture_size_{};
    std::unordered_map<char32_t, Glyph> glyphs_;
    std::unique_ptr<Texture> texture_;
    std::vector<PendingGlyph> pending_;
    std::vector<GpuBuffer> staging_buffers_;

    edt::Vec2<u32> pen_{};
    u32 shelf_height_ = 0;
};

}  // namespace klvk
