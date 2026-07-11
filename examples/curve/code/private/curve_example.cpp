#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/rendering/curve_renderer_2d.hpp"
#include "klvk/window.hpp"

namespace
{
using namespace edt::lazy_matrix_aliases;  // NOLINT

class CurveApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Curve Example");
        spiral_ = std::make_unique<klvk::CurveRenderer2d>(*this);
        extreme_ = std::make_unique<klvk::CurveRenderer2d>(*this);

        const auto points = edt::Math::GenerateSpiralPoints(100, {1.7f, 1.7f});
        std::vector<klvk::CurveRenderer2d::ControlPoint> spiral_points;
        for (size_t i = 0; i != points.size(); ++i)
        {
            spiral_points.push_back(
                {.position = points[i],
                 .color = Vec4f(
                     edt::Math::GetRainbowColors(10.f * static_cast<float>(i) / static_cast<float>(points.size()))
                             .Cast<float>() /
                         255.f,
                     0.55f)});
        }
        spiral_->SetPoints(spiral_points);
        spiral_->thickness_ = 12.f;

        const std::array extreme_points{
            klvk::CurveRenderer2d::ControlPoint{{-0.85f, 0.65f}, {1.f, 1.f, 0.f, 0.35f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.25f, 0.65f}, {1.f, 1.f, 0.f, 0.35f}},
            klvk::CurveRenderer2d::ControlPoint{{0.48f, 0.65f}, {1.f, 1.f, 0.f, 0.35f}},
            klvk::CurveRenderer2d::ControlPoint{{0.6f, 0.42f}, {1.f, 1.f, 0.f, 0.35f}},
            klvk::CurveRenderer2d::ControlPoint{{0.42f, 0.18f}, {1.f, 1.f, 0.f, 0.35f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.3f, 0.18f}, {1.f, 1.f, 0.f, 0.35f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.85f, 0.18f}, {1.f, 1.f, 0.f, 0.35f}},
        };
        extreme_->SetPoints(extreme_points);
        extreme_->thickness_ = 120.f;
        extreme_->segment_pixel_length_ = 45.f;
    }

    void Tick() override
    {
        klvk::Application::Tick();
        ImGui::SliderFloat("Spiral thickness", &spiral_->thickness_, 1.f, 60.f);
        ImGui::SliderFloat("Extreme thickness", &extreme_->thickness_, 10.f, 180.f);
        const Vec2f viewport = GetWindow().GetFramebufferSize().Cast<float>();
        Mat3f view = edt::Math::ScaleMatrix(Vec2f{} + 0.48f);
        spiral_->Draw(viewport, view);
        extreme_->Draw(viewport, Mat3f::Identity());
    }

    std::unique_ptr<klvk::CurveRenderer2d> spiral_;
    std::unique_ptr<klvk::CurveRenderer2d> extreme_;
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
