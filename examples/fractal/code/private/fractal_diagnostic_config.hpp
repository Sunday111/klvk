#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <vector>

#include "edt/math/matrix.hpp"

struct FractalDiagnosticConfig
{
    enum class Renderer : std::uint8_t
    {
        SimpleGpu,
        Counting,
        SimpleCpu,
    };

    struct Camera
    {
        std::optional<edt::Vec2f> eye;
        std::optional<float> zoom;
    };

    struct ConstantAnimation
    {
        edt::Vec2f center{};
        edt::Vec2f radius{};
        float period_seconds = 0.f;
        float start_phase_turns = 0.f;
    };

    [[nodiscard]] static std::optional<FractalDiagnosticConfig> FromJSON(const nlohmann::json* json);

    std::optional<Renderer> renderer;
    std::optional<Camera> camera;
    std::optional<int> color_mode;
    std::optional<std::vector<edt::Vec3f>> colors;
    std::optional<bool> show_interpolation_widget;
    std::optional<float> view_rotation_radians;
    std::optional<ConstantAnimation> constant_animation;
};
