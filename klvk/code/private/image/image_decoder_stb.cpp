// The stb-backed decoder. This is the only file in klvk that names a decoding
// library; see image_decoder.hpp for what replacing it involves.

#include <stb_image.h>

#include "klvk/image/image_decoder.hpp"

namespace klvk
{

std::optional<DecodedImage> DecodeImage(std::span<const u8> encoded)
{
    constexpr int kChannels = 4;

    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    u8* pixels = stbi_load_from_memory(
        encoded.data(),
        static_cast<int>(encoded.size()),
        &width,
        &height,
        &channels_in_file,
        kChannels);
    if (pixels == nullptr) return std::nullopt;

    const auto count = static_cast<size_t>(width) * static_cast<size_t>(height) * kChannels;
    DecodedImage image{
        .size = {static_cast<u32>(width), static_cast<u32>(height)},
        .pixels = std::vector<u8>{pixels, pixels + count},
    };
    stbi_image_free(pixels);
    return image;
}

}  // namespace klvk
