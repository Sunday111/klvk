#pragma once

#include "EverydayTools/Math/Matrix.hpp"

namespace klvk
{

class Window;

class GlfwState
{
public:
    ~GlfwState();

    void Initialize();
    void Uninitialize();
    [[nodiscard]] bool ConfigureForVulkan(bool hidden);
    [[nodiscard]] bool IsVulkanSupported() const;
    [[nodiscard]] edt::Vec2f GetPrimaryMonitorContentScale() const;
    void ShowWindow(Window& window) const;
    void HideWindow(Window& window) const;
    void PollEvents() const;
    void WaitEvents() const;

    [[nodiscard]] bool InitializeImGui(Window& window) const;
    void ShutdownImGui() const;
    void BeginImGuiFrame() const;

private:
    bool initialized_ = false;
};

}  // namespace klvk
