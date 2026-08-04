#include "klvk/text/font_face.hpp"

#include <freetype/freetype.h>
#include <freetype/ftoutln.h>

#include <fstream>

#include "klvk/error_handling.hpp"

namespace klvk
{

namespace
{

// FreeType reports positions in 26.6 fixed point almost everywhere.
[[nodiscard]] constexpr f32 FromFixed266(FT_Pos value)
{
    return static_cast<f32>(value) / 64.f;
}

// The decomposer walks the outline and appends to the vector behind `user`.
struct OutlineSink
{
    static int Move(const FT_Vector* to, void* user)
    {
        Append(user, {.kind = GlyphOutlineCommand::Kind::Move, .points = {Point(to)}});
        return 0;
    }

    static int Line(const FT_Vector* to, void* user)
    {
        Append(user, {.kind = GlyphOutlineCommand::Kind::Line, .points = {Point(to)}});
        return 0;
    }

    static int Conic(const FT_Vector* control, const FT_Vector* to, void* user)
    {
        Append(user, {.kind = GlyphOutlineCommand::Kind::Quadratic, .points = {Point(control), Point(to)}});
        return 0;
    }

    static int Cubic(const FT_Vector* control0, const FT_Vector* control1, const FT_Vector* to, void* user)
    {
        Append(
            user,
            {.kind = GlyphOutlineCommand::Kind::Cubic, .points = {Point(control0), Point(control1), Point(to)}});
        return 0;
    }

private:
    [[nodiscard]] static edt::Vec2f Point(const FT_Vector* vector)
    {
        return {static_cast<f32>(vector->x), static_cast<f32>(vector->y)};
    }

    static void Append(void* user, GlyphOutlineCommand command)
    {
        static_cast<std::vector<GlyphOutlineCommand>*>(user)->push_back(command);
    }
};

}  // namespace

struct FontFace::Internal
{
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    // The size the face is currently configured for, so repeated rasterization at
    // one size does not keep reconfiguring it.
    u32 pixel_size = 0;

    ~Internal()
    {
        if (face != nullptr) FT_Done_Face(face);
        if (library != nullptr) FT_Done_FreeType(library);
    }

    void UsePixelSize(u32 size)
    {
        if (pixel_size == size) return;
        ErrorHandling::Ensure(FT_Set_Pixel_Sizes(face, 0, size) == 0, "Cannot use pixel size {}", size);
        pixel_size = size;
    }
};

std::unique_ptr<FontFace> FontFace::FromFile(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    ErrorHandling::Ensure(file.good(), "Cannot open font {}", path.string());

    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0);
    std::vector<u8> bytes(static_cast<size_t>(size));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): reading bytes is what this is
    ErrorHandling::Ensure(
        file.read(reinterpret_cast<char*>(bytes.data()), size).good(),
        "Cannot read {}",
        path.string());

    return FromMemory(std::move(bytes));
}

std::unique_ptr<FontFace> FontFace::FromMemory(std::vector<u8> bytes)
{
    auto font = std::unique_ptr<FontFace>(new FontFace());
    font->bytes_ = std::move(bytes);
    font->internal_ = std::make_unique<Internal>();

    ErrorHandling::Ensure(FT_Init_FreeType(&font->internal_->library) == 0, "Cannot initialize FreeType");
    const FT_Error error = FT_New_Memory_Face(
        font->internal_->library,
        font->bytes_.data(),
        static_cast<FT_Long>(font->bytes_.size()),
        0,
        &font->internal_->face);
    ErrorHandling::Ensure(error == 0, "Cannot read the font: FreeType error {}", static_cast<int>(error));

    return font;
}

FontFace::~FontFace() = default;

f32 FontFace::GetUnitsPerEm() const noexcept
{
    return static_cast<f32>(internal_->face->units_per_EM);
}

u32 FontFace::GetGlyphIndex(char32_t codepoint) const
{
    return FT_Get_Char_Index(internal_->face, static_cast<FT_ULong>(codepoint));
}

std::vector<GlyphOutlineCommand> FontFace::GetGlyphOutline(u32 glyph_index) const
{
    // Unscaled and unhinted: the outline is wanted in font units, so that one
    // conversion serves every size it is later drawn at.
    ErrorHandling::Ensure(
        FT_Load_Glyph(internal_->face, glyph_index, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) == 0,
        "Cannot load glyph {}",
        glyph_index);

    std::vector<GlyphOutlineCommand> commands;
    if (internal_->face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return commands;

    constexpr FT_Outline_Funcs functions{
        .move_to = &OutlineSink::Move,
        .line_to = &OutlineSink::Line,
        .conic_to = &OutlineSink::Conic,
        .cubic_to = &OutlineSink::Cubic,
        .shift = 0,
        .delta = 0,
    };
    ErrorHandling::Ensure(
        FT_Outline_Decompose(&internal_->face->glyph->outline, &functions, &commands) == 0,
        "Cannot read the outline of glyph {}",
        glyph_index);

    // FreeType closes each contour implicitly; the command stream says so.
    if (!commands.empty()) commands.push_back({.kind = GlyphOutlineCommand::Kind::Close});
    return commands;
}

f32 FontFace::GetAdvance(u32 glyph_index) const
{
    ErrorHandling::Ensure(
        FT_Load_Glyph(internal_->face, glyph_index, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) == 0,
        "Cannot load glyph {}",
        glyph_index);
    return static_cast<f32>(internal_->face->glyph->metrics.horiAdvance);
}

RasterizedGlyph FontFace::Rasterize(u32 glyph_index, u32 pixel_size) const
{
    internal_->UsePixelSize(pixel_size);
    ErrorHandling::Ensure(
        FT_Load_Glyph(internal_->face, glyph_index, FT_LOAD_RENDER) == 0,
        "Cannot rasterize glyph {}",
        glyph_index);

    const FT_GlyphSlot slot = internal_->face->glyph;
    RasterizedGlyph glyph{
        .size = {slot->bitmap.width, slot->bitmap.rows},
        .bearing = {static_cast<f32>(slot->bitmap_left), static_cast<f32>(slot->bitmap_top)},
        .advance = FromFixed266(slot->advance.x),
        .coverage = {},
    };

    const size_t pixel_count = static_cast<size_t>(glyph.size.x()) * glyph.size.y();
    if (pixel_count == 0) return glyph;

    // FreeType rows can be padded, so they are copied one at a time rather than
    // as one block.
    glyph.coverage.resize(pixel_count);
    for (u32 row = 0; row != glyph.size.y(); ++row)
    {
        const u8* source = slot->bitmap.buffer + (static_cast<ptrdiff_t>(row) * slot->bitmap.pitch);
        std::copy_n(source, glyph.size.x(), glyph.coverage.begin() + (static_cast<ptrdiff_t>(row) * glyph.size.x()));
    }
    return glyph;
}

f32 FontFace::GetLineHeight(u32 pixel_size) const
{
    internal_->UsePixelSize(pixel_size);
    return FromFixed266(internal_->face->size->metrics.height);
}

}  // namespace klvk
