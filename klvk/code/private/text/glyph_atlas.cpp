#include "klvk/text/glyph_atlas.hpp"

#include <algorithm>

#include "klvk/vulkan/device_context.hpp"

namespace klvk
{

namespace
{

// A pixel of space around each glyph so that sampling at the edge of one cannot
// reach into its neighbour.
constexpr u32 kPadding = 1;

}  // namespace

GlyphAtlas::GlyphAtlas(
    DeviceContext& context,
    const FontFace& face,
    u32 pixel_size,
    edt::Vec2<u32> texture_size,
    size_t frames_in_flight)
    : context_(&context),
      face_(&face),
      pixel_size_(pixel_size),
      line_height_(face.GetLineHeight(pixel_size)),
      texture_size_(texture_size),
      texture_(Texture::CreateEmptyR8(context, texture_size)),
      staging_buffers_(frames_in_flight)
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
        RasterizedGlyph rasterized = face_->Rasterize(index, pixel_size_);

        Glyph glyph{
            .uv_min = {},
            .uv_max = {},
            .size = rasterized.size.Cast<f32>(),
            .bearing = rasterized.bearing,
            .advance = rasterized.advance,
        };

        // A space has an advance and no coverage: a glyph the atlas knows about
        // that occupies none of it.
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

        const edt::Vec2f texture_size = texture_size_.Cast<f32>();
        glyph.uv_min = position.Cast<f32>() / texture_size;
        glyph.uv_max = (position + rasterized.size).Cast<f32>() / texture_size;
        glyphs_.emplace(codepoint, glyph);

        pending_.push_back({
            .offset = position,
            .size = rasterized.size,
            .coverage = std::move(rasterized.coverage),
        });
    }
    return packed_everything;
}

void GlyphAtlas::EnsureStagingCapacity(size_t frame_index, size_t bytes)
{
    GpuBuffer& buffer = staging_buffers_[frame_index];
    if (buffer.IsValid() && buffer.GetSize() >= bytes) return;

    size_t capacity = std::max<size_t>(buffer.GetSize(), 4096);
    while (capacity < bytes) capacity *= 2;

    context_->WaitIdle();
    buffer =
        GpuBuffer{*context_, vk::BufferUsageFlagBits::eTransferSrc, capacity, GpuBufferHostAccess::SequentialWrite};
}

void GlyphAtlas::RecordPendingUploads(vk::CommandBuffer command_buffer, size_t frame_index)
{
    if (pending_.empty()) return;

    size_t total = 0;
    for (const PendingGlyph& glyph : pending_)
    {
        total += glyph.coverage.size();
    }

    // One staging buffer per frame slot: the copy of an earlier frame may not have
    // run yet, and overwriting its bytes would corrupt what it uploads.
    EnsureStagingCapacity(frame_index, total);
    GpuBuffer& staging = staging_buffers_[frame_index];

    std::vector<Texture::RegionUpdate> regions;
    regions.reserve(pending_.size());

    vk::DeviceSize offset = 0;
    for (const PendingGlyph& glyph : pending_)
    {
        staging.Write(std::as_bytes(std::span{glyph.coverage}), offset);
        regions.push_back({.buffer_offset = offset, .offset = glyph.offset, .size = glyph.size});
        offset += glyph.coverage.size();
    }

    texture_->RecordRegionUpdates(command_buffer, staging.GetHandle(), regions);
    pending_.clear();
}

std::optional<GlyphAtlas::Glyph> GlyphAtlas::Find(char32_t codepoint) const
{
    const auto found = glyphs_.find(codepoint);
    if (found == glyphs_.end()) return std::nullopt;
    return found->second;
}

}  // namespace klvk
