#include "counting_renderer.hpp"

#include "../fractal_settings.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace
{

// Only vec2 u_resolution goes to the counting draw shader.
struct DrawPushConstants
{
    edt::Vec2f resolution{};
};

}  // namespace

CountingRenderer::CountingRenderer(klvk::Application& app, size_t max_iterations_)
    : app_(&app),
      max_iterations(max_iterations_),
      fullscreen_shader_(app.GetDeviceContext(), "fractal_example/fullscreen"),
      draw_shader_(app.GetDeviceContext(), "fractal_example/counting_draw"),
      compute_shader_(app.GetDeviceContext(), "fractal_example/counting_compute")
{
    klvk::DeviceContext& context = app.GetDeviceContext();
    VkDevice device = context.GetDevice();

    const auto iterations = static_cast<i32>(max_iterations);
    draw_shader_.SetDefineValue(draw_shader_.GetDefine("MAX_ITERATIONS"), iterations);
    compute_shader_.SetDefineValue(compute_shader_.GetDefine("MAX_ITERATIONS"), iterations);
    def_compute_inside_out_space_ = compute_shader_.GetDefine("INSIDE_OUT_SPACE");

    color_table_ =
        klvk::GpuBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (max_iterations + 1) * sizeof(edt::Vec4f), true);

    {
        const VkDescriptorSetLayoutBinding binding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
        compute_set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &binding,
            });
    }

    {
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        draw_set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<u32>(bindings.size()),
                .pBindings = bindings.data(),
            });
    }

    const VkDescriptorPoolSize pool_size{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 3};
    descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 2,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size,
        });

    const std::array layouts{compute_set_layout_, draw_set_layout_};
    const std::vector<VkDescriptorSet> sets = klvk::Vulkan::AllocateDescriptorSets(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptor_pool_,
            .descriptorSetCount = static_cast<u32>(layouts.size()),
            .pSetLayouts = layouts.data(),
        });
    compute_set_ = sets[0];
    draw_set_ = sets[1];

    {
        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(FractalPushConstants),
        };
        compute_pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &compute_set_layout_,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &push_constant_range,
            });
    }

    {
        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(DrawPushConstants),
        };
        draw_pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &draw_set_layout_,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &push_constant_range,
            });
    }
}

CountingRenderer::~CountingRenderer() noexcept
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
    VkDevice device = context.GetDevice();
    klvk::Vulkan::DestroyPipelineNE(device, draw_pipeline_);
    klvk::Vulkan::DestroyPipelineNE(device, compute_pipeline_);
    klvk::Vulkan::DestroyPipelineLayoutNE(device, draw_pipeline_layout_);
    klvk::Vulkan::DestroyPipelineLayoutNE(device, compute_pipeline_layout_);
    klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
    klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, draw_set_layout_);
    klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, compute_set_layout_);
}

void CountingRenderer::ApplySettings(const FractalSettings& settings)
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    VkDevice device = context.GetDevice();

    compute_shader_.SetDefineValue(def_compute_inside_out_space_, settings.inside_out_space ? 1 : 0);

    // Rebuild only when a define actually changed - color edits skip the wait.
    if (compute_pipeline_ == VK_NULL_HANDLE || pipelines_shader_version_ != compute_shader_.GetVersion())
    {
        // Old pipelines may still be referenced by the frame in flight.
        context.WaitIdle();
        klvk::Vulkan::DestroyPipelineNE(device, compute_pipeline_);
        klvk::Vulkan::DestroyPipelineNE(device, draw_pipeline_);

        const VkComputePipelineCreateInfo compute_info{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = compute_shader_.MakeShaderStages().front(),
            .layout = compute_pipeline_layout_,
        };
        compute_pipeline_ =
            klvk::Vulkan::CreateComputePipelines(device, VK_NULL_HANDLE, std::span{&compute_info, 1}).front();

        auto stages = fullscreen_shader_.MakeShaderStages();
        const auto fragment_stages = draw_shader_.MakeShaderStages();
        stages.insert(stages.end(), fragment_stages.begin(), fragment_stages.end());
        draw_pipeline_ = CreateFullscreenPipeline(*app_, draw_pipeline_layout_, stages);
        pipelines_shader_version_ = compute_shader_.GetVersion();
    }

    const auto resolution = settings.viewport.size.Cast<size_t>();
    const size_t num_pixels = resolution.x() * resolution.y();
    if (current_counters_size_ != num_pixels)
    {
        counters_ = klvk::GpuBuffer(
            app_->GetDeviceContext(),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            num_pixels * sizeof(u32),
            false);
        current_counters_size_ = num_pixels;
    }

    const VkDescriptorBufferInfo counters_info{.buffer = counters_.GetHandle(), .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo colors_info{.buffer = color_table_.GetHandle(), .range = VK_WHOLE_SIZE};
    const std::array<VkWriteDescriptorSet, 3> writes{
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = compute_set_,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &counters_info,
        },
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = draw_set_,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &counters_info,
        },
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = draw_set_,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &colors_info,
        },
    };
    klvk::Vulkan::UpdateDescriptorSets(device, writes);

    std::vector<edt::Vec4f> colors(max_iterations + 1);
    settings.ComputeColors(
        colors.size(),
        [&](size_t index, const edt::Vec3f& color) { colors[index] = edt::Vec4f{color, 1.f}; });
    color_table_.Write(std::as_bytes(std::span{colors}));
}

void CountingRenderer::PrepareFrame(VkCommandBuffer command_buffer, const FractalSettings& settings)
{
    if (compute_pipeline_ == VK_NULL_HANDLE || !counters_.IsValid()) return;

    render_transforms_.Update(settings.camera, settings.viewport);

    auto global_barrier = [&](VkPipelineStageFlags2 source_stage,
                              VkAccessFlags2 source_access,
                              VkPipelineStageFlags2 destination_stage,
                              VkAccessFlags2 destination_access)
    {
        const VkMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = source_stage,
            .srcAccessMask = source_access,
            .dstStageMask = destination_stage,
            .dstAccessMask = destination_access,
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier,
        };
        klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
    };

    // Previous frame's fragment reads must finish before the counters are cleared.
    global_barrier(
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT);
    klvk::Vulkan::CmdFillBuffer(command_buffer, counters_.GetHandle(), 0, VK_WHOLE_SIZE, 0);
    global_barrier(
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
    klvk::Vulkan::CmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        compute_pipeline_layout_,
        0,
        std::span{&compute_set_, 1});

    const FractalPushConstants push_constants = MakeFractalPushConstants(settings, render_transforms_.screen_to_world);
    klvk::Vulkan::CmdPushConstants(
        command_buffer,
        compute_pipeline_layout_,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        push_constants);

    constexpr u32 group_size = 16;
    const auto resolution = settings.viewport.size;
    klvk::Vulkan::CmdDispatch(
        command_buffer,
        (resolution.x() + group_size - 1) / group_size,
        (resolution.y() + group_size - 1) / group_size,
        1);

    global_barrier(
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

void CountingRenderer::Render(VkCommandBuffer command_buffer, const FractalSettings& settings)
{
    if (draw_pipeline_ == VK_NULL_HANDLE) return;

    CmdSetGlStyleViewport(command_buffer, settings.viewport, app_->GetWindow().GetSize());
    klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_pipeline_);
    klvk::Vulkan::CmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        draw_pipeline_layout_,
        0,
        std::span{&draw_set_, 1});

    const DrawPushConstants push_constants{.resolution = settings.viewport.size.Cast<float>()};
    klvk::Vulkan::CmdPushConstants(
        command_buffer,
        draw_pipeline_layout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        push_constants);

    klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
}
