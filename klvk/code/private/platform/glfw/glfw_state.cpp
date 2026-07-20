#include "glfw_state.hpp"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>

#include <stdexcept>

#include "klvk/window.hpp"

namespace klvk
{

GlfwState::~GlfwState()
{
    Uninitialize();
}

void GlfwState::Initialize()
{
    [[unlikely]] if (!glfwInit())
    {
        throw std::runtime_error("failed to initialize glfw");
    }
    initialized_ = true;
}

void GlfwState::Uninitialize()
{
    if (!initialized_) return;
    glfwTerminate();
    initialized_ = false;
}

bool GlfwState::ConfigureForVulkan(bool hidden)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!hidden) return false;

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    const bool realize_hidden_x11 = glfwGetPlatform() == GLFW_PLATFORM_X11;
    if (realize_hidden_x11)
    {
        // Some Vulkan X11 drivers report a fixed fallback surface extent until
        // the native window has been mapped at least once.
        glfwWindowHint(GLFW_POSITION_X, -32000);
        glfwWindowHint(GLFW_POSITION_Y, -32000);
    }
    return realize_hidden_x11;
}

bool GlfwState::IsVulkanSupported() const
{
    return glfwVulkanSupported() == GLFW_TRUE;
}

edt::Vec2f GlfwState::GetPrimaryMonitorContentScale() const
{
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return {1.f, 1.f};
    edt::Vec2f scale{};
    glfwGetMonitorContentScale(monitor, &scale.x(), &scale.y());
    return scale;
}

void GlfwState::ShowWindow(Window& window) const
{
    glfwShowWindow(static_cast<GLFWwindow*>(window.GetPlatformHandle()));
}

void GlfwState::HideWindow(Window& window) const
{
    glfwHideWindow(static_cast<GLFWwindow*>(window.GetPlatformHandle()));
}

void GlfwState::PollEvents() const
{
    glfwPollEvents();
}

void GlfwState::WaitEvents() const
{
    glfwWaitEvents();
}

bool GlfwState::InitializeImGui(Window& window) const
{
    return ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window.GetPlatformHandle()), true);
}

void GlfwState::ShutdownImGui() const
{
    ImGui_ImplGlfw_Shutdown();
}

void GlfwState::BeginImGuiFrame() const
{
    ImGui_ImplGlfw_NewFrame();
}

}  // namespace klvk
