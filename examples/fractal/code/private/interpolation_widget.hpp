#pragma once

#include "graphics_utils.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"

class FractalSettings;

class InterpolationWidget
{
public:
    InterpolationWidget(klvk::Application& app, size_t num_colors);
    ~InterpolationWidget() noexcept;

    void Render(VkCommandBuffer command_buffer, const klvk::Viewport& viewport, const FractalSettings& settings);

private:
    klvk::Application* app_ = nullptr;
    size_t num_colors_{};

    std::array<klvk::GpuBuffer, klvk::Application::kFramesInFlight> color_buffers_{};
    std::array<VkDescriptorSet, klvk::Application::kFramesInFlight> descriptor_sets_{};

    klvk::Shader fullscreen_shader_;
    klvk::Shader widget_shader_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
