#pragma once

#include <array>
#include <span>
#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/application.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"
#include "klvk/vulkan/vk_object.hpp"

namespace klvk
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

class CurveRenderer2d
{
public:
    struct ControlPoint
    {
        Vec2f position{};
        Vec4f color{};
    };

    explicit CurveRenderer2d(Application& app);
    CurveRenderer2d(Application& app, VkFormat color_format);
    CurveRenderer2d(const CurveRenderer2d&) = delete;
    CurveRenderer2d(CurveRenderer2d&&) = delete;
    ~CurveRenderer2d();

    // Uploads compact control points and records a GPU-tessellated draw. Call at
    // most once per renderer per frame: another call overwrites this frame's
    // persistently mapped vertex and index buffers before submission.
    void Draw(
        std::span<const ControlPoint> points,
        Vec2f viewport_size,
        const Mat3f& world_to_view,
        float thickness,
        float segment_pixel_length);

private:
    void EnsureBuffers(size_t frame_index, size_t vertex_bytes, size_t index_bytes);

    Application* app_ = nullptr;
    PipelineLayout pipeline_layout_;
    VkObject<VkPipeline> pipeline_;
    std::array<GpuBuffer, Application::kFramesInFlight> vertex_buffers_{};
    std::array<GpuBuffer, Application::kFramesInFlight> index_buffers_{};
    std::vector<u32> indices_;
};

}  // namespace klvk
