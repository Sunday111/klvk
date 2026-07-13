#pragma once

#include <array>
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

    explicit CurveRenderer2d(Application& app);
    CurveRenderer2d(Application& app, VkFormat color_format);
    CurveRenderer2d(const CurveRenderer2d&) = delete;
    CurveRenderer2d(CurveRenderer2d&&) = delete;
    ~CurveRenderer2d();

    void SetPoints(std::span<const ControlPoint> points);
    void Draw(Vec2f viewport_size, const Mat3f& world_to_view);

    // Same defaults as klgl's CurveRenderer2d.
    float thickness_ = 5.f;
    float segment_pixel_length_ = 8.f;

private:
    struct Vertex
    {
        Vec2f position{};
        Vec4u8 color{};
    };

    void EnsureBuffer(size_t frame_index, size_t bytes);
    std::vector<Vertex> BuildVertices(Vec2f viewport_size, const Mat3f& world_to_view) const;

    Application* app_ = nullptr;
    std::vector<ControlPoint> points_;
    VkObject<VkPipelineLayout> pipeline_layout_;
    VkObject<VkPipeline> pipeline_;
    std::array<GpuBuffer, Application::kFramesInFlight> buffers_{};
};

}  // namespace klvk
