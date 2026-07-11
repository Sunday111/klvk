#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "EverydayTools/Math/Matrix.hpp"
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
    virtual void PreTick();
    virtual void BeforeSwapchainRender(VkCommandBuffer command_buffer);
    virtual void Tick();
    virtual void PostTick();
    virtual void MainLoop();
    virtual void InitializeReflectionTypes();

    [[nodiscard]] virtual bool WantsToClose() const;

    Window& GetWindow();
    const Window& GetWindow() const;

    const std::filesystem::path& GetExecutableDir() const;
    virtual std::filesystem::path GetContentDir() const;
    virtual std::filesystem::path GetShaderDir() const;

    events::EventManager& GetEventManager();

    // Current time. Relative to app start
    float GetTimeSeconds() const;

    // Time (in seconds) when the current fame started. Relative to app start
    float GetCurrentFrameStartTime() const;

    // How many ticks app does per second (on average among last 128 ticks)
    float GetFramerate() const;

    // Duration of the previous tick (in seconds)
    float GetLastFrameDurationSeconds() const;

    void SetTargetFramerate(std::optional<float> framerate);

    void SetClearColor(const edt::Vec4f& color);
    void SetDepthBufferEnabled(bool enabled);

    // Vulkan accessors for renderers.
    [[nodiscard]] DeviceContext& GetDeviceContext();
    [[nodiscard]] VkFormat GetSwapchainFormat() const;
    [[nodiscard]] VkFormat GetDepthFormat() const;

    // Valid between PreTick and PostTick: the command buffer of the frame being recorded
    // (inside an active dynamic rendering pass targeting the swapchain image)
    // and the index of the frame-in-flight slot it belongs to.
    [[nodiscard]] VkCommandBuffer GetCurrentCommandBuffer() const;
    [[nodiscard]] size_t GetFrameInFlightIndex() const;

private:
    std::unique_ptr<State> state_;
};

}  // namespace klvk
