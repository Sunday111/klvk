#include "klvk/rendering/curve_renderer_2d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{
namespace
{

struct alignas(16) PushConstants
{
    Vec4f transform_x{};
    Vec4f transform_y{};
    Vec2f viewport_size{};
    float thickness = 0.f;
    float segment_pixel_length = 0.f;
    // Antialiasing half-width in pixels. 1 gives smooth, round-capped strokes
    // (CompositeMode::Union); 0 gives sharp, butt-capped strokes whose per-segment
    // quads do not overlap at joints (CompositeMode::Accumulate) so alpha-over
    // accumulation does not double-count them.
    float antialias = 1.f;
};

static_assert(sizeof(CurveRenderer2d::ControlPoint) == 24);
static_assert(sizeof(PushConstants) == 64);

// The fragment shader outputs premultiplied color (rgb*coverage*a, coverage*a).
// CompositeMode::Union takes the per-channel MAX so overlapping/self-intersecting
// coverage unions instead of alpha-over double-blending; it is correct only over a
// black/transparent clear (the direct-to-target consumers clear to (0,0,0,0)).
constexpr auto kColorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
constexpr auto kUnionBlend = vk::PipelineColorBlendAttachmentState{}
                                 .setBlendEnable(true)
                                 .setSrcColorBlendFactor(vk::BlendFactor::eOne)
                                 .setDstColorBlendFactor(vk::BlendFactor::eOne)
                                 .setColorBlendOp(vk::BlendOp::eMax)
                                 .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                                 .setDstAlphaBlendFactor(vk::BlendFactor::eOne)
                                 .setAlphaBlendOp(vk::BlendOp::eMax)
                                 .setColorWriteMask(kColorWriteMask);

// CompositeMode::Accumulate is premultiplied alpha-over - identical to straight
// alpha-over for the composited result, so many stacked or frame-accumulated
// strokes build up additively as before, now antialiased. Correct over any
// background; use this when curves accumulate into a persistent target.
constexpr auto kAccumulateBlend = vk::PipelineColorBlendAttachmentState{}
                                      .setBlendEnable(true)
                                      .setSrcColorBlendFactor(vk::BlendFactor::eOne)
                                      .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                                      .setColorBlendOp(vk::BlendOp::eAdd)
                                      .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                                      .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                                      .setAlphaBlendOp(vk::BlendOp::eAdd)
                                      .setColorWriteMask(kColorWriteMask);

size_t GrowCapacity(size_t required)
{
    size_t capacity = 1024;
    while (capacity < required)
    {
        ErrorHandling::Ensure(
            capacity <= std::numeric_limits<size_t>::max() / 2,
            "CurveRenderer2d: buffer size overflow");
        capacity *= 2;
    }
    return capacity;
}

}  // namespace

CurveRenderer2d::CurveRenderer2d(Application& app)
    : CurveRenderer2d(app, app.GetSwapchainFormat(), CompositeMode::Union)
{
}

CurveRenderer2d::CurveRenderer2d(Application& app, vk::Format color_format, CompositeMode composite)
    : app_(&app),
      composite_(composite)
{
    auto& context = app.GetDeviceContext();
    ErrorHandling::Ensure(
        context.IsTessellationShaderEnabled(),
        "CurveRenderer2d requires Vulkan tessellation-shader support");
    ErrorHandling::Ensure(context.IsGeometryShaderEnabled(), "CurveRenderer2d requires Vulkan geometry-shader support");

    constexpr vk::ShaderStageFlags push_stages =
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
        vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eGeometry;
    const std::array push_ranges{vk::PushConstantRange{push_stages, 0, sizeof(PushConstants)}};
    pipeline_layout_ = PipelineLayout{context, {}, push_ranges};
    pipeline_ = VulkanObject<vk::Pipeline>{
        context.GetDevice(),
        GraphicsPipelineBuilder(app)
            .Layout(pipeline_layout_)
            .VertexShaderFile(app.GetShaderDir() / "klvk/curve2d.vert.slang")
            .TessellationControlShaderFile(app.GetShaderDir() / "klvk/curve2d.hull.slang")
            .TessellationEvaluationShaderFile(app.GetShaderDir() / "klvk/curve2d.domain.slang")
            .GeometryShaderFile(app.GetShaderDir() / "klvk/curve2d.geom.slang")
            .FragmentShaderFile(app.GetShaderDir() / "klvk/curve2d.frag.slang")
            .Topology(vk::PrimitiveTopology::ePatchList)
            .PatchControlPoints(6)
            .VertexBinding(0, sizeof(ControlPoint), vk::VertexInputRate::eVertex)
            .VertexAttribute(0, 0, vk::Format::eR32G32Sfloat, offsetof(ControlPoint, position))
            .VertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ControlPoint, color))
            .Blend(composite == CompositeMode::Accumulate ? kAccumulateBlend : kUnionBlend)
            .ColorFormat(color_format)
            .Build()};
}

CurveRenderer2d::~CurveRenderer2d()
{
    app_->GetDeviceContext().WaitIdle();
}

void CurveRenderer2d::EnsureBuffers(size_t frame_index, size_t vertex_bytes, size_t index_bytes)
{
    auto& vertex_buffer = vertex_buffers_[frame_index];
    if (!vertex_buffer.IsValid() || vertex_buffer.GetSize() < vertex_bytes)
    {
        vertex_buffer = GpuBuffer(
            app_->GetDeviceContext(),
            vk::BufferUsageFlagBits::eVertexBuffer,
            GrowCapacity(vertex_bytes),
            GpuBufferHostAccess::SequentialWrite);
    }
    auto& index_buffer = index_buffers_[frame_index];
    if (!index_buffer.IsValid() || index_buffer.GetSize() < index_bytes)
    {
        index_buffer = GpuBuffer(
            app_->GetDeviceContext(),
            vk::BufferUsageFlagBits::eIndexBuffer,
            GrowCapacity(index_bytes),
            GpuBufferHostAccess::SequentialWrite);
    }
}

void CurveRenderer2d::Draw(
    std::span<const ControlPoint> points,
    Vec2f viewport_size,
    const Mat3f& world_to_view,
    float thickness,
    float segment_pixel_length)
{
    if (points.size() < 2) return;
    ErrorHandling::Ensure(
        points.size() <= std::numeric_limits<u32>::max(),
        "CurveRenderer2d: {} control points exceed the uint32 index range",
        points.size());
    ErrorHandling::Ensure(
        std::isfinite(viewport_size.x()) && std::isfinite(viewport_size.y()) && viewport_size.x() > 0.f &&
            viewport_size.y() > 0.f,
        "CurveRenderer2d: viewport dimensions must be positive");
    ErrorHandling::Ensure(
        std::isfinite(thickness) && thickness > 0.f,
        "CurveRenderer2d: thickness must be finite and positive");
    ErrorHandling::Ensure(
        std::isfinite(segment_pixel_length) && segment_pixel_length > 0.f,
        "CurveRenderer2d: segment pixel length must be finite and positive");
    ErrorHandling::Ensure(
        points.size() - 1 <= std::numeric_limits<u32>::max() / 6,
        "CurveRenderer2d: curve index count exceeds the uint32 draw range");

    indices_.clear();
    indices_.reserve((points.size() - 1) * 6);
    const auto last = static_cast<u32>(points.size() - 1);
    for (u32 segment = 0; segment != last; ++segment)
    {
        for (int offset = -2; offset != 4; ++offset)
        {
            const auto index = std::clamp<i64>(static_cast<i64>(segment) + offset, 0, last);
            indices_.push_back(static_cast<u32>(index));
        }
    }

    const size_t frame = app_->GetFrameInFlightIndex();
    const size_t vertex_bytes = points.size_bytes();
    const size_t index_bytes = indices_.size() * sizeof(u32);
    EnsureBuffers(frame, vertex_bytes, index_bytes);
    vertex_buffers_[frame].Write(std::as_bytes(points));
    index_buffers_[frame].Write(std::as_bytes(std::span{indices_}));

    const PushConstants constants{
        .transform_x = {world_to_view(0, 0), world_to_view(0, 1), world_to_view(0, 2), 0.f},
        .transform_y = {world_to_view(1, 0), world_to_view(1, 1), world_to_view(1, 2), 0.f},
        .viewport_size = viewport_size,
        .thickness = thickness,
        .segment_pixel_length = segment_pixel_length,
        .antialias = composite_ == CompositeMode::Union ? 1.f : 0.f,
    };
    const vk::CommandBuffer command_buffer = app_->GetCurrentCommandBuffer();
    const std::array vertex_buffers{vertex_buffers_[frame].GetHandle()};
    constexpr std::array<vk::DeviceSize, 1> offsets{0};
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
    command_buffer.bindVertexBuffers(0, vertex_buffers, offsets);
    command_buffer.bindIndexBuffer(index_buffers_[frame].GetHandle(), 0, vk::IndexType::eUint32);
    command_buffer.pushConstants<PushConstants>(
        pipeline_layout_.GetHandle(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
            vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eGeometry,
        0,
        constants);
    command_buffer.drawIndexed(static_cast<u32>(indices_.size()), 1, 0, 0, 0);
}

}  // namespace klvk
