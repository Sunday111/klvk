#pragma once

#include <cassert>
#include <optional>

#include "EverydayTools/Math/Math.hpp"
#include "klvk/math/rotator.hpp"

namespace klvk
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

class Camera3d
{
public:
    [[nodiscard]] static constexpr edt::Mat4f
    LookAtRH(const edt::Vec3f& eye, const edt::Vec3f& dir, const edt::Vec3f& up) noexcept
    {
        const auto& forward = dir;
        const auto right = forward.Cross(up);
        const auto actual_up = right.Cross(forward);
        auto result = Mat4f::Identity();
        result.SetColumn(0, Vec4f{right, -right.Dot(eye)});
        result.SetColumn(1, Vec4f{actual_up, -actual_up.Dot(eye)});
        result.SetColumn(2, Vec4f{-forward, forward.Dot(eye)});
        return result;
    }

    [[nodiscard]] static constexpr edt::Mat4f
    MakeViewMatrix(const edt::Vec3f& eye, const edt::Vec3f& dir, const edt::Vec3f& up) noexcept
    {
        auto result = LookAtRH(eye, dir, up);
        result.SetColumn(0, -result.GetColumn(0));
        return result;
    }

    // Right-handed perspective matrix with Vulkan's zero-to-one depth range.
    [[nodiscard]] static edt::Mat4f PerspectiveRH(float fovy, float aspect, float near, float far) noexcept
    {
        assert(std::abs(aspect - std::numeric_limits<float>::epsilon()) > 0.f);
        const float tan_half_fovy = std::tan(fovy / 2.f);
        edt::Mat4f result{};
        result.At<0, 0>() = 1.f / (aspect * tan_half_fovy);
        result.At<1, 1>() = 1.f / tan_half_fovy;
        result.At<2, 2>() = -far / (far - near);
        result.At<2, 3>() = -1.f;
        result.At<3, 2>() = -(far * near) / (far - near);
        return result;
    }

    constexpr Camera3d() noexcept = default;
    constexpr Camera3d(const Camera3d&) noexcept = default;
    constexpr Camera3d(Camera3d&&) noexcept = default;
    constexpr Camera3d& operator=(const Camera3d&) noexcept = default;
    constexpr Camera3d& operator=(Camera3d&&) noexcept = default;
    constexpr Camera3d(const Vec3f& eye, const Rotator& rotation) noexcept : rotation_(rotation), eye_(eye) {}

    bool Widget();

    [[nodiscard]] const Rotator& GetRotation() const noexcept { return rotation_; }
    void SetRotation(const Rotator& rotator) noexcept
    {
        rotation_ = rotator;
        view_cache_.reset();
    }

    [[nodiscard]] const Vec3f& GetEye() const noexcept { return eye_; }
    void SetEye(const Vec3f& eye) noexcept
    {
        eye_ = eye;
        view_cache_.reset();
    }

    [[nodiscard]] const Vec3f& GetForwardAxis() const noexcept { return GetViewCache().forward; }
    [[nodiscard]] const Vec3f& GetRightAxis() const noexcept { return GetViewCache().right; }
    [[nodiscard]] const Vec3f& GetUpAxis() const noexcept { return GetViewCache().up; }

    // Stored transposed to match EverydayTools' row-major representation.
    [[nodiscard]] const Mat4f& GetViewMatrix() const noexcept { return GetViewCache().view_matrix; }

    [[nodiscard]] edt::Mat4f GetProjectionMatrix(float aspect) const noexcept
    {
        return PerspectiveRH(edt::Math::DegToRad(fov_), aspect, near_, far_);
    }

    [[nodiscard]] constexpr float GetNear() const noexcept { return near_; }
    constexpr void SetNear(float near) noexcept { near_ = near; }
    [[nodiscard]] constexpr float GetFar() const noexcept { return far_; }
    constexpr void SetFar(float far) noexcept { far_ = far; }
    [[nodiscard]] constexpr float GetFOV() const noexcept { return fov_; }
    constexpr void SetFOV(float fov) noexcept { fov_ = fov; }

private:
    struct ViewCache
    {
        Vec3f forward;
        Vec3f right;
        Vec3f up;
        Mat4f view_matrix;
    };

    [[nodiscard]] const ViewCache& GetViewCache() const noexcept
    {
        if (!view_cache_.has_value())
        {
            view_cache_ = ViewCache{};
            auto& cache = *view_cache_;
            const Mat4f rotation_matrix = rotation_.ToMatrix();
            edt::Math::ToBasisVectors(rotation_matrix, &cache.forward, &cache.right, &cache.up);
            cache.view_matrix = MakeViewMatrix(eye_, cache.forward, cache.up);
        }
        return *view_cache_;
    }

    mutable std::optional<ViewCache> view_cache_;
    float near_ = 0.1f;
    float far_ = 100.f;
    float fov_ = 45.f;
    Rotator rotation_;
    Vec3f eye_;
};

}  // namespace klvk
