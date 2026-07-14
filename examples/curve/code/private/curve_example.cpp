#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/rendering/curve_renderer_2d.hpp"
#include "klvk/window.hpp"

namespace
{
using namespace edt::lazy_matrix_aliases;  // NOLINT

// The renderer only draws vertices, so each curve keeps its own control points,
// tessellation parameters and vertex buffer.
struct Curve
{
    void Draw(Vec2f viewport_size, const Mat3f& world_to_view)
    {
        klvk::CurveRenderer2d::BuildVertices(points, thickness, segment_pixel_length, viewport_size, world_to_view, vertices);
        renderer->DrawVertices(vertices);
    }

    std::unique_ptr<klvk::CurveRenderer2d> renderer;
    std::vector<klvk::CurveRenderer2d::ControlPoint> points;
    std::vector<klvk::CurveRenderer2d::Vertex> vertices;
    float thickness = 5.f;
    float segment_pixel_length = 8.f;
};

class CurveApp : public klvk::Application
{
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
            klvk::CurveRenderer2d::ControlPoint{{-1.f, -1.f}, {1.f, 0.f, 0.f, 1.f}},
            klvk::CurveRenderer2d::ControlPoint{{0.f, 1.f}, {0.f, 1.f, 0.f, 0.5f}},
            klvk::CurveRenderer2d::ControlPoint{{1.f, -1.f}, {0.f, 0.f, 1.f, 0.f}},
        };
        secondary_.thickness = 20.f;

        extreme_.points = {
            klvk::CurveRenderer2d::ControlPoint{{-0.8f, 0.6f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.4f, 0.6f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{0.28f, 0.6f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{0.42f, 0.5f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{0.28f, 0.4f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.4f, 0.4f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.8f, 0.4f}, {1.f, 1.f, 0.f, 0.3f}},
        };
        extreme_.thickness = 120.f;
        extreme_.segment_pixel_length = 100.f;
    }

    void Tick() override
    {
        klvk::Application::Tick();
        ImGui::SliderFloat("Spiral thickness", &spiral_.thickness, 1.f, 60.f);
        ImGui::SliderFloat("Secondary thickness", &secondary_.thickness, 1.f, 60.f);
        ImGui::SliderFloat("Extreme thickness", &extreme_.thickness, 10.f, 180.f);
        const Vec2f viewport = GetWindow().GetFramebufferSize().Cast<float>();
        const float minimum_extent = viewport.Min();
        const Mat3f view = edt::Math::ScaleMatrix(Vec2f{minimum_extent / viewport.x(), minimum_extent / viewport.y()});
        spiral_.Draw(viewport, view);
        secondary_.Draw(viewport, view);
        extreme_.Draw(viewport, view);
    }

    Curve spiral_;
    Curve secondary_;
    Curve extreme_;
};

void Main()
{
    CurveApp app;
    app.Run();
}
}  // namespace

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
