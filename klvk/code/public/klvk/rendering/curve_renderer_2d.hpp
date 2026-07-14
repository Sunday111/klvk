#pragma once

#include <array>
#include <span>
#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/application.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
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

    // The stroke geometry produced by BuildVertices: a triangle list already in
    // clip space, so the vertex shader is a pass-through.
    struct Vertex
    {
        Vec2f position{};
        Vec4u8 color{};
    };

    explicit CurveRenderer2d(Application& app);
    CurveRenderer2d(Application& app, VkFormat color_format);
    CurveRenderer2d(const CurveRenderer2d&) = delete;
    CurveRenderer2d(CurveRenderer2d&&) = delete;
    ~CurveRenderer2d();

    // Tessellates a curve (Catmull-Rom sampling plus miter joins) into out. This
    // is pure CPU work touching no instance or GPU state, so it is safe to call
    // from any thread - e.g. on the producer threads that generate the curves.
    static void BuildVertices(
        std::span<const ControlPoint> points,
        float thickness,
        float segment_pixel_length,
        Vec2f viewport_size,
        const Mat3f& world_to_view,
        std::vector<Vertex>& out);

    // Uploads pre-built vertices into this frame's buffer and records the draw
    // into the application's current command buffer. Must run on the render thread.
    void DrawVertices(std::span<const Vertex> vertices);

private:
    void EnsureBuffer(size_t frame_index, size_t bytes);

    Application* app_ = nullptr;
    VkObject<VkPipelineLayout> pipeline_layout_;
    VkObject<VkPipeline> pipeline_;
    std::array<GpuBuffer, Application::kFramesInFlight> buffers_{};
};

}  // namespace klvk
