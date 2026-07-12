#pragma once

#include <klvk/events/event_listener_interface.hpp>

#include "fractal_settings.hpp"
#include "klvk/application.hpp"
#include "klvk/ui/imgui_value_combo.hpp"

namespace klvk::events
{
class OnMouseScroll;
}

class FractalRenderer;
class InterpolationWidget;

class FractalApp : public klvk::Application
{
public:
    FractalApp();
    ~FractalApp() noexcept override;

    static constexpr size_t kMaxIterations = 200;

    void Initialize() override;

    void HandleInput();

    void BeforeSwapchainRender(VkCommandBuffer command_buffer) override;
    void Tick() override;

    void OnMouseScroll(const klvk::events::OnMouseScroll& event);
    std::vector<edt::Vec4u8> CaptureScreenshot() const;

    std::unique_ptr<FractalRenderer> renderer_;
    std::unique_ptr<InterpolationWidget> interpolation_widget_;

    std::unique_ptr<klvk::events::IEventListener> event_listener_;
    float zoom_power_ = 0.f;
    float move_speed_ = 0.5f;
    FractalSettings settings_{10};
    bool screenshot = false;
    bool screenshot_with_ui = false;

    template <typename T>
    [[nodiscard]] static std::unique_ptr<FractalRenderer> RendererFactoryFn(
        klvk::Application& app,
        size_t num_iterations)
    {
        return std::make_unique<T>(app, num_iterations);
    }

    using RendererFactory = std::unique_ptr<FractalRenderer> (*)(klvk::Application& app, size_t iterations);
    klvk::ImGuiValueCombo<RendererFactory> renderer_combo_{"Renderer"};

    // The current renderer's commands are already recorded into this frame's command
    // buffer when the combo changes, so the swap is deferred to the next frame.
    RendererFactory pending_renderer_factory_ = nullptr;
};
