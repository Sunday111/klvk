#include "fractal_app.hpp"

#include <cmath>
#include <numbers>

#include "clipboard.hpp"
#include "imgui.h"
#include "interpolation_widget.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/window.hpp"
#include "renderers/counting_renderer.hpp"
#include "renderers/simple_cpu_renderer.hpp"
#include "renderers/simple_gpu_renderer.hpp"

FractalApp::FractalApp() = default;
FractalApp::~FractalApp() noexcept = default;

void FractalApp::ApplyDiagnosticConfig()
{
    const std::optional config = FractalDiagnosticConfig::FromJSON(GetDiagnosticApplicationConfig());
    if (!config) return;

    if (config->renderer)
    {
        size_t index = 0;
        if (*config->renderer == FractalDiagnosticConfig::Renderer::Counting) index = 1;
        if (*config->renderer == FractalDiagnosticConfig::Renderer::SimpleCpu) index = 2;
        renderer_combo_.SetSelectedIndex(index);
    }
    if (config->camera)
    {
        if (config->camera->eye) settings_.camera.eye = *config->camera->eye;
        if (config->camera->zoom)
        {
            settings_.camera.zoom = *config->camera->zoom;
            zoom_power_ = std::log(settings_.camera.zoom) / std::log(1.05f);
        }
    }
    if (config->color_mode) settings_.color_mode = *config->color_mode;
    if (config->colors)
    {
        settings_.colors = *config->colors;
        settings_.num_colors = settings_.colors.size();
        settings_.color_positions.assign(settings_.num_colors, 0.f);
        settings_.DistributePositionsUniformly();
    }
    if (config->show_interpolation_widget)
    {
        show_interpolation_widget_ = *config->show_interpolation_widget;
    }
    if (config->view_rotation_radians) settings_.view_rotation_radians = *config->view_rotation_radians;
    constant_animation_ = config->constant_animation;
}

void FractalApp::UpdateAnimation()
{
    if (!constant_animation_) return;

    const float phase =
        2.f * std::numbers::pi_v<float> *
        (GetTimeSeconds() / constant_animation_->period_seconds + constant_animation_->start_phase_turns);
    settings_.fractal_constant =
        constant_animation_->center + constant_animation_->radius * edt::Vec2f{std::cos(phase), std::sin(phase)};
}

void FractalApp::Initialize()
{
    klvk::Application::Initialize();
    event_listener_ = klvk::events::EventListenerMethodCallbacks<&FractalApp::OnMouseScroll>::CreatePtr(this);
    event_subscription_ = GetEventManager().AddEventListener(*event_listener_);

    SetClearColor({});
    GetWindow().SetSize(1000, 1000);
    GetWindow().SetTitle("Fractal");
    SetTargetFramerate(30.f);

    klvk::Shader::shaders_dir_ = GetShaderDir();
    interpolation_widget_ = std::make_unique<InterpolationWidget>(*this, kMaxIterations + 1);

    settings_.RandomizeColors();
    settings_.DistributePositionsUniformly();

    renderer_combo_.EmplaceItem("Simple GPU", RendererFactoryFn<SimpleGpuRenderer>);
    renderer_combo_.EmplaceItem("Counting", RendererFactoryFn<CountingRenderer>);
    renderer_combo_.EmplaceItem("Simple CPU", RendererFactoryFn<SimpleCpuRenderer>);
    ApplyDiagnosticConfig();
    renderer_ = renderer_combo_.GetSelectedItem()(*this, kMaxIterations);
}

void FractalApp::HandleInput()
{
    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
        int right = 0;
        int up = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) up += 1;
        if (ImGui::IsKeyDown(ImGuiKey_S)) up -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_D)) right += 1;
        if (ImGui::IsKeyDown(ImGuiKey_A)) right -= 1;
        if (std::abs(right) + std::abs(up))
        {
            edt::Vec2f delta{};
            delta += static_cast<float>(right) * edt::Vec2f::AxisX();
            delta += static_cast<float>(up) * edt::Vec2f::AxisY();
            settings_.camera.eye =
                settings_.camera.eye + delta * move_speed_ * GetLastFrameDurationSeconds() / settings_.camera.zoom;
        }
    }
}

std::vector<edt::Vec4u8> FractalApp::CaptureScreenshot() const
{
    // klvk has no framebuffer readback yet, and Clipboard::AddImage is a stub
    // on linux anyway - the button stays for parity with klgl.
    return {};
}

void FractalApp::BeforeSwapchainRender(vk::CommandBuffer command_buffer)
{
    if (pending_renderer_factory_)
    {
        renderer_.reset();  // waits for the device before destroying pipelines
        renderer_ = pending_renderer_factory_(*this, kMaxIterations);
        pending_renderer_factory_ = nullptr;
        settings_.changed = true;
    }

    HandleInput();
    UpdateAnimation();

    settings_.SetViewport(
        klvk::Viewport{
            .position = {},
            .size = GetWindow().GetSize(),
        });
    if (settings_.changed)
    {
        renderer_->ApplySettings(settings_);
        settings_.changed = false;
    }
    renderer_->PrepareFrame(command_buffer, settings_);
}

void FractalApp::Tick()
{
    klvk::Application::Tick();

    renderer_->Render(GetCurrentCommandBuffer(), settings_);

    if (screenshot)
    {
        auto resolution = GetWindow().GetSize();
        auto pixels = CaptureScreenshot();
        Clipboard::AddImage(resolution.Cast<size_t>(), pixels);
        screenshot = false;
    }

    if (show_interpolation_widget_)
    {
        klvk::Viewport widget_viewport;
        widget_viewport.MatchWindowSize(GetWindow().GetSize());
        widget_viewport.size.y() = 50;
        interpolation_widget_->Render(GetCurrentCommandBuffer(), widget_viewport, settings_);
    }

    if (ImGui::Begin("Settings"))
    {
        if (renderer_combo_.Draw())
        {
            pending_renderer_factory_ = renderer_combo_.GetSelectedItem();
        }

        screenshot = ImGui::Button("Screenshot to clipboard");
        ImGui::SameLine();
        ImGui::Checkbox("With interface", &screenshot_with_ui);

        settings_.DrawGUI();
    }
    ImGui::End();
}

void FractalApp::OnMouseScroll(const klvk::events::OnMouseScroll& event)
{
    if (ImGui::GetIO().WantCaptureMouse) return;

    zoom_power_ += event.value.y();
    settings_.camera.zoom = std::max(std::pow(1.05f, zoom_power_), std::numeric_limits<float>::min());
}
