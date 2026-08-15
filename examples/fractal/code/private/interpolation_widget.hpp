#pragma once

#include "graphics_utils.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"

class FractalSettings;

class InterpolationWidget
{
public:
    InterpolationWidget(klvk::Application& app, size_t num_colors);
    ~InterpolationWidget() noexcept;

    void Render(vk::CommandBuffer command_buffer, const klvk::Viewport& viewport, const FractalSettings& settings);

private:
    klvk::Application* app_ = nullptr;
    size_t num_colors_{};

    std::array<klvk::GpuBuffer, klvk::Application::kFramesInFlight> color_buffers_{};
    std::array<vk::DescriptorSet, klvk::Application::kFramesInFlight> descriptor_sets_{};

    klvk::Shader fullscreen_shader_;
    klvk::Shader widget_shader_;
    vk::UniqueDescriptorSetLayout set_layout_;
    klvk::DescriptorSetLayoutDescription set_layout_description_;
    vk::UniqueDescriptorPool descriptor_pool_;
    klvk::PipelineLayout pipeline_layout_;
    vk::UniquePipeline pipeline_;
};
