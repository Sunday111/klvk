#pragma once

#include <cstddef>
#include <ranges>
#include <utility>

#include "edt/math/matrix.hpp"
#include "edt/ranges/array_indices2d.hpp"

namespace klvk
{

[[nodiscard]] inline constexpr auto VectorIndices2d(edt::Vec2<size_t> size)
{
    return edt::ArrayIndices2d(size.y(), size.x()) |
           std::views::transform([](std::pair<size_t, size_t> yx) { return edt::Vec2<size_t>{yx.second, yx.first}; });
}

}  // namespace klvk
