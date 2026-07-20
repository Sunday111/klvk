#include "fractal_settings.hpp"

#include <bit>
#include <klvk/error_handling.hpp>
#include <klvk/integral_aliases.hpp>
#include <klvk/template/on_scope_leave.hpp>
#include <klvk/ui/simple_type_widget.hpp>
#include <optional>
#include <random>

#include "imgui.h"

FractalSettings::FractalSettings(size_t num_colors_) : num_colors(num_colors_)
{
    klvk::ErrorHandling::Ensure(num_colors > 1, "Need at least two colors");
    color_positions.resize(num_colors, 0);
    colors.resize(num_colors, edt::Vec3f{});
}

void FractalSettings::RandomizeColors()
{
    std::mt19937 rnd(static_cast<unsigned>(color_seed));
    std::uniform_real_distribution<float> color_distr(0, 1.0f);

    for (auto& color : colors)
    {
        color = color.Transform([&](float) { return color_distr(rnd); });
    }

    changed = true;
}

void FractalSettings::DistributePositionsUniformly()
{
    float delta = 1.f / static_cast<float>(num_colors - 1);
    for (size_t i = 1; i != num_colors - 1; ++i)
    {
        color_positions[i] = static_cast<float>(i) * delta;
    }

    color_positions.back() = 1.f;
    changed = true;
}

[[nodiscard]] constexpr edt::Vec3f rgb2hsv(edt::Vec3f in)
{
    edt::Vec3f out;

    const float min = in.Min();
    const float max = in.Max();

    out[2] = max;  // v
    float delta = max - min;
    if (delta < 0.00001f)
    {
        out[1] = 0;
        out[0] = 0;  // undefined, maybe nan?
        return out;
    }
    if (max > 0.0f)
    {                            // NOTE: if Max is == 0, this divide would cause a crash
        out[1] = (delta / max);  // s
    }
    else
    {
        // if max is 0, then r = g = b = 0
        // s = 0, h is undefined
        out[1] = 0.0;
        out[0] = NAN;  // its now undefined
        return out;
    }

    if (in[0] >= max)  // > is bogus, just keeps compilor happy
    {
        out[0] = (in[1] - in[2]) / delta;  // between yellow & magenta
    }
    else if (in[1] >= max)
    {
        out[0] = 2 + (in[2] - in[0]) / delta;  // between cyan & yellow
    }
    else
    {
        out[0] = 4 + (in[0] - in[1]) / delta;  // between magenta & cyan
    }

    out[0] *= 60.f;  // degrees

    if (out[0] < 0.f) out[0] += 360.f;

    return out;
}

[[nodiscard]] constexpr edt::Vec3f hsv2rgb(edt::Vec3f in)
{
    edt::Vec3f out;

    if (in[1] <= 0.f)
    {  // < is bogus, just shuts up warnings
        return out + in[2];
    }
    float hh = in[0];
    if (hh >= 360.f) hh = 0.f;
    hh /= 60.f;
    const auto i = static_cast<u8>(hh);
    float ff = hh - static_cast<float>(i);
    float p = in[2] * (1 - in[1]);
    float q = in[2] * (1 - (in[1] * ff));
    float t = in[2] * (1 - (in[1] * (1 - ff)));

    switch (i)
    {
    case 0:
        out[0] = in[2];
        out[1] = t;
        out[2] = p;
        break;
    case 1:
        out[0] = q;
        out[1] = in[2];
        out[2] = p;
        break;
    case 2:
        out[0] = p;
        out[1] = in[2];
        out[2] = t;
        break;

    case 3:
        out[0] = p;
        out[1] = q;
        out[2] = in[2];
        break;
    case 4:
        out[0] = t;
        out[1] = p;
        out[2] = in[2];
        break;
    case 5:
    default:
        out[0] = in[2];
        out[1] = p;
        out[2] = q;
        break;
    }
    return out;
}

[[nodiscard]] edt::Vec3f LerpHSV(edt::Vec3f a, edt::Vec3f b, float t) noexcept
{
    float x = a[0] / 360, y = b[0] / 360;
    float delta = std::fmod(y - x + 1, 1.f);
    if (delta > 0.5f) delta -= 1;
    float h = std::fmod(x + t * delta, 1.f);
    if (h < 0) h += 1;
    edt::Vec3f result = edt::Math::Lerp(a, b, t);
    result[0] = h * 360.f;
    return result;
}

edt::Vec3f FractalSettings::LerpColors(edt::Vec3f from, edt::Vec3f to, float t) const
{
    if (interpolate_with_hsv)
    {
        return hsv2rgb(LerpHSV(rgb2hsv(from), rgb2hsv(to), t));
    }

    return edt::Math::Lerp(from, to, t);
}

void FractalSettings::DrawGUI()
{
    if (ImGui::Button("I'm lost!"))
    {
        *this = FractalSettings{num_colors};
        DistributePositionsUniformly();
        RandomizeColors();
        changed = true;
    }

    changed |= ImGui::Checkbox("Inside out space", &inside_out_space);
    changed |= klvk::SimpleTypeWidget("Fractal Constant", fractal_constant);
    changed |= klvk::SimpleTypeWidget("Fractal Power", fractal_power);
    changed |= ImGui::SliderInt("Color Mode", &color_mode, 0, 4);

    if (ImGui::CollapsingHeader("Colors"))
    {
        std::optional<size_t> index_to_remove, index_to_add;
        for (size_t color_index = 0; color_index != colors.size(); ++color_index)
        {
            constexpr int color_edit_flags =
                ImGuiColorEditFlags_DefaultOptions_ | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
            auto& color = colors[color_index];
            ImGui::PushID(&color);
            auto pop_on_exit = klvk::OnScopeLeave(&ImGui::PopID);
            changed |= ImGui::ColorEdit3("Color", color.data(), color_edit_flags);

            if (color_index != 0)
            {
                ImGui::SameLine();
                if (ImGui::Button("+")) index_to_add = color_index;
            }

            if (color_index != 0 && color_index != colors.size() - 1)
            {
                ImGui::SameLine();
                if (ImGui::Button("-")) index_to_remove = color_index;

                ImGui::SameLine();
                changed |= ImGui::SliderFloat("Pos", &color_positions[color_index], 0.0f, 1.f);
            }
        }

        if (index_to_remove)
        {
            colors.erase(std::next(colors.begin(), static_cast<int>(*index_to_remove)));
            color_positions.erase(std::next(color_positions.begin(), static_cast<int>(*index_to_remove)));
        }

        if (index_to_add && index_to_add != 0)
        {
            size_t right = *index_to_add;
            size_t left = right - 1;
            auto color = LerpColors(colors[left], colors[right], 0.5f);
            colors.insert(std::next(colors.begin(), static_cast<int>(right)), color);

            float position = std::lerp(color_positions[left], color_positions[right], 0.5f);
            color_positions.insert(std::next(color_positions.begin(), static_cast<int>(right)), position);
        }

        changed |= ImGui::Checkbox("Interpolate colors", &interpolate_colors);
        if (interpolate_colors)
        {
            changed |= ImGui::Checkbox("Interpolate in HSV space", &interpolate_with_hsv);
        }

        bool randomize = false;

        if (ImGui::InputInt("Color Seed:", &color_seed))
        {
            randomize = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Randomize"))
        {
            randomize = true;
            color_seed = std::bit_cast<int>(std::random_device()());
        }

        if (randomize)
        {
            RandomizeColors();
            changed = true;
        }
    }
}
