#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"

#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

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
    VkDevice device = context.GetDevice();

    descriptor_sets_ = DescriptorSets::Builder(context)
                           .Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                           .Binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
                           .Build(Application::kFramesInFlight);
    for (size_t frame = 0; frame != Application::kFramesInFlight; ++frame)
    {
        descriptor_sets_.WriteImage(frame, 0, texture.GetView(), texture.GetSampler());
    }

    {
        const std::array push_constant_ranges{VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(PushConstants),
        }};
        const std::array set_layouts{descriptor_sets_.GetLayout()};
        const VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = set_layouts.size(),
            .pSetLayouts = set_layouts.data(),
            .pushConstantRangeCount = push_constant_ranges.size(),
            .pPushConstantRanges = push_constant_ranges.data(),
        };
        pipeline_layout_ = VkObject<VkPipelineLayout>{device, Vulkan::CreatePipelineLayout(device, layout_info)};
    }

    pipeline_ = VkObject<VkPipeline>{
        device,
        GraphicsPipelineBuilder(app)
            .Layout(pipeline_layout_)
            .VertexShaderFile(app.GetShaderDir() / "klvk/instanced_sprite.vert")
            .FragmentShaderFile(app.GetShaderDir() / "klvk/instanced_sprite.frag")
            .AlphaBlend()
            .Build()};
}

InstancedSpriteRenderer2d::~InstancedSpriteRenderer2d()
{
    // The pipeline, layout and descriptor sets are VkObject/DescriptorSets members
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
    buffer = GpuBuffer(app_->GetDeviceContext(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, new_size, true);
    descriptor_sets_.WriteBuffer(frame_index, 1, buffer.GetHandle(), VK_WHOLE_SIZE);
}

void InstancedSpriteRenderer2d::Render(const Mat3f& world_to_view)
{
    if (instances_.empty()) return;

    const size_t frame_index = app_->GetFrameInFlightIndex();
    VkCommandBuffer command_buffer = app_->GetCurrentCommandBuffer();

    EnsureFrameBufferCapacity(frame_index, instances_.size() * sizeof(Instance));
    instance_buffers_[frame_index].Write(std::as_bytes(std::span{instances_}));

    const std::array descriptor_sets{descriptor_sets_.Get(frame_index)};
    Vulkan::CmdBindPipelineNE(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    Vulkan::CmdBindDescriptorSetsNE(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_,
        0,
        descriptor_sets);

    // The shader constructs the mat3 from columns.
    PushConstants push_constants{};
    for (size_t column = 0; column != 3; ++column)
    {
        const Vec3f matrix_column = world_to_view.GetColumn(column);
        push_constants.columns[column] = Vec4f{matrix_column, 0.f};
    }
    Vulkan::CmdPushConstantsNE(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, push_constants);

    Vulkan::CmdDrawNE(command_buffer, 6, static_cast<u32>(instances_.size()), 0, 0);
}

}  // namespace klvk
