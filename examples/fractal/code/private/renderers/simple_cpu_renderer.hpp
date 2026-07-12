#pragma once

#include <vector>

#include "../graphics_utils.hpp"
#include "fractal_renderer.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"

VK_DEFINE_HANDLE(VmaAllocation)

class SimpleCpuRenderer : public FractalRenderer
{
public:
    SimpleCpuRenderer(klvk::Application& app, size_t max_iterations);
    ~SimpleCpuRenderer() noexcept override;

    void PrepareFrame(VkCommandBuffer command_buffer, const FractalSettings& settings) override;
    void Render(VkCommandBuffer command_buffer, const FractalSettings& settings) override;
    void ApplySettings(const FractalSettings& settings) override;

private:
    void DestroyImage();

    klvk::Application* app_ = nullptr;
    size_t max_iterations{};
    klvk::RenderTransforms2d render_transforms_;

    std::vector<edt::Vec3f> pallette;
    std::vector<edt::Vec4u8> image_buffer_;

    edt::Vec2<size_t> image_size_{};
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation image_allocation_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    bool image_initialized_ = false;

    std::array<klvk::GpuBuffer, klvk::Application::kFramesInFlight> staging_buffers_{};

    VkShaderModule vertex_shader_ = VK_NULL_HANDLE;
    VkShaderModule fragment_shader_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
