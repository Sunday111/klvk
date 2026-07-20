#pragma once

#include <bit>
#include <string_view>

#include "klvk/integral_aliases.hpp"

namespace klvk
{
using ConstexprStringHasher = decltype([](const std::string_view& str)
    {
        size_t r = 5381;
        for (const char c: str)
        {
            r = ((r << 5) + r) + std::bit_cast<u8>(c);
        }

        return r;
    });
}  // namespace klvk
