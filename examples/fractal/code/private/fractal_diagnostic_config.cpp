#include "fractal_diagnostic_config.hpp"

#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>
#include <string>
#include <string_view>

#include "klvk/error_handling.hpp"

namespace
{

const nlohmann::json& RequiredField(const nlohmann::json& object, std::string_view name, std::string_view path)
{
    klvk::ErrorHandling::Ensure(object.contains(name), "{} is required", path);
    return object.at(name);
}

edt::Vec2f ReadVec2(const nlohmann::json& value, std::string_view path)
{
    klvk::ErrorHandling::Ensure(
        value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number(),
        "{} must be an array of two numbers, got {}",
        path,
        value.dump());
    const edt::Vec2f result{value[0].get<float>(), value[1].get<float>()};
    klvk::ErrorHandling::Ensure(
        std::isfinite(result.x()) && std::isfinite(result.y()),
        "{} components must be finite, got {}",
        path,
        value.dump());
    return result;
}

float ReadNumber(const nlohmann::json& value, std::string_view path)
{
    klvk::ErrorHandling::Ensure(value.is_number(), "{} must be a number, got {}", path, value.dump());
    const float result = value.get<float>();
    klvk::ErrorHandling::Ensure(std::isfinite(result), "{} must be finite, got {}", path, value.dump());
    return result;
}

}  // namespace

std::optional<FractalDiagnosticConfig> FractalDiagnosticConfig::FromJSON(const nlohmann::json* json)
{
    if (json == nullptr) return std::nullopt;

    klvk::ErrorHandling::Ensure(json->is_object(), "application must be an object, got {}", json->dump());
    FractalDiagnosticConfig config;

    if (json->contains("renderer"))
    {
        const nlohmann::json& value = json->at("renderer");
        klvk::ErrorHandling::Ensure(
            value.is_string(),
            "application.renderer must be one of simple_gpu, counting, or simple_cpu, got {}",
            value.dump());
        const std::string name = value.get<std::string>();
        if (name == "simple_gpu")
        {
            config.renderer = Renderer::SimpleGpu;
        }
        else if (name == "counting")
        {
            config.renderer = Renderer::Counting;
        }
        else if (name == "simple_cpu")
        {
            config.renderer = Renderer::SimpleCpu;
        }
        else
        {
            klvk::ErrorHandling::ThrowWithMessage(
                "application.renderer must be one of simple_gpu, counting, or simple_cpu, got {}",
                value.dump());
        }
    }

    if (json->contains("camera"))
    {
        const nlohmann::json& value = json->at("camera");
        klvk::ErrorHandling::Ensure(value.is_object(), "application.camera must be an object, got {}", value.dump());
        Camera camera;
        if (value.contains("eye")) camera.eye = ReadVec2(value.at("eye"), "application.camera.eye");
        if (value.contains("zoom"))
        {
            const float zoom = ReadNumber(value.at("zoom"), "application.camera.zoom");
            klvk::ErrorHandling::Ensure(
                zoom > 0.f,
                "application.camera.zoom must be a positive number, got {}",
                value.at("zoom").dump());
            camera.zoom = zoom;
        }
        config.camera = camera;
    }

    if (json->contains("color_mode"))
    {
        const nlohmann::json& value = json->at("color_mode");
        const bool in_range =
            (value.is_number_integer() && value.get<std::int64_t>() >= 0 && value.get<std::int64_t>() <= 4) ||
            (value.is_number_unsigned() && value.get<std::uint64_t>() <= 4);
        klvk::ErrorHandling::Ensure(
            in_range,
            "application.color_mode must be an integer from 0 through 4, got {}",
            value.dump());
        config.color_mode = value.get<int>();
    }

    if (json->contains("colors"))
    {
        const nlohmann::json& values = json->at("colors");
        klvk::ErrorHandling::Ensure(
            values.is_array() && values.size() >= 2,
            "application.colors must be an array of at least two RGB colors, got {}",
            values.dump());

        std::vector<edt::Vec3f> colors;
        colors.reserve(values.size());
        for (size_t index = 0; index != values.size(); ++index)
        {
            const nlohmann::json& value = values[index];
            const std::string path = fmt::format("application.colors[{}]", index);
            klvk::ErrorHandling::Ensure(
                value.is_array() && value.size() == 3 && value[0].is_number() && value[1].is_number() &&
                    value[2].is_number(),
                "{} must be an array of three numbers, got {}",
                path,
                value.dump());
            const edt::Vec3f color{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            klvk::ErrorHandling::Ensure(
                color.Min() >= 0.f && color.Max() <= 1.f,
                "{} components must be from 0 through 1, got {}",
                path,
                value.dump());
            colors.push_back(color);
        }
        config.colors = std::move(colors);
    }

    if (json->contains("show_interpolation_widget"))
    {
        const nlohmann::json& value = json->at("show_interpolation_widget");
        klvk::ErrorHandling::Ensure(
            value.is_boolean(),
            "application.show_interpolation_widget must be a boolean, got {}",
            value.dump());
        config.show_interpolation_widget = value.get<bool>();
    }

    if (json->contains("view_rotation_degrees"))
    {
        config.view_rotation_radians =
            ReadNumber(json->at("view_rotation_degrees"), "application.view_rotation_degrees") *
            std::numbers::pi_v<float> / 180.f;
    }

    if (json->contains("constant_animation"))
    {
        const nlohmann::json& value = json->at("constant_animation");
        klvk::ErrorHandling::Ensure(
            value.is_object(),
            "application.constant_animation must be an object, got {}",
            value.dump());

        ConstantAnimation animation{
            .center = ReadVec2(
                RequiredField(value, "center", "application.constant_animation.center"),
                "application.constant_animation.center"),
            .radius = ReadVec2(
                RequiredField(value, "radius", "application.constant_animation.radius"),
                "application.constant_animation.radius"),
            .period_seconds = ReadNumber(
                RequiredField(value, "period_seconds", "application.constant_animation.period_seconds"),
                "application.constant_animation.period_seconds"),
        };
        klvk::ErrorHandling::Ensure(
            animation.period_seconds > 0.f,
            "application.constant_animation.period_seconds must be a positive number, got {}",
            value.at("period_seconds").dump());
        if (value.contains("start_phase_turns"))
        {
            animation.start_phase_turns =
                ReadNumber(value.at("start_phase_turns"), "application.constant_animation.start_phase_turns");
        }
        config.constant_animation = animation;
    }

    return config;
}
