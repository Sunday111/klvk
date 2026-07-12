#pragma once

#include "../graphics_utils.hpp"
#include "fractal_renderer.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"

class SimpleGpuRenderer : public FractalRenderer
{
public:
    SimpleGpuRenderer(klvk::Application& app, size_t max_iterations);
    ~SimpleGpuRenderer() noexcept override;

    void Render(VkCommandBuffer command_buffer, const FractalSettings& settings) override;
    void ApplySettings(const FractalSettings& settings) override;

private:
    klvk::Application* app_ = nullptr;
    size_t max_iterations{};
    klvk::RenderTransforms2d render_transforms_;

    klvk::GpuBuffer color_table_;
    klvk::Shader fullscreen_shader_;
    klvk::Shader fractal_shader_;
    klvk::DefineHandle def_inside_out_space_;
    klvk::DefineHandle def_color_mode_;
    size_t pipeline_shader_version_ = 0;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
