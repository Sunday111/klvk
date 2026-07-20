#include "klvk/rendering/curve_renderer_2d.hpp"

#include <algorithm>

#include "EverydayTools/Math/Math.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{
namespace
{

struct Sample
{
    Vec2f position{};
    Vec4f color{};
};

struct Join
{
    Vec2f incoming_normal{};
    Vec2f outgoing_normal{};
    Vec2f v1{};
    Vec2f v2{};
    float turn = 0.f;
};

Vec2f CatmullRom(Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 +
            (-p0 + p1 * 3.f - p2 * 3.f + p3) * t3) *
           0.5f;
}

Vec2f ToPixelVector(Vec2f clip_vector, Vec2f viewport_size)
{
    return clip_vector * viewport_size * 0.5f;
}

Vec2f ToClipVector(Vec2f pixel_vector, Vec2f viewport_size)
{
    return pixel_vector * 2.f / viewport_size;
}

Join CalculateJoin(Vec2f previous, Vec2f current, Vec2f next, Vec2f viewport_size, float thickness)
{
    Vec2f incoming = ToPixelVector(current - previous, viewport_size);
    Vec2f outgoing = ToPixelVector(next - current, viewport_size);
    if (incoming.SquaredLength() < 1e-6f) incoming = outgoing;
    if (outgoing.SquaredLength() < 1e-6f) outgoing = incoming;
    if (incoming.SquaredLength() < 1e-6f) incoming = {1.f, 0.f};
    incoming = incoming.Normalized();
    outgoing = outgoing.Normalized();

    const float half_width = thickness * 0.5f;
    const Vec2f k1{-incoming.y(), incoming.x()};
    const Vec2f k2{-outgoing.y(), outgoing.x()};
    const Vec2f n1 = k1 * half_width;
    const Vec2f n2 = k2 * half_width;
    const Vec2f sum = k1 + k2;
    const float denominator = sum.Dot(k1);
    Vec2f miter = n1;
    Vec2f bisector = n1;
    if (sum.SquaredLength() >= 1e-6f && std::abs(denominator) >= 1e-3f)
    {
        miter = sum * (half_width / denominator);
        bisector = sum.Normalized() * half_width;
        constexpr float miter_limit = 4.f;
        const float max_length = half_width * miter_limit;
        if (miter.SquaredLength() > max_length * max_length) miter = miter.Normalized() * max_length;
    }
    const float turn = incoming.Cross(outgoing);
    return {
        .incoming_normal = ToClipVector(n1, viewport_size),
        .outgoing_normal = ToClipVector(n2, viewport_size),
        .v1 = ToClipVector(turn < 0.f ? -miter : -bisector, viewport_size),
        .v2 = ToClipVector(turn < 0.f ? bisector : miter, viewport_size),
        .turn = turn,
    };
}

Vec4u8 PackColor(Vec4f color)
{
    color = edt::Math::Clamp(color, 0.f, 1.f);
    return (color * 255.f).Cast<u8>();
}

}  // namespace

CurveRenderer2d::CurveRenderer2d(Application& app) : CurveRenderer2d(app, app.GetSwapchainFormat()) {}

CurveRenderer2d::CurveRenderer2d(Application& app, VkFormat color_format) : app_(&app)
{
    auto& context = app.GetDeviceContext();
    const VkDevice device = context.GetDevice();
    pipeline_layout_ = VkObject<VkPipelineLayout>{
        device,
        Vulkan::CreatePipelineLayout(device, {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO})};
    pipeline_ = VkObject<VkPipeline>{
        device,
        GraphicsPipelineBuilder(app)
            .Layout(pipeline_layout_)
            .VertexShaderFile(app.GetShaderDir() / "klvk" / "curve2d.vert")
            .FragmentShaderFile(app.GetShaderDir() / "klvk" / "curve2d.frag")
            .VertexBinding(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
            .VertexAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
            .VertexAttribute(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, color))
            .AlphaBlend()
            .ColorFormat(color_format)
            .Build()};
}

CurveRenderer2d::~CurveRenderer2d()
{
    // pipeline_ and pipeline_layout_ are VkObject members that destroy themselves;
    // wait first in case a runtime destruction races in-flight frames.
    app_->GetDeviceContext().WaitIdle();
}

void CurveRenderer2d::BuildVertices(
    std::span<const ControlPoint> points,
    float thickness,
    float segment_pixel_length,
    Vec2f viewport_size,
    const Mat3f& world_to_view,
    std::vector<Vertex>& out)
{
    out.clear();
    if (points.size() < 2) return;
    std::vector<Sample> samples;
    for (size_t segment = 0; segment + 1 < points.size(); ++segment)
    {
        const auto& a = points[segment];
        const auto& b = points[segment + 1];
        const Vec2f p0 = edt::Math::TransformPos(world_to_view, points[segment == 0 ? 0 : segment - 1].position);
        const Vec2f p1 = edt::Math::TransformPos(world_to_view, a.position);
        const Vec2f p2 = edt::Math::TransformPos(world_to_view, b.position);
        const Vec2f p3 =
            edt::Math::TransformPos(world_to_view, points[std::min(segment + 2, points.size() - 1)].position);
        const float pixel_length = ToPixelVector(p2 - p1, viewport_size).Length();
        const size_t steps = std::max<size_t>(1, static_cast<size_t>(std::ceil(pixel_length / segment_pixel_length)));
        for (size_t step = 0; step != steps; ++step)
        {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            samples.push_back({.position = CatmullRom(p0, p1, p2, p3, t), .color = a.color * (1.f - t) + b.color * t});
        }
    }
    samples.push_back(
        {.position = edt::Math::TransformPos(world_to_view, points.back().position), .color = points.back().color});

    std::vector<Join> joins(samples.size());
    for (size_t i = 0; i != samples.size(); ++i)
    {
        const Vec2f previous = samples[i == 0 ? 0 : i - 1].position;
        const Vec2f next = samples[std::min(i + 1, samples.size() - 1)].position;
        joins[i] = CalculateJoin(previous, samples[i].position, next, viewport_size, thickness);
    }

    out.reserve((samples.size() - 1) * 12);
    auto emit_triangle = [&](Vec2f a, Vec2f b, Vec2f c, Vec4f ca, Vec4f cb, Vec4f cc)
    {
        out.push_back({.position = a, .color = PackColor(ca)});
        out.push_back({.position = b, .color = PackColor(cb)});
        out.push_back({.position = c, .color = PackColor(cc)});
    };
    for (size_t i = 0; i + 1 < samples.size(); ++i)
    {
        const auto& a = samples[i];
        const auto& b = samples[i + 1];
        const auto& start = joins[i];
        const auto& end = joins[i + 1];
        const Vec2f start_plus = start.turn < 0.f ? start.outgoing_normal : start.v2;
        const Vec2f start_minus = start.turn < 0.f ? start.v1 : -start.outgoing_normal;
        const Vec2f end_plus = end.turn < 0.f ? end.incoming_normal : end.v2;
        const Vec2f end_minus = end.turn < 0.f ? end.v1 : -end.incoming_normal;
        emit_triangle(
            a.position + start_plus,
            a.position + start_minus,
            b.position + end_plus,
            a.color,
            a.color,
            b.color);
        emit_triangle(
            b.position + end_plus,
            a.position + start_minus,
            b.position + end_minus,
            b.color,
            a.color,
            b.color);

        if (std::abs(end.turn) > 1e-6f)
        {
            const Vec2f next_corner = end.turn < 0.f ? end.outgoing_normal : -end.outgoing_normal;
            const Vec2f first = end.turn < 0.f ? end.v2 : end.v1;
            const Vec2f connector_side = end.turn < 0.f ? end_minus : end_plus;
            emit_triangle(b.position + end_plus, b.position + end_minus, b.position + first, b.color, b.color, b.color);
            emit_triangle(
                b.position + first,
                b.position + connector_side,
                b.position + next_corner,
                b.color,
                b.color,
                b.color);
        }
    }
}

void CurveRenderer2d::EnsureBuffer(size_t frame_index, size_t bytes)
{
    auto& buffer = buffers_[frame_index];
    if (buffer.IsValid() && buffer.GetSize() >= bytes) return;
    size_t capacity = 1024;
    while (capacity < bytes) capacity *= 2;
    buffer = GpuBuffer(app_->GetDeviceContext(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, capacity, true);
}

void CurveRenderer2d::DrawVertices(std::span<const Vertex> vertices)
{
    if (vertices.empty()) return;
    const size_t frame = app_->GetFrameInFlightIndex();
    EnsureBuffer(frame, vertices.size() * sizeof(Vertex));
    buffers_[frame].Write(std::as_bytes(vertices));
    const VkCommandBuffer command_buffer = app_->GetCurrentCommandBuffer();
    const std::array vertex_buffers{buffers_[frame].GetHandle()};
    const std::array<VkDeviceSize, 1> offsets{0};
    Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    Vulkan::CmdBindVertexBuffers(command_buffer, 0, vertex_buffers, offsets);
    Vulkan::CmdDraw(command_buffer, static_cast<u32>(vertices.size()), 1, 0, 0);
}

}  // namespace klvk
