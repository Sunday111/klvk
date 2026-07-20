#ifdef __linux__

#include "clipboard.hpp"

void Clipboard::AddImage([[maybe_unused]] edt::Vec2<size_t> size, [[maybe_unused]] std::span<const edt::Vec4u8> pixels)
{
    // Image clipboard support is not implemented on Linux.
}

#endif
