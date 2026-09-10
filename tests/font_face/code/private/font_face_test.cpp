#include "klvk/text/font_face.hpp"

#include <array>
#include <string_view>

#include "klvk/error_handling.hpp"

int main()
{
    return klvk::ErrorHandling::InvokeAndCatchAll(
        []
        {
            constexpr std::string_view font_data = R"(STARTFONT 2.1
FONT -test-fixed-medium-r-normal--8-80-75-75-c-80-iso10646-1
SIZE 8 75 75
FONTBOUNDINGBOX 8 8 0 0
STARTPROPERTIES 2
FONT_ASCENT 8
FONT_DESCENT 0
ENDPROPERTIES
CHARS 1
STARTCHAR A
ENCODING 65
SWIDTH 500 0
DWIDTH 8 0
BBX 8 8 0 0
BITMAP
81
42
24
18
FF
00
AA
55
ENDCHAR
ENDFONT
)";
            const auto face = klvk::FontFace::FromMemory(std::vector<u8>(font_data.begin(), font_data.end()));
            const auto glyph = face->Rasterize(face->GetGlyphIndex(U'A'), 8);
            klvk::ErrorHandling::Ensure(glyph.size == edt::Vec2<u32>{8, 8}, "Bitmap dimensions differ");
            constexpr std::array<u8, 8> rows{0x81, 0x42, 0x24, 0x18, 0xFF, 0x00, 0xAA, 0x55};
            klvk::ErrorHandling::Ensure(glyph.coverage.size() == 64, "Bitmap coverage size differs");
            for (size_t row = 0; row != rows.size(); ++row)
            {
                for (size_t column = 0; column != 8; ++column)
                {
                    const u8 expected = (rows[row] & (0x80U >> column)) != 0 ? 255 : 0;
                    klvk::ErrorHandling::Ensure(
                        glyph.coverage[row * 8 + column] == expected,
                        "Monochrome coverage differs");
                }
            }
            for (u32 bits_per_pixel : {2U, 4U, 8U})
            {
                const u32 maximum = (1U << bits_per_pixel) - 1;
                std::string packed_font{font_data};
                const auto size_offset = packed_font.find("SIZE 8 75 75");
                packed_font.replace(
                    size_offset,
                    std::string_view{"SIZE 8 75 75"}.size(),
                    fmt::format("SIZE 8 75 75 {}", bits_per_pixel));
                std::string bitmap;
                std::vector<u8> expected;
                for (u32 row = 0; row != 8; ++row)
                {
                    u32 packed = 0;
                    for (u32 column = 0; column != 8; ++column)
                    {
                        const u32 sample = (row * 8 + column) % (maximum + 1);
                        expected.push_back(static_cast<u8>(sample * 255 / maximum));
                        packed = (packed << bits_per_pixel) | sample;
                        if ((column + 1) % (8 / bits_per_pixel) == 0)
                        {
                            bitmap += fmt::format("{:02X}", packed);
                            packed = 0;
                        }
                    }
                    bitmap += '\n';
                }
                const auto bitmap_offset = packed_font.find("BITMAP\n") + std::string_view{"BITMAP\n"}.size();
                packed_font.replace(bitmap_offset, packed_font.find("ENDCHAR") - bitmap_offset, bitmap);
                const auto packed_face =
                    klvk::FontFace::FromMemory(std::vector<u8>(packed_font.begin(), packed_font.end()));
                const auto packed_glyph = packed_face->Rasterize(packed_face->GetGlyphIndex(U'A'), 8);
                klvk::ErrorHandling::Ensure(packed_glyph.coverage == expected, "Packed grayscale coverage differs");
            }
        });
}
