#include "klvk/error_handling.hpp"
#include "klvk/texture/procedural_texture_generator.hpp"

int main()
{
    return klvk::ErrorHandling::InvokeAndCatchAll(
        []
        {
            using klvk::ErrorHandling;
            using klvk::ProceduralTextureGenerator;
            const std::vector<u8> expected{0, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 255, 0};
            ErrorHandling::Ensure(ProceduralTextureGenerator::CircleMask({4, 4}) == expected, "Pixel centres differ");
            for (size_t factor : {size_t{1}, size_t{2}, size_t{8}})
            {
                for (edt::Vec2<size_t> size : {edt::Vec2<size_t>{4, 4}, edt::Vec2<size_t>{8, 4}})
                {
                    const auto mask = ProceduralTextureGenerator::CircleMask(size, factor);
                    for (size_t y = 0; y != size.y(); ++y)
                    {
                        for (size_t x = 0; x != size.x(); ++x)
                        {
                            const auto value = mask[y * size.x() + x];
                            ErrorHandling::Ensure(
                                value == mask[y * size.x() + size.x() - 1 - x],
                                "Horizontal asymmetry");
                            ErrorHandling::Ensure(
                                value == mask[(size.y() - 1 - y) * size.x() + x],
                                "Vertical asymmetry");
                        }
                    }
                }
            }
            const auto triangle = ProceduralTextureGenerator::TriangleMask({1, 1}, 2);
            ErrorHandling::Ensure(triangle == std::vector<u8>{63}, "Triangle samples do not cover the pixel");
            ErrorHandling::Ensure(ProceduralTextureGenerator::CircleMask({0, 4}).empty(), "Empty image differs");
            bool rejected = false;
            try
            {
                static_cast<void>(ProceduralTextureGenerator::CircleMask({4, 4}, 0));
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            ErrorHandling::Ensure(rejected, "Zero supersampling factor accepted");
        });
}
