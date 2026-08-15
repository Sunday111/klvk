#pragma once

#include <array>
#include <span>
#include <vector>

#include "edt/math/matrix.hpp"
#include "klvk/application.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"
#include "klvk/vulkan/vulkan.hpp"

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

    // How overlapping coverage combines. Both antialias.
    enum class CompositeMode : u8
    {
        // Overlapping coverage unions (max): a few self-intersecting strokes read
        // as one silhouette with no double-blend. Correct only over a black clear.
        Union,
        // Overlapping coverage composites with alpha-over, so many stacked or
        // frame-accumulated strokes build up additively (e.g. a curve fractal that
        // accumulates into a persistent target). Correct over any background.
        Accumulate,
    };

    explicit CurveRenderer2d(Application& app);
    CurveRenderer2d(Application& app, vk::Format color_format, CompositeMode composite = CompositeMode::Union);
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
    CompositeMode composite_ = CompositeMode::Union;
    PipelineLayout pipeline_layout_;
    vk::UniquePipeline pipeline_;
    std::array<GpuBuffer, Application::kFramesInFlight> vertex_buffers_{};
    std::array<GpuBuffer, Application::kFramesInFlight> index_buffers_{};
    std::vector<u32> indices_;
};

}  // namespace klvk
