#pragma once

#include "EverydayTools/Math/Math.hpp"
#include "klvk/math/rotator.hpp"

namespace klvk
{
struct Transform
{
    [[nodiscard]] constexpr edt::Mat4f Matrix() const noexcept
    {
        return edt::Math::TranslationMatrix(translation)
            .MatMul(rotation.ToMatrix().MatMul(edt::Math::ScaleMatrix(scale)));
    }

    void Widget();

    edt::Vec3f translation{};
    Rotator rotation{};
    edt::Vec3f scale{1, 1, 1};
};
}  // namespace klvk
