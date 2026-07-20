#pragma once

#include <EverydayTools/Math/FloatRange.hpp>
#include <EverydayTools/Math/Math.hpp>

#include "klvk/integral_aliases.hpp"
#include "viewport.hpp"

namespace klvk
{

class Camera2d;
class Viewport;

enum class AspectRatioPolicy : u8
{
    Stretch,
    ShrinkToFit,
    GrowToFill
};

class RenderTransforms2d
{
public:
    constexpr void Update(
        const Camera2d camera,
        const Viewport& vierport,
        AspectRatioPolicy aspect_ratio_policy = AspectRatioPolicy::ShrinkToFit);

    edt::Mat3f world_to_view{};
    edt::Mat3f view_to_screen{};
    edt::Mat3f world_to_screen{};

    edt::Mat3f view_to_world{};
    edt::Mat3f screen_to_view{};
    edt::Mat3f screen_to_world{};
};

class Camera2d
{
public:
    float zoom = 1.f;
    edt::Vec2f eye{};
};

constexpr void
RenderTransforms2d::Update(const Camera2d camera, const Viewport& viewport, AspectRatioPolicy aspect_ratio_policy)
{
    using edt::Math, edt::Vec2f;

    auto view_size = viewport.size.Cast<float>();
    auto view_pos = viewport.position.Cast<float>();

    edt::Vec2f half_camera_extent{1, 1};
    switch (aspect_ratio_policy)
    {
    case AspectRatioPolicy::Stretch:
        half_camera_extent = Vec2f{1, 1};
        break;
    case AspectRatioPolicy::ShrinkToFit:
        half_camera_extent = view_size / view_size.Min();
        break;
    case AspectRatioPolicy::GrowToFill:
        half_camera_extent = view_size / view_size.Max();
        break;
    }

    half_camera_extent /= camera.zoom;

    // Forward
    world_to_view = Math::MatMul(Math::ScaleMatrix(1.f / half_camera_extent), Math::TranslationMatrix(-camera.eye));
    view_to_screen = Math::MatMul(
        Math::TranslationMatrix(view_pos),
        Math::ScaleMatrix(view_size / 2),
        Math::TranslationMatrix(Vec2f{} + 1));
    world_to_screen = Math::MatMul(view_to_screen, world_to_view);

    // Backwards
    view_to_world = Math::MatMul(Math::TranslationMatrix(camera.eye), Math::ScaleMatrix(half_camera_extent));
    screen_to_view = Math::MatMul(
        Math::TranslationMatrix(Vec2f{} - 1),
        Math::ScaleMatrix(2 / view_size),
        Math::TranslationMatrix(-view_pos));
    screen_to_world = Math::MatMul(view_to_world, screen_to_view);
}

}  // namespace klvk
