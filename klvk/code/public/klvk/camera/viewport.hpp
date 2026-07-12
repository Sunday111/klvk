#pragma once

#include "EverydayTools/Math/Matrix.hpp"

namespace klvk
{
class Viewport
{
public:
    [[nodiscard]] constexpr float GetAspect() const
    {
        auto s = size.Cast<float>();
        return s.x() / s.y();
    }

    [[nodiscard]] static constexpr Viewport FromWindowSize(const edt::Vec2u32& window_size)
    {
        Viewport v;
        v.MatchWindowSize(window_size);
        return v;
    }

    constexpr void MatchWindowSize(const edt::Vec2u32& window_size)
    {
        position = {};
        size = window_size;
    }

    [[nodiscard]] constexpr bool operator==(const Viewport& rhs) const noexcept
    {
        return position == rhs.position && size == rhs.size;
    }

    [[nodiscard]] constexpr bool operator!=(const Viewport& rhs) const noexcept { return !(*this == rhs); }

    edt::Vec2u32 position;
    edt::Vec2u32 size;
};
}  // namespace klvk
