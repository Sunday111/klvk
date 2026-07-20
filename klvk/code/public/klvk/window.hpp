#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

class Application;
class DeviceContext;
class GlfwState;

class Window
{
public:
    Window(Application& app, u32 width, u32 height);
    ~Window();

    [[nodiscard]] bool ShouldClose() const noexcept;
    [[nodiscard]] u32 GetWidth() const noexcept { return width_; }
    [[nodiscard]] u32 GetHeight() const noexcept { return height_; }
    [[nodiscard]] bool IsFocused() const noexcept;
    [[nodiscard]] bool IsHovered() const noexcept;
    [[nodiscard]] Vec2f GetCursorPos() const noexcept { return cursor_; }
    [[nodiscard]] bool IsInInputMode() const noexcept { return input_mode_; }

    [[nodiscard]] Vec2<u32> GetSize() const { return {width_, height_}; }
    [[nodiscard]] Vec2f GetSize2f() const { return GetSize().Cast<float>(); }

    // Size of the surface to render to, in pixels. Differs from GetSize on high-dpi displays.
    [[nodiscard]] Vec2<u32> GetFramebufferSize() const noexcept;
    [[nodiscard]] float GetAspect() const noexcept
    {
        return static_cast<float>(GetWidth()) / static_cast<float>(GetHeight());
    }

    void SetSize(size_t width, size_t height);
    // Adjusts the logical window size until the framebuffer has this exact pixel size.
    // Intended for deterministic diagnostic runs on scaled displays.
    void SetFramebufferSize(Vec2<u32> size);
    void SetTitle(const char* title);

    [[nodiscard]] bool IsKeyPressed(int key) const;

private:
    friend class Application;
    friend class DeviceContext;
    friend class GlfwState;

    enum class Backend : u8
    {
        Glfw,
        Offscreen,
    };

    Window(Application& app, u32 width, u32 height, Backend backend);
    [[nodiscard]] static std::unique_ptr<Window> CreateOffscreen(Application& app, u32 width, u32 height);
    void SetFixedFramebufferSize(Vec2<u32> size);
    static u32 MakeWindowId();

    void Create();
    void Destroy();
    void OnResize(int width, int height);
    void OnMouseMove(Vec2f new_cursor);
    void OnMouseButton(int button, int action, [[maybe_unused]] int mods);
    void OnMouseScroll([[maybe_unused]] float dx, float dy);

    [[nodiscard]] void* GetPlatformHandle() const noexcept;
    [[nodiscard]] std::vector<const char*> GetRequiredVulkanInstanceExtensions() const;
    [[nodiscard]] VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const;

    struct Impl;
    Application* app_ = nullptr;
    std::unique_ptr<Impl> impl_;
    Vec2f cursor_;
    u32 id_;
    u32 width_;
    u32 height_;
    std::optional<Vec2<u32>> fixed_framebuffer_size_;
    bool input_mode_ = false;
};

}  // namespace klvk
