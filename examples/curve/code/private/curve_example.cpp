#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>
#include <cmath>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/rendering/curve_renderer_2d.hpp"
#include "klvk/window.hpp"

namespace
{
using namespace edt::lazy_matrix_aliases;  // NOLINT

struct Curve
{
    void Draw(Vec2f viewport_size, const Mat3f& world_to_view)
    {
        renderer->Draw(points, viewport_size, world_to_view, thickness, segment_pixel_length);
    }

    std::unique_ptr<klvk::CurveRenderer2d> renderer;
    std::vector<klvk::CurveRenderer2d::ControlPoint> points;
    float thickness = 5.f;
    float segment_pixel_length = 8.f;
};

class CurveApp : public klvk::Application
{
    void UpdateExtremeCurve(float time)
    {
        extreme_.points[0].position = {-0.8f, 0.60f};
        extreme_.points[1].position = {-0.4f, 0.60f};
        extreme_.points[2].position = {0.18f + 0.08f * std::sin(1.7f * time), 0.60f};
        extreme_.points[3].position = {0.30f + 0.22f * std::sin(2.3f * time), 0.48f + 0.15f * std::cos(1.9f * time)};
        extreme_.points[4].position = {0.18f - 0.08f * std::sin(1.3f * time), 0.36f};
        extreme_.points[5].position = {-0.4f, 0.36f};
        extreme_.points[6].position = {-0.8f, 0.36f};
    }

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Curve Example");
        spiral_.renderer = std::make_unique<klvk::CurveRenderer2d>(*this);
        secondary_.renderer = std::make_unique<klvk::CurveRenderer2d>(*this);
        extreme_.renderer = std::make_unique<klvk::CurveRenderer2d>(*this);

        auto points = edt::Math::GenerateSpiralPoints(100, {2.f, 2.f});
        for (size_t i = 0; i != points.size(); ++i)
        {
            constexpr float modulation = 0.1f;
            const float offset = (1.f - modulation * 0.5f) + static_cast<float>(i & 1) * modulation * 0.5f;
            points[i] *= offset;
        }
        for (size_t i = 0; i != points.size(); ++i)
        {
            spiral_.points.push_back(
                {.position = points[i],
                 .color = Vec4f(
                     edt::Math::GetRainbowColors(10.f * static_cast<float>(i) / static_cast<float>(points.size()))
                             .Cast<float>() /
                         255.f,
                     1.f)});
        }

        secondary_.points = {
            klvk::CurveRenderer2d::ControlPoint{.position = {-1.f, -1.f}, .color = {1.f, 0.f, 0.f, 1.f}},
            klvk::CurveRenderer2d::ControlPoint{.position = {0.f, 1.f}, .color = {0.f, 1.f, 0.f, 0.5f}},
            klvk::CurveRenderer2d::ControlPoint{.position = {1.f, -1.f}, .color = {0.f, 0.f, 1.f, 0.f}},
        };
        secondary_.thickness = 20.f;

        extreme_.points = {
            klvk::CurveRenderer2d::ControlPoint{.position = {-0.8f, 0.6f}, .color = {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{.position = {-0.4f, 0.6f}, .color = {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{.position = {0.28f, 0.6f}, .color = {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{.position = {0.42f, 0.5f}, .color = {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{.position = {0.28f, 0.4f}, .color = {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{.position = {-0.4f, 0.4f}, .color = {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{.position = {-0.8f, 0.4f}, .color = {1.f, 1.f, 0.f, 0.3f}},
        };
        extreme_.thickness = 120.f;
        extreme_.segment_pixel_length = 100.f;
        UpdateExtremeCurve(0.f);
    }

    void Tick() override
    {
        klvk::Application::Tick();
        ImGui::SliderFloat("Spiral thickness", &spiral_.thickness, 1.f, 60.f);
        ImGui::SliderFloat("Secondary thickness", &secondary_.thickness, 1.f, 60.f);
        ImGui::SliderFloat("Extreme thickness", &extreme_.thickness, 10.f, 180.f);
        UpdateExtremeCurve(GetTimeSeconds());
        const Vec2f viewport = GetWindow().GetFramebufferSize().Cast<float>();
        const float minimum_extent = viewport.Min();
        const Mat3f view = edt::Math::ScaleMatrix(minimum_extent / viewport);
        spiral_.Draw(viewport, view);
        secondary_.Draw(viewport, view);
        extreme_.Draw(viewport, view);
    }

    Curve spiral_;
    Curve secondary_;
    Curve extreme_;
};

void Main(int argc, char** argv)
{
    CurveApp app;
    app.RunWithArguments(argc, argv);
}
}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
