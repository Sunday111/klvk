#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/rendering/curve_renderer_2d.hpp"
#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/window.hpp"

namespace
{
using namespace edt::lazy_matrix_aliases;  // NOLINT

template <size_t size, typename Function>
void Rk4Step(double time, double delta_time, std::span<double, size> state, Function derivatives)
{
    const double delta_sixth = delta_time / 6.0;
    const double delta_half = delta_time * 0.5;
    std::array<double, size> k1{}, k2{}, k3{}, k4{}, temporary{};
    derivatives(time, state, k1);
    for (size_t i = 0; i != size; ++i) temporary[i] = state[i] + delta_half * k1[i];
    derivatives(time + delta_half, temporary, k2);
    for (size_t i = 0; i != size; ++i) temporary[i] = state[i] + delta_half * k2[i];
    derivatives(time + delta_half, temporary, k3);
    for (size_t i = 0; i != size; ++i) temporary[i] = state[i] + delta_time * k3[i];
    derivatives(time + delta_time, temporary, k4);
    for (size_t i = 0; i != size; ++i) state[i] += delta_sixth * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
}

struct DoublePendulumParameters
{
    double mass1 = 1.0;
    double mass2 = 1.0;
    double length1 = 0.45;
    double length2 = 0.45;
    double gravity = 9.81;
};

struct DoublePendulum
{
    [[nodiscard]] std::tuple<Vec2f, Vec2f, Vec2f> GetPoints() const
    {
        const double angle0 = state[0];
        const Vec2<double> point1 =
            origin + Vec2<double>{parameters.length1 * std::sin(angle0), -parameters.length1 * std::cos(angle0)};
        const double angle1 = state[1];
        const Vec2<double> point2 =
            point1 + Vec2<double>{parameters.length2 * std::sin(angle1), -parameters.length2 * std::cos(angle1)};
        return {origin.Cast<float>(), point1.Cast<float>(), point2.Cast<float>()};
    }

    void TimeStep(double delta_time)
    {
        Rk4Step<4>(
            current_time,
            delta_time,
            state,
            [&](double, std::span<const double, 4> value, std::span<double, 4> derivative)
            {
                const double angle1 = value[0];
                const double angle2 = value[1];
                const double velocity1 = value[2];
                const double velocity2 = value[3];
                const double angle_delta = angle1 - angle2;
                const double sin1 = std::sin(angle1);
                const double sin2 = std::sin(angle2);
                const double sin_delta = std::sin(angle_delta);
                const double cos_delta = std::cos(angle_delta);
                const double denominator =
                    (parameters.mass1 + parameters.mass2) - parameters.mass2 * cos_delta * cos_delta;
                derivative[0] = velocity1;
                derivative[1] = velocity2;
                derivative[2] = (-parameters.gravity * (parameters.mass1 + parameters.mass2) * sin1 +
                                 parameters.mass2 * parameters.gravity * sin2 * cos_delta -
                                 parameters.mass2 * sin_delta *
                                     (parameters.length2 * velocity2 * velocity2 +
                                      parameters.length1 * velocity1 * velocity1 * cos_delta)) /
                                (parameters.length1 * denominator);
                derivative[3] = (parameters.gravity * (parameters.mass1 + parameters.mass2) * sin1 * cos_delta -
                                 parameters.gravity * (parameters.mass1 + parameters.mass2) * sin2 +
                                 (parameters.mass1 + parameters.mass2) * sin_delta *
                                     (parameters.length1 * velocity1 * velocity1 +
                                      parameters.length2 * velocity2 * velocity2 * cos_delta)) /
                                (parameters.length2 * denominator);
            });
        current_time += delta_time;
    }

    std::array<double, 4> state{std::numbers::pi_v<double> / 2.0, std::numbers::pi_v<double> / 2.0, 0.0, 0.0};
    double current_time = 0.0;
    Vec2<double> origin{};
    DoublePendulumParameters parameters{};
};

class PendulumApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(2000, 2000);
        GetWindow().SetTitle("Pendulum App");

        constexpr std::array<u8, 1> white{255};
        white_texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {1, 1}, white);
        std::array<u8, kCircleTextureSize * kCircleTextureSize> circle{};
        for (u32 y = 0; y != kCircleTextureSize; ++y)
        {
            for (u32 x = 0; x != kCircleTextureSize; ++x)
            {
                const Vec2f point{
                    (static_cast<float>(x) + 0.5f) / kCircleTextureSize * 2.f - 1.f,
                    (static_cast<float>(y) + 0.5f) / kCircleTextureSize * 2.f - 1.f};
                circle[y * kCircleTextureSize + x] =
                    static_cast<u8>(std::clamp((1.02f - point.Length()) * 2550.f, 0.f, 255.f));
            }
        }
        circle_texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {kCircleTextureSize, kCircleTextureSize}, circle);
        line_renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *white_texture_);
        circle_renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *circle_texture_);
        trail_renderer_ = std::make_unique<klvk::CurveRenderer2d>(*this);

        DoublePendulum& pendulum = pendulums_.emplace_back();
        pendulum.state[0] = std::numbers::pi_v<double>;
        pendulum.state[1] = -0.00001 + std::numbers::pi_v<double>;
        const Vec2f point = std::get<2>(pendulum.GetPoints());
        trail_.push_back(
            {.position = point,
             .color = Vec4f(edt::Math::GetRainbowColors(GetTimeSeconds()).Cast<float>() / 256.f, 1.f)});
    }

    static void AddLine(klvk::InstancedSpriteRenderer2d& renderer, Vec2f a, Vec2f b, float width, Vec4u8 color)
    {
        const Vec2f delta = b - a;
        renderer.Add((a + b) * 0.5f, color, {delta.Length() * 0.5f, width * 0.5f}, std::atan2(delta.y(), delta.x()));
    }

    void Tick() override
    {
        klvk::Application::Tick();
        ImGui::SliderFloat("Time scale", &time_scale_, 0.1f, 10.f);
        const auto target_time = static_cast<double>(GetTimeSeconds() * time_scale_);
        while (pendulums_[0].current_time < target_time)
        {
            pendulums_[0].TimeStep(0.0001);
            const Vec2f point = std::get<2>(pendulums_[0].GetPoints());
            const float distance = (point - trail_.back().position).Length();
            if (distance > 0.03f)
            {
                total_trail_distance_ += distance;
                trail_.push_back(
                    {.position = point,
                     .color =
                         Vec4f(edt::Math::GetRainbowColors(total_trail_distance_ / 40.f).Cast<float>() / 256.f, 1.f)});
            }
        }

        line_renderer_->Clear();
        circle_renderer_->Clear();
        constexpr Vec4u8 red{255, 0, 0, 255};
        constexpr Vec4u8 green{0, 255, 0, 255};
        for (const DoublePendulum& pendulum : pendulums_)
        {
            const auto [point0, point1, point2] = pendulum.GetPoints();
            AddLine(*line_renderer_, point0, point1, 0.001f, red);
            AddLine(*line_renderer_, point1, point2, 0.001f, red);
            circle_renderer_->Add(point0, green, {0.025f, 0.025f});
            circle_renderer_->Add(point1, green, {0.025f, 0.025f});
            circle_renderer_->Add(point2, green, {0.025f, 0.025f});
        }

        const auto viewport = klvk::Viewport::FromWindowSize(GetWindow().GetSize());
        transforms_.Update(camera_, viewport, klvk::AspectRatioPolicy::ShrinkToFit);
        line_renderer_->Render(transforms_.world_to_view);
        circle_renderer_->Render(transforms_.world_to_view);

        constexpr size_t max_points = 3000;
        if (trail_.size() > max_points)
        {
            trail_.erase(trail_.begin(), trail_.begin() + static_cast<ptrdiff_t>(trail_.size() - max_points));
        }
        for (size_t index = 1; auto& point : trail_)
        {
            point.color.w() = static_cast<float>(index++) / static_cast<float>(trail_.size());
        }
        if (trail_.size() > 10)
        {
            klvk::CurveRenderer2d::BuildVertices(
                trail_,
                kTrailThickness,
                kTrailSegmentPixelLength,
                viewport.size.Cast<float>(),
                transforms_.world_to_view,
                trail_vertices_);
            trail_renderer_->DrawVertices(trail_vertices_);
        }
    }

public:
    ~PendulumApp() override
    {
        if (trail_renderer_) GetDeviceContext().WaitIdle();
    }

private:
    static constexpr u32 kCircleTextureSize = 128;
    static constexpr float kTrailThickness = 5.f;
    static constexpr float kTrailSegmentPixelLength = 8.f;
    klvk::Camera2d camera_{};
    klvk::RenderTransforms2d transforms_{};
    std::unique_ptr<klvk::Texture> white_texture_;
    std::unique_ptr<klvk::Texture> circle_texture_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> line_renderer_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> circle_renderer_;
    std::unique_ptr<klvk::CurveRenderer2d> trail_renderer_;
    std::vector<DoublePendulum> pendulums_;
    std::vector<klvk::CurveRenderer2d::ControlPoint> trail_;
    std::vector<klvk::CurveRenderer2d::Vertex> trail_vertices_;
    float time_scale_ = 1.f;
    float total_trail_distance_ = 0.f;
};

void Main(int argc, char** argv)
{
    PendulumApp app;
    app.RunWithArguments(argc, argv);
}
}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
