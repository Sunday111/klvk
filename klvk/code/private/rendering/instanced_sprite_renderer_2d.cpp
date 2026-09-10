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
    texture_view_ = texture.GetView();
    texture_sampler_ = texture.GetSampler();
    frames_.front().batches.push_back(CreateBatch());

    {
        const std::array push_constant_ranges{
            vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstants)}};
        const std::array set_layouts{frames_.front().batches.front().descriptor_sets.GetLayoutView()};
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

InstancedSpriteRenderer2d::Batch InstancedSpriteRenderer2d::CreateBatch()
{
    Batch batch;
    batch.descriptor_sets =
        DescriptorSets::Builder(app_->GetDeviceContext())
            .Binding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
            .Binding(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex)
            .Build();
    batch.descriptor_sets.WriteImage(0, 0, texture_view_, texture_sampler_);
    return batch;
}

InstancedSpriteRenderer2d::Batch& InstancedSpriteRenderer2d::AcquireBatch(size_t bytes)
{
    auto& frame = frames_[app_->GetFrameInFlightIndex()];
    if (frame.frame_number != app_->GetFrameNumber())
    {
        frame.frame_number = app_->GetFrameNumber();
        frame.next_batch = 0;
    }
    if (frame.next_batch == frame.batches.size()) frame.batches.push_back(CreateBatch());
    auto& batch = frame.batches[frame.next_batch];
    if (!batch.buffer.IsValid() || batch.buffer.GetSize() < bytes)
    {
        size_t capacity = 1024;
        while (capacity < bytes) capacity *= 2;
        batch.buffer = GpuBuffer(app_->GetDeviceContext(), vk::BufferUsageFlagBits::eStorageBuffer, capacity, true);
        batch.descriptor_sets.WriteBuffer(0, 1, batch.buffer.GetHandle(), vk::WholeSize);
    }
    ++frame.next_batch;
    return batch;
}

void InstancedSpriteRenderer2d::Render(const Mat3f& world_to_view)
{
    if (instances_.empty()) return;

    vk::CommandBuffer command_buffer = app_->GetCurrentCommandBuffer();

    auto& batch = AcquireBatch(instances_.size() * sizeof(Instance));
    batch.buffer.Write(std::as_bytes(std::span{instances_}));

    const std::array descriptor_sets{batch.descriptor_sets.Get(0)};
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
