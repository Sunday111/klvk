#ifdef _WIN32

#include <functional>

#include "Windows.h"  // IWYU pragma: keep
#include "clipboard.hpp"
#include "klvk/template/on_scope_leave.hpp"

void Clipboard::AddImage(edt::Vec2<size_t> size, std::span<const edt::Vec4u8> pixels)
{
    size_t image_size = size.x() * size.y();
    HGLOBAL hMem = GlobalAlloc(GHND, static_cast<DWORD>(sizeof(BITMAPINFOHEADER) + image_size * 4));
    const auto free_mem = klvk::OnScopeLeave(std::bind_front(GlobalFree, hMem));

    {
        auto mem = reinterpret_cast<uint8_t*>(GlobalLock(hMem));  // NOLINT
        klvk::OnScopeLeave(std::bind_front(GlobalUnlock, mem));

        auto& header = *reinterpret_cast<BITMAPINFOHEADER*>(mem);  // NOLINT
        ZeroMemory(&header, sizeof(header));
        header.biSize = sizeof(BITMAPINFOHEADER);
        header.biWidth = static_cast<LONG>(size.x());
        header.biHeight = static_cast<LONG>(size.y());
        header.biPlanes = 1;
        header.biBitCount = 32;
        header.biCompression = BI_RGB;

        const std::span<edt::Vec4u8> dst{
            reinterpret_cast<edt::Vec4u8*>(mem + sizeof(header)),  // NOLINT
            image_size,
        };

        // RGB -> BGR
        for (size_t y = 0; y != size.y(); ++y)
        {
            size_t offset = y * size.x();
            for (size_t x = 0; x != size.x(); ++x)
            {
                size_t index = offset + x;
                auto& from = pixels[index];
                auto& to = dst[index];
                to.x() = from.z();
                to.y() = from.y();
                to.z() = from.x();
                to.w() = from.w();
            }
        }
    }

    OpenClipboard(nullptr);
    EmptyClipboard();
    // Windows takes ownership — don't free it yourself
    SetClipboardData(CF_DIB, std::exchange(hMem, nullptr));
    CloseClipboard();
}

#endif
