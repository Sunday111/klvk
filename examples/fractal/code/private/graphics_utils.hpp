#pragma once

#include <string_view>

#include "klvk/application.hpp"
#include "klvk/camera/viewport.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

// Matches the push constant block shared by fractal.frag and counting_compute.comp.
struct FractalPushConstants
{
    std::array<edt::Vec4f, 3> screen_to_world_columns{};
    edt::Vec2f resolution{};
    edt::Vec2f julia_constant{};
    edt::Vec2f fractal_power{};
};

static_assert(sizeof(FractalPushConstants) == 72);

[[nodiscard]] FractalPushConstants
MakeFractalPushConstants(const class FractalSettings& settings, const edt::Mat3f& screen_to_world);

[[nodiscard]] VkShaderModule LoadShaderModule(klvk::Application& app, std::string_view name);

// Fullscreen triangle list (6 vertices, no vertex buffers), no blending, dynamic
// viewport and scissor, targeting the swapchain format.
[[nodiscard]] VkPipeline CreateFullscreenPipeline(
    klvk::Application& app,
    VkPipelineLayout pipeline_layout,
    VkShaderModule vertex_shader,
    VkShaderModule fragment_shader,
    const VkSpecializationInfo* fragment_specialization);

// Applies a viewport given in klgl's convention (origin at the bottom-left corner of the
// window) by flipping it the same way the application flips the default viewport.
void CmdSetGlStyleViewport(VkCommandBuffer command_buffer, const klvk::Viewport& viewport, edt::Vec2u32 framebuffer);
