#pragma once

#include <array>
#include <vector>

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
    struct FrameResources
    {
        klvk::GpuBuffer color_table;
        klvk::GpuBuffer counters;
        size_t counters_size = 0;
        size_t color_version = 0;
        vk::DescriptorSet compute_set = nullptr;
        vk::DescriptorSet draw_set = nullptr;
    };

    klvk::Application* app_ = nullptr;
    size_t max_iterations{};
    klvk::RenderTransforms2d render_transforms_;

    std::array<FrameResources, klvk::Application::kFramesInFlight> frames_;
    FrameResources* active_frame_ = nullptr;
    std::vector<edt::Vec4f> colors_;
    size_t color_version_ = 0;
    size_t counters_size_ = 0;

    klvk::Shader fullscreen_shader_;
    klvk::Shader draw_shader_;
    klvk::Shader compute_shader_;
    klvk::DefineHandle def_compute_inside_out_space_;
    size_t pipelines_shader_version_ = 0;

    vk::UniqueDescriptorSetLayout compute_set_layout_;
    vk::UniqueDescriptorSetLayout draw_set_layout_;
    vk::UniqueDescriptorPool descriptor_pool_;

    klvk::PipelineLayout compute_pipeline_layout_;
    klvk::PipelineLayout draw_pipeline_layout_;
    vk::UniquePipeline compute_pipeline_;
    vk::UniquePipeline draw_pipeline_;
};
