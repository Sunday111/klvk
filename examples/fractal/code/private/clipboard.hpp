#pragma once

#include <span>

#include "EverydayTools/Math/Matrix.hpp"

class Clipboard
{
public:
    // Send bitmap to clipboard. Pixels are expected to be in RGBA format
    static void AddImage(edt::Vec2<size_t> size, std::span<const edt::Vec4u8> pixels);
};
