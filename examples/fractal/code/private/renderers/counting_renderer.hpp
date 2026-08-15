#pragma once

#include "../graphics_utils.hpp"
#include "fractal_renderer.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"

class CountingRenderer : public FractalRenderer
{
public:
    CountingRenderer(klvk::Application& app, size_t max_iterations);
    ~CountingRenderer() noexcept override;

    void PrepareFrame(vk::CommandBuffer command_buffer, const FractalSettings& settings) override;
    void Render(vk::CommandBuffer command_buffer, const FractalSettings& settings) override;
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

    vk::DescriptorSetLayout compute_set_layout_ = nullptr;
    vk::DescriptorSetLayout draw_set_layout_ = nullptr;
    vk::DescriptorPool descriptor_pool_ = nullptr;
    vk::DescriptorSet compute_set_ = nullptr;
    vk::DescriptorSet draw_set_ = nullptr;

    klvk::PipelineLayout compute_pipeline_layout_;
    klvk::PipelineLayout draw_pipeline_layout_;
    vk::Pipeline compute_pipeline_ = nullptr;
    vk::Pipeline draw_pipeline_ = nullptr;
};
