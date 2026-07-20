#include "klvk/vulkan/vulkan_common.hpp"

// Vulkan types must be visible before GLFW declares glfwCreateWindowSurface.
#include <GLFW/glfw3.h>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/events/window_events.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/window.hpp"

namespace klvk
{

struct Window::Impl
{
    static Window* GetWindow(GLFWwindow* glfw_window) noexcept
    {
        if (!glfw_window) return nullptr;
        return static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
    }

    template <auto method, typename... Args>
    static void CallWindowMethod(GLFWwindow* glfw_window, Args&&... args)
    {
        [[likely]] if (Window* window = GetWindow(glfw_window))
        {
            (window->*method)(std::forward<Args>(args)...);
        }
    }

    static void FramebufferSizeCallback(GLFWwindow* glfw_window, int width, int height)
    {
        CallWindowMethod<&Window::OnResize>(glfw_window, width, height);
    }

    static void MouseCallback(GLFWwindow* glfw_window, double x, double y)
    {
        CallWindowMethod<&Window::OnMouseMove>(glfw_window, Vec2<double>{x, y}.Cast<float>());
    }

    static void MouseButtonCallback(GLFWwindow* glfw_window, int button, int action, int mods)
    {
        CallWindowMethod<&Window::OnMouseButton>(glfw_window, button, action, mods);
    }

    static void MouseScrollCallback(GLFWwindow* glfw_window, double x_offset, double y_offset)
    {
        CallWindowMethod<&Window::OnMouseScroll>(
            glfw_window,
            static_cast<float>(x_offset),
            static_cast<float>(y_offset));
    }

    GLFWwindow* window = nullptr;
    Backend backend = Backend::Glfw;
};

Window::Window(Application& app, u32 width, u32 height) : Window(app, width, height, Backend::Glfw) {}

Window::Window(Application& app, u32 width, u32 height, Backend backend)
    : app_(&app),
      impl_(std::make_unique<Impl>()),
      id_(MakeWindowId()),
      width_(width),
      height_(height)
{
    impl_->backend = backend;
    Create();
}

std::unique_ptr<Window> Window::CreateOffscreen(Application& app, u32 width, u32 height)
{
    return std::unique_ptr<Window>(new Window(app, width, height, Backend::Offscreen));
}

Window::~Window()
{
    Destroy();
}

bool Window::ShouldClose() const noexcept
{
    return impl_->window && glfwWindowShouldClose(impl_->window);
}

void Window::SetSize(size_t width, size_t height)
{
    if (fixed_framebuffer_size_.has_value()) return;
    if (impl_->window)
    {
        glfwSetWindowSize(impl_->window, static_cast<int>(width), static_cast<int>(height));
    }
    else
    {
        OnResize(static_cast<int>(width), static_cast<int>(height));
    }
}

void Window::SetFramebufferSize(Vec2<u32> size)
{
    if (fixed_framebuffer_size_.has_value()) size = *fixed_framebuffer_size_;
    if (!impl_->window)
    {
        OnResize(static_cast<int>(size.x()), static_cast<int>(size.y()));
        return;
    }
    constexpr size_t max_attempts = 20;
    for (size_t attempt = 0; attempt != max_attempts; ++attempt)
    {
        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(impl_->window, &framebuffer_width, &framebuffer_height);
        if (std::cmp_equal(framebuffer_width, size.x()) && std::cmp_equal(framebuffer_height, size.y())) return;

        int window_width = 0;
        int window_height = 0;
        glfwGetWindowSize(impl_->window, &window_width, &window_height);
        const int new_width =
            framebuffer_width > 0
                ? static_cast<int>(std::lround(static_cast<double>(window_width) * size.x() / framebuffer_width))
                : static_cast<int>(size.x());
        const int new_height =
            framebuffer_height > 0
                ? static_cast<int>(std::lround(static_cast<double>(window_height) * size.y() / framebuffer_height))
                : static_cast<int>(size.y());
        glfwSetWindowSize(impl_->window, std::max(new_width, 1), std::max(new_height, 1));
        // X11 window-manager resizes are asynchronous. Give the corresponding
        // configure event time to arrive before calculating the next correction.
        glfwWaitEventsTimeout(0.05);
    }
    const Vec2<u32> actual = GetFramebufferSize();
    ErrorHandling::Ensure(
        actual == size,
        "Could not set framebuffer size to {}x{}; GLFW reports {}x{}",
        size.x(),
        size.y(),
        actual.x(),
        actual.y());
}

void Window::SetFixedFramebufferSize(Vec2<u32> size)
{
    fixed_framebuffer_size_ = size;
    SetFramebufferSize(size);
}

void Window::SetTitle(const char* title)
{
    if (impl_->window) glfwSetWindowTitle(impl_->window, title);
}

bool Window::IsKeyPressed(int key) const
{
    return impl_->window && glfwGetKey(impl_->window, key) == GLFW_PRESS;
}

u32 Window::MakeWindowId()
{
    static u32 next_id = 0;
    return next_id++;
}

void Window::Create()
{
    if (impl_->backend == Backend::Offscreen)
    {
        // There is no pointer in a display-independent run. Keep cursor-driven
        // overlays outside the logical framebuffer instead of inventing input
        // at its top-left pixel.
        cursor_ = {-1'000'000.f, -1'000'000.f};
        return;
    }

    impl_->window = glfwCreateWindow(static_cast<int>(width_), static_cast<int>(height_), "klvk", nullptr, nullptr);

    if (!impl_->window) throw std::runtime_error(fmt::format("Failed to create window"));

    glfwSetInputMode(impl_->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowUserPointer(impl_->window, this);
    glfwSetFramebufferSizeCallback(impl_->window, Impl::FramebufferSizeCallback);
    glfwSetCursorPosCallback(impl_->window, Impl::MouseCallback);
    glfwSetMouseButtonCallback(impl_->window, Impl::MouseButtonCallback);
    glfwSetScrollCallback(impl_->window, Impl::MouseScrollCallback);

    double cursor_x{};
    double cursor_y{};
    glfwGetCursorPos(impl_->window, &cursor_x, &cursor_y);
    cursor_ = {static_cast<float>(cursor_x), static_cast<float>(cursor_y)};
}

void Window::Destroy()
{
    if (impl_->window)
    {
        glfwDestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
}

void Window::OnResize(int width, int height)
{
    Vec2i prev_size = {static_cast<int>(width_), static_cast<int>(height_)};
    width_ = static_cast<u32>(width);
    height_ = static_cast<u32>(height);

    app_->GetEventManager().Emit(events::OnWindowResize{.previous = prev_size, .current{width, height}});
}

void Window::OnMouseMove(Vec2f new_cursor)
{
    auto prev = cursor_;
    cursor_ = new_cursor;
    app_->GetEventManager().Emit(events::OnMouseMove{.previous = prev, .current = cursor_});
}

void Window::OnMouseButton(int button, int action, [[maybe_unused]] int mods)
{
    switch (button)
    {
    case GLFW_MOUSE_BUTTON_RIGHT:
        if (action == GLFW_PRESS)
        {
            glfwSetInputMode(impl_->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            input_mode_ = true;
        }
        else if (action == GLFW_RELEASE)
        {
            glfwSetInputMode(impl_->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            input_mode_ = false;
        }
        break;
    }
}

void Window::OnMouseScroll(float dx, float dy)
{
    app_->GetEventManager().Emit(events::OnMouseScroll{.value = {dx, dy}});
}

bool Window::IsFocused() const noexcept
{
    return impl_->window && glfwGetWindowAttrib(impl_->window, GLFW_FOCUSED);
}

Vec2<u32> Window::GetFramebufferSize() const noexcept
{
    if (!impl_->window) return {width_, height_};
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(impl_->window, &width, &height);
    return Vec2<int>{width, height}.Cast<u32>();
}

bool Window::IsHovered() const noexcept
{
    return impl_->window && glfwGetWindowAttrib(impl_->window, GLFW_HOVERED);
}

void* Window::GetPlatformHandle() const noexcept
{
    return impl_->window;
}

std::vector<const char*> Window::GetRequiredVulkanInstanceExtensions() const
{
    ErrorHandling::Ensure(impl_->window != nullptr, "Offscreen windows do not require Vulkan surface extensions");
    u32 extension_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extension_count);
    ErrorHandling::Ensure(extensions != nullptr, "GLFW cannot provide Vulkan instance extensions");
    return {extensions, extensions + extension_count};
}

VkSurfaceKHR Window::CreateVulkanSurface(VkInstance instance) const
{
    ErrorHandling::Ensure(impl_->window != nullptr, "Cannot create a Vulkan surface for an offscreen window");
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    CheckVkResult(glfwCreateWindowSurface(instance, impl_->window, nullptr, &surface), "glfwCreateWindowSurface");
    return surface;
}

}  // namespace klvk
