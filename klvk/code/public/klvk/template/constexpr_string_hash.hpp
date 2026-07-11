#pragma once

#include <bit>
#include <string_view>

namespace klvk
{
using ConstexprStringHasher = decltype([](const std::string_view& str)
    {
        size_t r = 5381;
        for (const char c: str)
        {
            r = ((r << 5) + r) + std::bit_cast<uint8_t>(c);
        }

        return r;
    });
}  // namespace klvk
