#pragma once

#include <vector>

#include "../graphics_utils.hpp"
#include "fractal_renderer.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"

VK_DEFINE_HANDLE(VmaAllocation)

class SimpleCpuRenderer : public FractalRenderer
{
public:
    SimpleCpuRenderer(klvk::Application& app, size_t max_iterations);
    ~SimpleCpuRenderer() noexcept override;

    void PrepareFrame(vk::CommandBuffer command_buffer, const FractalSettings& settings) override;
    void Render(vk::CommandBuffer command_buffer, const FractalSettings& settings) override;
    void ApplySettings(const FractalSettings& settings) override;

private:
    void DestroyImage();

    klvk::Application* app_ = nullptr;
    size_t max_iterations{};
    klvk::RenderTransforms2d render_transforms_;

    std::vector<edt::Vec3f> pallette;
    std::vector<edt::Vec4u8> image_buffer_;

    edt::Vec2<size_t> image_size_{};
    vk::Image image_ = nullptr;
    VmaAllocation image_allocation_ = nullptr;
    vk::ImageView image_view_ = nullptr;
    vk::Sampler sampler_ = nullptr;
    bool image_initialized_ = false;

    std::array<klvk::GpuBuffer, klvk::Application::kFramesInFlight> staging_buffers_{};

    klvk::Shader fullscreen_shader_;
    klvk::Shader textured_quad_shader_;
    vk::DescriptorSetLayout set_layout_ = nullptr;
    klvk::DescriptorSetLayoutDescription set_layout_description_;
    vk::DescriptorPool descriptor_pool_ = nullptr;
    vk::DescriptorSet descriptor_set_ = nullptr;
    klvk::PipelineLayout pipeline_layout_;
    vk::Pipeline pipeline_ = nullptr;
};
