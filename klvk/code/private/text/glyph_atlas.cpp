#include "klvk/text/glyph_atlas.hpp"

#include <algorithm>

#include "klvk/vulkan/texture.hpp"

namespace klvk
{

namespace
{

// A pixel of space around each glyph so that bilinear sampling at the edge of one
// cannot reach into its neighbour.
constexpr u32 kPadding = 1;

}  // namespace

GlyphAtlas::GlyphAtlas(const FontFace& face, u32 pixel_size, edt::Vec2<u32> texture_size)
    : face_(&face),
      pixel_size_(pixel_size),
      line_height_(face.GetLineHeight(pixel_size)),
      texture_size_(texture_size),
      coverage_(static_cast<size_t>(texture_size.x()) * texture_size.y(), 0)
{
}

GlyphAtlas::~GlyphAtlas() = default;

bool GlyphAtlas::Place(edt::Vec2<u32> size, edt::Vec2<u32>& position)
{
    if (size.x() > texture_size_.x()) return false;

    if (pen_.x() + size.x() > texture_size_.x())
    {
        pen_ = {0, pen_.y() + shelf_height_ + kPadding};
        shelf_height_ = 0;
    }
    if (pen_.y() + size.y() > texture_size_.y()) return false;

    position = pen_;
    pen_.x() += size.x() + kPadding;
    shelf_height_ = std::max(shelf_height_, size.y());
    return true;
}

bool GlyphAtlas::Add(std::u32string_view codepoints)
{
    bool packed_everything = true;
    for (const char32_t codepoint : codepoints)
    {
        if (glyphs_.contains(codepoint)) continue;

        const u32 index = face_->GetGlyphIndex(codepoint);
        const RasterizedGlyph rasterized = face_->Rasterize(index, pixel_size_);

        Glyph glyph{
            .uv_min = {},
            .uv_max = {},
            .size = rasterized.size.Cast<f32>(),
            .bearing = rasterized.bearing,
            .advance = rasterized.advance,
        };

        // A space has an advance and no coverage; it is a glyph the atlas knows
        // about that simply occupies none of it.
        if (rasterized.coverage.empty())
        {
            glyphs_.emplace(codepoint, glyph);
            continue;
        }

        edt::Vec2<u32> position{};
        if (!Place(rasterized.size, position))
        {
            packed_everything = false;
            continue;
        }

        for (u32 row = 0; row != rasterized.size.y(); ++row)
        {
            const auto source = rasterized.coverage.begin() + (static_cast<ptrdiff_t>(row) * rasterized.size.x());
            const size_t destination = ((static_cast<size_t>(position.y()) + row) * texture_size_.x()) + position.x();
            std::copy_n(source, rasterized.size.x(), coverage_.begin() + static_cast<ptrdiff_t>(destination));
        }

        const edt::Vec2f texture_size = texture_size_.Cast<f32>();
        glyph.uv_min = position.Cast<f32>() / texture_size;
        glyph.uv_max = (position + rasterized.size).Cast<f32>() / texture_size;
        glyphs_.emplace(codepoint, glyph);
    }
    return packed_everything;
}

void GlyphAtlas::Upload(DeviceContext& context)
{
    texture_ = Texture::CreateR8(context, texture_size_, coverage_);
}

std::optional<GlyphAtlas::Glyph> GlyphAtlas::Find(char32_t codepoint) const
{
    const auto found = glyphs_.find(codepoint);
    if (found == glyphs_.end()) return std::nullopt;
    return found->second;
}

}  // namespace klvk
