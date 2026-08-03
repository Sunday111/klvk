#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "edt/math/matrix.hpp"
#include "klvk/float_aliases.hpp"
#include "klvk/integral_aliases.hpp"

namespace klvk
{

// One command of a glyph's outline, in font units. Quadratic and cubic carry
// their control points ahead of the endpoint, so a consumer reads `points` up to
// the count the kind implies.
struct GlyphOutlineCommand
{
    enum class Kind : u8
    {
        Move,
        Line,
        Quadratic,
        Cubic,
        Close,
    };

    Kind kind = Kind::Move;
    std::array<edt::Vec2f, 3> points{};

    [[nodiscard]] static constexpr size_t GetPointCount(Kind kind) noexcept
    {
        switch (kind)
        {
        case Kind::Move:
        case Kind::Line:
            return 1;
        case Kind::Quadratic:
            return 2;
        case Kind::Cubic:
            return 3;
        case Kind::Close:
            return 0;
        }
        return 0;
    }
};

// A glyph rasterized at a particular pixel size: the coverage bitmap plus where
// it sits relative to the pen and how far the pen then moves.
struct RasterizedGlyph
{
    edt::Vec2<u32> size{};
    // From the pen position to the top-left of the bitmap, y up.
    edt::Vec2f bearing{};
    f32 advance = 0.f;
    // One byte of coverage per pixel, row major, `size.x() * size.y()` long.
    std::vector<u8> coverage;
};

// A font file opened through FreeType. Glyphs come out either as outlines in
// font units - which scale to any size and can be filled, stroked or transformed
// like any other path - or as a coverage bitmap at a chosen pixel size.
class FontFace
{
public:
    [[nodiscard]] static std::unique_ptr<FontFace> FromFile(const std::filesystem::path& path);
    // Takes ownership of the file's bytes, which FreeType reads for the face's lifetime.
    [[nodiscard]] static std::unique_ptr<FontFace> FromMemory(std::vector<u8> bytes);

    FontFace(const FontFace&) = delete;
    FontFace(FontFace&&) = delete;
    ~FontFace();

    // Font units per em, the scale outlines are expressed in.
    [[nodiscard]] f32 GetUnitsPerEm() const noexcept;

    // Zero for a codepoint the font has no glyph for.
    [[nodiscard]] u32 GetGlyphIndex(char32_t codepoint) const;

    // In font units. Empty for a glyph with no contours, such as a space.
    [[nodiscard]] std::vector<GlyphOutlineCommand> GetGlyphOutline(u32 glyph_index) const;

    // In font units.
    [[nodiscard]] f32 GetAdvance(u32 glyph_index) const;

    // Rasterizes at a pixel size. The result's coverage is empty for a glyph that
    // draws nothing, which is still a valid glyph with an advance.
    [[nodiscard]] RasterizedGlyph Rasterize(u32 glyph_index, u32 pixel_size) const;

    // Baseline to baseline, in pixels at this size.
    [[nodiscard]] f32 GetLineHeight(u32 pixel_size) const;

private:
    FontFace() = default;

    struct Internal;
    std::unique_ptr<Internal> internal_;
    std::vector<u8> bytes_;
};

}  // namespace klvk
