#pragma once

#include <EverydayTools/Math/Math.hpp>
#include <EverydayTools/Math/Matrix.hpp>
#include <vector>

#include "klvk/camera/camera_2d.hpp"

using namespace edt::lazy_matrix_aliases;  // NOLINT

class FractalSettings
{
public:
    explicit FractalSettings(size_t num_colors);

    Vec2f fractal_constant{-0.4f, 0.6f};

    float time = 0.0f;
    bool use_current_time = true;
    int color_seed = 1234;
    bool interpolate_colors = true;
    bool interpolate_with_hsv = true;
    std::vector<Vec3f> colors;
    std::vector<float> color_positions;
    bool inside_out_space = false;
    bool changed = true;
    int color_mode = 0;
    Vec2f fractal_power{2.f, 0.f};
    klvk::Camera2d camera;
    klvk::Viewport viewport;
    size_t num_colors;

    void RandomizeColors();

    void SetCurrentTime(float current_time)
    {
        if (use_current_time)
        {
            time = current_time;
            changed = true;
        }
    }

    void SetViewport(const klvk::Viewport& new_viewport)
    {
        if (new_viewport != viewport)
        {
            viewport = new_viewport;
            changed = true;
        }
    }

    void DistributePositionsUniformly();

    void DrawGUI();

    Vec3f LerpColors(Vec3f a, Vec3f b, float t) const;

    template <typename Callback>
    void ComputeColors(size_t n, Callback cb) const
    {
        std::vector<size_t> indices(colors.size());
        for (size_t i = 0; i != colors.size(); ++i) indices[i] = i;

        constexpr std::less pred{};
        auto proj = [&](size_t i) -> const float&
        {
            return color_positions[i];
        };

        std::ranges::sort(indices, pred, proj);

        for (size_t i = 0; i != n; ++i)
        {
            const float fi = static_cast<float>(i) / static_cast<float>(n - 1);
            size_t left = indices.front();
            size_t right = indices.back();

            for (size_t j = 1; j != indices.size(); ++j)
            {
                size_t k = indices[j];
                if (color_positions[k] > fi)
                {
                    right = k;
                    left = indices[j - 1];
                    break;
                }
            }

            if (interpolate_colors)
            {
                float t = (fi - color_positions[left]) / (color_positions[right] - color_positions[left]);
                auto color = LerpColors(colors[left], colors[right], t);
                cb(i, color);
            }
            else
            {
                bool use_left = fi - color_positions[left] < color_positions[right] - fi;
                auto color = colors[use_left ? left : right];
                cb(i, color);
            }
        }
    }
};
