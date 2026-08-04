#pragma once

#include <optional>
#include <span>
#include <vector>

#include "edt/math/matrix.hpp"
#include "klvk/integral_aliases.hpp"

namespace klvk
{

// An image after decoding: four channels, one byte each, rows packed top to
// bottom with no padding. Four channels whatever the file held, so nothing
// downstream branches on what arrived.
struct DecodedImage
{
    edt::Vec2<u32> size{};
    std::vector<u8> pixels;
};

// Turns encoded bytes - a PNG, a JPEG, whatever the decoder in force understands
// - into pixels. Nothing when the bytes are not an image it can read, which a
// truncated or unrecognized file is.
//
// This declaration is the seam. Exactly one translation unit implements it, and
// that is the only place in klvk that names a decoding library at all, so
// replacing the decoder is replacing that file - or dropping it and linking
// another module that defines this function. Nothing else has to change, because
// nothing else knows what the decoder is.
[[nodiscard]] std::optional<DecodedImage> DecodeImage(std::span<const u8> encoded);

}  // namespace klvk
