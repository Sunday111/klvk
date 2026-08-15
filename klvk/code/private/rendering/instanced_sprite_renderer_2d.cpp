#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"

#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

namespace
{

// The world-to-view matrix as three vec4 columns, matching the push constant block layout.
struct PushConstants
{
    std::array<edt::Vec4f, 3> columns;
};

}  // namespace

InstancedSpriteRenderer2d::InstancedSpriteRenderer2d(Application& app, const Texture& texture) : app_(&app)
{
    DeviceContext& context = app.GetDeviceContext();
    descriptor_sets_ = DescriptorSets::Builder(context)
                           .Binding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
                           .Binding(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex)
                           .Build(Application::kFramesInFlight);
    for (size_t frame = 0; frame != Application::kFramesInFlight; ++frame)
    {
        descriptor_sets_.WriteImage(frame, 0, texture.GetView(), texture.GetSampler());
    }

    {
        const std::array push_constant_ranges{
            vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstants)}};
        const std::array set_layouts{descriptor_sets_.GetLayoutView()};
        pipeline_layout_ = PipelineLayout{context, set_layouts, push_constant_ranges};
    }

    pipeline_ = GraphicsPipelineBuilder(app)
                    .Layout(pipeline_layout_)
                    .VertexShaderFile(app.GetShaderDir() / "klvk/instanced_sprite.vert.slang")
                    .FragmentShaderFile(app.GetShaderDir() / "klvk/instanced_sprite.frag.slang")
                    .AlphaBlend()
                    .Build();
}

InstancedSpriteRenderer2d::~InstancedSpriteRenderer2d()
{
    // The pipeline, layout and descriptor sets are owning members
    // that destroy themselves; wait first in case a runtime destruction races
    // in-flight frames (at shutdown Application::Run has already waited).
    app_->GetDeviceContext().WaitIdle();
}

void InstancedSpriteRenderer2d::EnsureFrameBufferCapacity(size_t frame_index, size_t bytes)
{
    GpuBuffer& buffer = instance_buffers_[frame_index];
    if (buffer.IsValid() && buffer.GetSize() >= bytes) return;

    size_t new_size = 1024;
    while (new_size < bytes) new_size *= 2;

    // The application waited on this frame slot's fence in PreTick, so the GPU
    // is done with the old buffer and it can be destroyed right away.
    buffer = GpuBuffer(app_->GetDeviceContext(), vk::BufferUsageFlagBits::eStorageBuffer, new_size, true);
    descriptor_sets_.WriteBuffer(frame_index, 1, buffer.GetHandle(), vk::WholeSize);
}

void InstancedSpriteRenderer2d::Render(const Mat3f& world_to_view)
{
    if (instances_.empty()) return;

    const size_t frame_index = app_->GetFrameInFlightIndex();
    vk::CommandBuffer command_buffer = app_->GetCurrentCommandBuffer();

    EnsureFrameBufferCapacity(frame_index, instances_.size() * sizeof(Instance));
    instance_buffers_[frame_index].Write(std::as_bytes(std::span{instances_}));

    const std::array descriptor_sets{descriptor_sets_.Get(frame_index)};
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    command_buffer
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout_.GetHandle(), 0, descriptor_sets, {});

    // The shader constructs the mat3 from columns.
    PushConstants push_constants{};
    for (size_t column = 0; column != 3; ++column)
    {
        const Vec3f matrix_column = world_to_view.GetColumn(column);
        push_constants.columns[column] = Vec4f{matrix_column, 0.f};
    }
    command_buffer.pushConstants<PushConstants>(
        pipeline_layout_.GetHandle(),
        vk::ShaderStageFlagBits::eVertex,
        0,
        push_constants);

    command_buffer.draw(6, static_cast<u32>(instances_.size()), 0, 0);
}

}  // namespace klvk
