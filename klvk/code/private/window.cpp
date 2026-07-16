#include "klvk/window.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "GLFW/glfw3.h"
#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/events/window_events.hpp"
#include "klvk/integral_aliases.hpp"

namespace klvk
{

Window::Window(Application& app, u32 width, u32 height)
    : app_(&app),
      id_(MakeWindowId()),
      width_(width),
      height_(height)
{
    Create();
}

Window::~Window()
{
    Destroy();
}

bool Window::ShouldClose() const noexcept
{
    return glfwWindowShouldClose(window_);
}

void Window::SetSize(size_t width, size_t height)
{
    if (fixed_framebuffer_size_.has_value()) return;
    glfwSetWindowSize(window_, static_cast<int>(width), static_cast<int>(height));
}

void Window::SetFramebufferSize(Vec2<u32> size)
{
    if (fixed_framebuffer_size_.has_value()) size = *fixed_framebuffer_size_;
    constexpr size_t max_attempts = 20;
    for (size_t attempt = 0; attempt != max_attempts; ++attempt)
    {
        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
        if (framebuffer_width == static_cast<int>(size.x()) && framebuffer_height == static_cast<int>(size.y())) return;

        int window_width = 0;
        int window_height = 0;
        glfwGetWindowSize(window_, &window_width, &window_height);
        const int new_width =
            framebuffer_width > 0
                ? static_cast<int>(std::lround(static_cast<double>(window_width) * size.x() / framebuffer_width))
                : static_cast<int>(size.x());
        const int new_height =
            framebuffer_height > 0
                ? static_cast<int>(std::lround(static_cast<double>(window_height) * size.y() / framebuffer_height))
                : static_cast<int>(size.y());
        glfwSetWindowSize(window_, std::max(new_width, 1), std::max(new_height, 1));
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
    glfwSetWindowTitle(window_, title);
}

bool Window::IsKeyPressed(int key) const
{
    return glfwGetKey(window_, key) == GLFW_PRESS;
}

u32 Window::MakeWindowId()
{
    static u32 next_id = 0;
    return next_id++;
}

Window* Window::GetWindow(GLFWwindow* glfw_window) noexcept
{
    [[likely]] if (glfw_window)
    {
        void* user_pointer = glfwGetWindowUserPointer(glfw_window);
        [[likely]] if (user_pointer)
        {
            return reinterpret_cast<Window*>(user_pointer);  // NOLINT (inevitable)
        }
    }

    return nullptr;
}

void Window::FrameBufferSizeCallback(GLFWwindow* glfw_window, int width, int height)
{
    CallWndMethod<&Window::OnResize>(glfw_window, width, height);
}

void Window::MouseCallback(GLFWwindow* glfw_window, double x, double y)
{
    CallWndMethod<&Window::OnMouseMove>(glfw_window, Vec2<double>{x, y}.Cast<float>());
}

void Window::MouseButtonCallback(GLFWwindow* glfw_window, int button, int action, int mods)
{
    CallWndMethod<&Window::OnMouseButton>(glfw_window, button, action, mods);
}

void Window::MouseScrollCallback(GLFWwindow* glfw_window, double x_offset, double y_offset)
{
    CallWndMethod<&Window::OnMouseScroll>(glfw_window, static_cast<float>(x_offset), static_cast<float>(y_offset));
}

void Window::Create()
{
    window_ = glfwCreateWindow(static_cast<int>(width_), static_cast<int>(height_), "klvk", nullptr, nullptr);

    if (!window_) throw std::runtime_error(fmt::format("Failed to create window"));

    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, FrameBufferSizeCallback);
    glfwSetCursorPosCallback(window_, MouseCallback);
    glfwSetMouseButtonCallback(window_, MouseButtonCallback);
    glfwSetScrollCallback(window_, MouseScrollCallback);

    double cursor_x{};
    double cursor_y{};
    glfwGetCursorPos(window_, &cursor_x, &cursor_y);
    cursor_.x() = static_cast<float>(cursor_x);
    cursor_.y() = static_cast<float>(cursor_y);
}

void Window::Destroy()
{
    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
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
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            input_mode_ = true;
        }
        else if (action == GLFW_RELEASE)
        {
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
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
    return glfwGetWindowAttrib(window_, GLFW_FOCUSED);
}

Vec2<u32> Window::GetFramebufferSize() const noexcept
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return Vec2<int>{width, height}.Cast<u32>();
}

bool Window::IsHovered() const noexcept
{
    return glfwGetWindowAttrib(window_, GLFW_HOVERED);
}

}  // namespace klvk
