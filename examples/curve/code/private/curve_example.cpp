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
        secondary_ = std::make_unique<klvk::CurveRenderer2d>(*this);
        extreme_ = std::make_unique<klvk::CurveRenderer2d>(*this);

        auto points = edt::Math::GenerateSpiralPoints(100, {2.f, 2.f});
        for (size_t i = 0; i != points.size(); ++i)
        {
            constexpr float modulation = 0.1f;
            const float offset = (1.f - modulation * 0.5f) + static_cast<float>(i & 1) * modulation * 0.5f;
            points[i] *= offset;
        }
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
        spiral_->thickness_ = 2.f;

        const std::array secondary_points{
            klvk::CurveRenderer2d::ControlPoint{{-1.f, -1.f}, {1.f, 0.f, 0.f, 1.f}},
            klvk::CurveRenderer2d::ControlPoint{{0.f, 1.f}, {0.f, 1.f, 0.f, 0.5f}},
            klvk::CurveRenderer2d::ControlPoint{{1.f, -1.f}, {0.f, 0.f, 1.f, 0.f}},
        };
        secondary_->SetPoints(secondary_points);
        secondary_->thickness_ = 20.f;

        const std::array extreme_points{
            klvk::CurveRenderer2d::ControlPoint{{-0.8f, 0.6f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.4f, 0.6f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{0.28f, 0.6f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{0.42f, 0.5f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{0.28f, 0.4f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.4f, 0.4f}, {1.f, 1.f, 0.f, 0.3f}},
            klvk::CurveRenderer2d::ControlPoint{{-0.8f, 0.4f}, {1.f, 1.f, 0.f, 0.3f}},
        };
        extreme_->SetPoints(extreme_points);
        extreme_->thickness_ = 120.f;
        extreme_->segment_pixel_length_ = 100.f;
    }

    void Tick() override
    {
        klvk::Application::Tick();
        ImGui::SliderFloat("Spiral thickness", &spiral_->thickness_, 1.f, 60.f);
        ImGui::SliderFloat("Secondary thickness", &secondary_->thickness_, 1.f, 60.f);
        ImGui::SliderFloat("Extreme thickness", &extreme_->thickness_, 10.f, 180.f);
        const Vec2f viewport = GetWindow().GetFramebufferSize().Cast<float>();
        const float minimum_extent = viewport.Min();
        const Mat3f view = edt::Math::ScaleMatrix(Vec2f{minimum_extent / viewport.x(), minimum_extent / viewport.y()});
        spiral_->Draw(viewport, view);
        secondary_->Draw(viewport, view);
        extreme_->Draw(viewport, view);
    }

    std::unique_ptr<klvk::CurveRenderer2d> spiral_;
    std::unique_ptr<klvk::CurveRenderer2d> secondary_;
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
