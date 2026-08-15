#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "edt/math/matrix.hpp"
#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/platform/file_dialog.hpp"
#include "klvk/vulkan/swapchain_present_mode.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk::events
{
class EventManager;
}

namespace klvk
{

class Window;
class DeviceContext;
class Swapchain;
class TimerManager;

class Application
{
    struct State;

public:
    // Number of frames the CPU can record ahead of the GPU. Per-frame GPU resources
    // (command buffers, dynamic data buffers) exist in this many copies.
    static constexpr size_t kFramesInFlight = 2;

    Application();
    Application(const Application&) = delete;
    Application(Application&&) = delete;
    virtual ~Application();

    virtual void Initialize();
    virtual void Run();
    void RunWithArguments(int argc, char** argv);
    virtual void PreTick();
    virtual void BeforeSwapchainRender(vk::CommandBuffer command_buffer);
    virtual void Tick();
    virtual void PostTick();
    virtual void MainLoop();
    virtual void InitializeReflectionTypes();

    [[nodiscard]] virtual bool WantsToClose() const;

    Window& GetWindow();
    [[nodiscard]] const Window& GetWindow() const;

    [[nodiscard]] const std::filesystem::path& GetExecutableDir() const;
    [[nodiscard]] virtual std::filesystem::path GetContentDir() const;
    [[nodiscard]] virtual std::filesystem::path GetShaderDir() const;

    // Present only when RunWithArguments loaded --klvk-diagnostics. Applications
    // may read their opaque "application" object during Initialize().
    [[nodiscard]] const nlohmann::json* GetDiagnosticApplicationConfig() const noexcept;

    // The frame a diagnostic run stops on, for an application that wants to say
    // how far through it is. Nothing when the run has no frame-based exit, which
    // includes every run that is not a diagnostic one.
    [[nodiscard]] std::optional<u64> GetDiagnosticExitFrame() const noexcept;

    // Ask the user for a file. Prefer these over FileDialog itself: a diagnostic
    // replay answers them from its
    // recording without a dialog reaching the screen, and a session started with
    // --klvk-record-input writes the answer into the recording. Only an ordinary
    // run puts a real dialog up, and that one blocks until the user is done.
    [[nodiscard]] std::optional<std::filesystem::path> OpenFileDialog(
        std::string_view title,
        std::span<const FileDialog::Filter> filters = {},
        const std::filesystem::path& default_path = {});

    [[nodiscard]] std::optional<std::filesystem::path> SaveFileDialog(
        std::string_view title,
        std::span<const FileDialog::Filter> filters = {},
        const std::filesystem::path& default_path = {});

private:
    // Both dialogs differ only in which one to put up when the run is an ordinary
    // one, so record and replay are decided in a single place.
    [[nodiscard]] std::optional<std::filesystem::path> AnswerFileDialog(
        const std::function<std::optional<std::filesystem::path>()>& ask);

public:
    events::EventManager& GetEventManager();

    // The application main loop owns Advance; callers schedule and cancel only.
    TimerManager& GetTimerManager();

    // Current time. Relative to app start
    [[nodiscard]] float GetTimeSeconds() const;

    // Time (in seconds) when the current fame started. Relative to app start
    [[nodiscard]] float GetCurrentFrameStartTime() const;

    // How many ticks app does per second (on average among last 128 ticks)
    [[nodiscard]] float GetFramerate() const;

    // Duration of the previous tick (in seconds)
    [[nodiscard]] float GetLastFrameDurationSeconds() const;

    void SetTargetFramerate(std::optional<float> framerate);
    void SetSwapchainPresentMode(SwapchainPresentMode present_mode);

    void SetClearColor(const edt::Vec4f& color);
    void SetDepthBufferEnabled(bool enabled);

    // Attaches the stencil plane of the depth-stencil image and clears it to zero
    // every frame. Independent of depth: a stencil-only pass leaves depth testing off.
    void SetStencilBufferEnabled(bool enabled);

    // Vulkan accessors for renderers.
    [[nodiscard]] DeviceContext& GetDeviceContext();
    [[nodiscard]] vk::Format GetSwapchainFormat() const;
    [[nodiscard]] vk::Format GetDepthFormat() const;

    // Valid between PreTick and PostTick: the command buffer of the frame being recorded
    // (inside an active dynamic rendering pass targeting the presentation image)
    // and the index of the frame-in-flight slot it belongs to.
    [[nodiscard]] vk::CommandBuffer GetCurrentCommandBuffer() const;
    [[nodiscard]] size_t GetFrameInFlightIndex() const;

private:
    void RunImpl();

    std::unique_ptr<State> state_;
};

}  // namespace klvk
