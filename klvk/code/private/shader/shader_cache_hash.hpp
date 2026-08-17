#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "klvk/integral_aliases.hpp"

namespace klvk
{

class ShaderCacheHash
{
public:
    ShaderCacheHash() = delete;

    template <typename T>
    static u64 Value(u64 hash, const T& value)
    {
        return Bytes(hash, &value, sizeof(value));
    }

    static u64 String(u64 hash, std::string_view text) { return Bytes(hash, text.data(), text.size()); }
    static u64 String(std::string_view text) { return String(kOffset, text); }
    static u64 Words(std::span<const u32> words) { return Bytes(kOffset, words.data(), words.size_bytes()); }

private:
    static constexpr u64 kOffset = 14695981039346656037ull;
    static constexpr u64 kPrime = 1099511628211ull;

    static u64 Bytes(u64 hash, const void* bytes, size_t size)
    {
        const auto* data = static_cast<const u8*>(bytes);
        for (size_t i = 0; i != size; ++i)
        {
            hash ^= data[i];
            hash *= kPrime;
        }
        return hash;
    }
};

}  // namespace klvk
