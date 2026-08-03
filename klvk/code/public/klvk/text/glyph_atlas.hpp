#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "klvk/text/font_face.hpp"

namespace klvk
{

class DeviceContext;
class Texture;

// Glyphs of one face at one pixel size, packed into a single coverage texture.
//
// Glyphs are rasterized into a CPU bitmap by Add and the texture is created once
// by Upload, because a klvk texture is uploaded at creation and never written
// again. Drawing a different size means a different atlas.
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

    GlyphAtlas(const FontFace& face, u32 pixel_size, edt::Vec2<u32> texture_size = {512, 512});
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas(GlyphAtlas&&) = delete;
    ~GlyphAtlas();

    // Rasterizes and packs whatever is not packed already. Returns false when the
    // texture had no room left, which is a size to raise rather than a glyph to
    // silently drop.
    bool Add(std::u32string_view codepoints);

    // Creates the texture from what has been added. Adding after this has no
    // effect on the texture until it is called again.
    void Upload(DeviceContext& context);

    [[nodiscard]] std::optional<Glyph> Find(char32_t codepoint) const;
    [[nodiscard]] const Texture* GetTexture() const noexcept { return texture_.get(); }
    [[nodiscard]] f32 GetLineHeight() const noexcept { return line_height_; }
    [[nodiscard]] u32 GetPixelSize() const noexcept { return pixel_size_; }

private:
    // Shelf packing: rows are filled left to right and a new row starts above the
    // tallest glyph of the one below. Enough for a glyph set, which is uniform in
    // height, and it needs no bookkeeping beyond three numbers.
    bool Place(edt::Vec2<u32> size, edt::Vec2<u32>& position);

    const FontFace* face_ = nullptr;
    u32 pixel_size_ = 0;
    f32 line_height_ = 0.f;
    edt::Vec2<u32> texture_size_{};
    std::vector<u8> coverage_;
    std::unordered_map<char32_t, Glyph> glyphs_;
    std::unique_ptr<Texture> texture_;

    edt::Vec2<u32> pen_{};
    u32 shelf_height_ = 0;
};

}  // namespace klvk
