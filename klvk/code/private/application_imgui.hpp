#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "edt/math/matrix.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class DeviceContext;
class GlfwState;
class RenderTarget;
class Window;

class ApplicationImGui
{
public:
    void Initialize(
        DeviceContext& device_context,
        const RenderTarget& render_target,
        GlfwState& glfw,
        Window& window,
        bool offscreen,
        edt::Vec2f content_scale,
        edt::Vec2f framebuffer_scale,
        const std::optional<std::filesystem::path>& ini_path,
        const std::filesystem::path& font_path);
    void Shutdown(GlfwState& glfw);

    void PrepareFrame(GlfwState& glfw, bool offscreen, vk::Extent2D extent) const;
    static void BeginFrame(std::optional<u64> fixed_step_nanoseconds);
    static void Render(vk::CommandBuffer command_buffer);

private:
    vk::UniqueDescriptorPool descriptor_pool_;
    std::string ini_filename_;
    bool context_created_ = false;
    bool glfw_initialized_ = false;
    bool vulkan_initialized_ = false;
};

}  // namespace klvk
