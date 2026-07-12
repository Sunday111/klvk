#pragma once

#include "../graphics_utils.hpp"
#include "fractal_renderer.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"

class CountingRenderer : public FractalRenderer
{
public:
    CountingRenderer(klvk::Application& app, size_t max_iterations);
    ~CountingRenderer() noexcept override;

    void PrepareFrame(VkCommandBuffer command_buffer, const FractalSettings& settings) override;
    void Render(VkCommandBuffer command_buffer, const FractalSettings& settings) override;
    void ApplySettings(const FractalSettings& settings) override;

private:
    klvk::Application* app_ = nullptr;
    size_t max_iterations{};
    klvk::RenderTransforms2d render_transforms_;

    klvk::GpuBuffer color_table_;
    klvk::GpuBuffer counters_;
    size_t current_counters_size_ = 0;

    klvk::Shader fullscreen_shader_;
    klvk::Shader draw_shader_;
    klvk::Shader compute_shader_;
    klvk::DefineHandle def_compute_inside_out_space_;
    size_t pipelines_shader_version_ = 0;

    VkDescriptorSetLayout compute_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout draw_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_set_ = VK_NULL_HANDLE;
    VkDescriptorSet draw_set_ = VK_NULL_HANDLE;

    VkPipelineLayout compute_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout draw_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline compute_pipeline_ = VK_NULL_HANDLE;
    VkPipeline draw_pipeline_ = VK_NULL_HANDLE;
};
