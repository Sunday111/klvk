#include "counting_renderer.hpp"

#include <array>

#include "../fractal_settings.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan.hpp"

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
    vk::Device device = context.GetDevice();

    const auto iterations = static_cast<i32>(max_iterations);
    draw_shader_.SetDefineValue(draw_shader_.GetDefine("MAX_ITERATIONS"), iterations);
    compute_shader_.SetDefineValue(compute_shader_.GetDefine("MAX_ITERATIONS"), iterations);
    def_compute_inside_out_space_ = compute_shader_.GetDefine("INSIDE_OUT_SPACE");

    color_table_ = klvk::GpuBuffer(
        context,
        vk::BufferUsageFlagBits::eStorageBuffer,
        (max_iterations + 1) * sizeof(edt::Vec4f),
        true);

    {
        const vk::DescriptorSetLayoutBinding binding = vk::DescriptorSetLayoutBinding{}
                                                           .setBinding(0)
                                                           .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                                                           .setDescriptorCount(1)
                                                           .setStageFlags(vk::ShaderStageFlagBits::eCompute);
        const vk::DescriptorSetLayoutCreateInfo layout_info = vk::DescriptorSetLayoutCreateInfo{}.setBindings(binding);
        compute_set_layout_ = device.createDescriptorSetLayoutUnique(layout_info);
    }

    {
        const std::array<vk::DescriptorSetLayoutBinding, 2> bindings{
            vk::DescriptorSetLayoutBinding{}
                .setBinding(0)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eFragment),
            vk::DescriptorSetLayoutBinding{}
                .setBinding(1)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        };
        const vk::DescriptorSetLayoutCreateInfo layout_info = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);
        draw_set_layout_ = device.createDescriptorSetLayoutUnique(layout_info);
    }

    const vk::DescriptorPoolSize pool_size =
        vk::DescriptorPoolSize{}.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(3);
    const vk::DescriptorPoolCreateInfo pool_info = vk::DescriptorPoolCreateInfo{}.setMaxSets(2).setPoolSizes(pool_size);
    descriptor_pool_ = device.createDescriptorPoolUnique(pool_info);

    const std::array layouts{compute_set_layout_.get(), draw_set_layout_.get()};
    const vk::DescriptorSetAllocateInfo allocate_info =
        vk::DescriptorSetAllocateInfo{}.setDescriptorPool(descriptor_pool_.get()).setSetLayouts(layouts);
    const std::vector<vk::DescriptorSet> sets = device.allocateDescriptorSets(allocate_info);
    compute_set_ = sets[0];
    draw_set_ = sets[1];

    {
        const klvk::DescriptorSetLayoutDescription set_description{
            .bindings = {vk::DescriptorSetLayoutBinding{}
                             .setBinding(0)
                             .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                             .setDescriptorCount(1)
                             .setStageFlags(vk::ShaderStageFlagBits::eCompute)}};
        const std::array set_layouts{klvk::DescriptorSetLayoutView{
            .handle = compute_set_layout_.get(),
            .description = &set_description,
        }};
        const vk::PushConstantRange push_constant_range = vk::PushConstantRange{}
                                                              .setStageFlags(vk::ShaderStageFlagBits::eCompute)
                                                              .setSize(sizeof(FractalPushConstants));
        compute_pipeline_layout_ = klvk::PipelineLayout(context, set_layouts, std::span{&push_constant_range, 1});
    }

    {
        const klvk::DescriptorSetLayoutDescription set_description{
            .bindings = {
                vk::DescriptorSetLayoutBinding{}
                    .setBinding(0)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDescriptorCount(1)
                    .setStageFlags(vk::ShaderStageFlagBits::eFragment),
                vk::DescriptorSetLayoutBinding{}
                    .setBinding(1)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDescriptorCount(1)
                    .setStageFlags(vk::ShaderStageFlagBits::eFragment),
            }};
        const std::array set_layouts{klvk::DescriptorSetLayoutView{
            .handle = draw_set_layout_.get(),
            .description = &set_description,
        }};
        const vk::PushConstantRange push_constant_range = vk::PushConstantRange{}
                                                              .setStageFlags(vk::ShaderStageFlagBits::eFragment)
                                                              .setSize(sizeof(DrawPushConstants));
        draw_pipeline_layout_ = klvk::PipelineLayout(context, set_layouts, std::span{&push_constant_range, 1});
    }
}

CountingRenderer::~CountingRenderer() noexcept
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
}

void CountingRenderer::ApplySettings(const FractalSettings& settings)
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    vk::Device device = context.GetDevice();

    compute_shader_.SetDefineValue(def_compute_inside_out_space_, settings.inside_out_space ? 1 : 0);

    // Rebuild only when a define actually changed - color edits skip the wait.
    if (!compute_pipeline_ || pipelines_shader_version_ != compute_shader_.GetVersion())
    {
        // Old pipelines may still be referenced by the frame in flight.
        context.WaitIdle();
        compute_pipeline_.reset();
        draw_pipeline_.reset();

        const klvk::ShaderStages compute_stages = compute_shader_.MakeStages();
        (void)compute_pipeline_layout_.Validate(compute_stages);
        const vk::ComputePipelineCreateInfo compute_info = vk::ComputePipelineCreateInfo{}
                                                               .setStage(compute_stages.GetCreateInfos().front())
                                                               .setLayout(compute_pipeline_layout_.GetHandle());
        const std::array compute_infos{compute_info};
        auto outcome = device.createComputePipelinesUnique(nullptr, compute_infos);
        compute_pipeline_ = std::move(outcome.value.front());
        klvk::VulkanCheck(outcome.result);

        auto stages = fullscreen_shader_.MakeStages();
        stages.Append(draw_shader_.MakeStages());
        draw_pipeline_ = CreateFullscreenPipeline(*app_, draw_pipeline_layout_, stages);
        pipelines_shader_version_ = compute_shader_.GetVersion();
    }

    const auto resolution = settings.viewport.size.Cast<size_t>();
    const size_t num_pixels = resolution.x() * resolution.y();
    if (current_counters_size_ != num_pixels)
    {
        counters_ = klvk::GpuBuffer(
            app_->GetDeviceContext(),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            num_pixels * sizeof(u32),
            false);
        current_counters_size_ = num_pixels;
    }

    const vk::DescriptorBufferInfo counters_info =
        vk::DescriptorBufferInfo{}.setBuffer(counters_.GetHandle()).setRange(vk::WholeSize);
    const vk::DescriptorBufferInfo colors_info =
        vk::DescriptorBufferInfo{}.setBuffer(color_table_.GetHandle()).setRange(vk::WholeSize);
    const std::array<vk::WriteDescriptorSet, 3> writes{
        vk::WriteDescriptorSet{}
            .setDstSet(compute_set_)
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(counters_info),
        vk::WriteDescriptorSet{}
            .setDstSet(draw_set_)
            .setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(counters_info),
        vk::WriteDescriptorSet{}
            .setDstSet(draw_set_)
            .setDstBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(colors_info),
    };
    device.updateDescriptorSets(writes, {});

    std::vector<edt::Vec4f> colors(max_iterations + 1);
    settings.ComputeColors(
        colors.size(),
        [&](size_t index, const edt::Vec3f& color) { colors[index] = edt::Vec4f{color, 1.f}; });
    color_table_.Write(std::as_bytes(std::span{colors}));
}

void CountingRenderer::PrepareFrame(vk::CommandBuffer command_buffer, const FractalSettings& settings)
{
    if (!compute_pipeline_ || !counters_.IsValid()) return;

    render_transforms_.Update(settings.camera, settings.viewport);

    auto global_barrier = [&](vk::PipelineStageFlags2 source_stage,
                              vk::AccessFlags2 source_access,
                              vk::PipelineStageFlags2 destination_stage,
                              vk::AccessFlags2 destination_access)
    {
        const vk::MemoryBarrier2 barrier = vk::MemoryBarrier2{}
                                               .setSrcStageMask(source_stage)
                                               .setSrcAccessMask(source_access)
                                               .setDstStageMask(destination_stage)
                                               .setDstAccessMask(destination_access);
        const vk::DependencyInfo dependency = vk::DependencyInfo{}.setMemoryBarriers(barrier);
        command_buffer.pipelineBarrier2(dependency);
    };

    // Previous frame's fragment reads must finish before the counters are cleared.
    global_barrier(
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderStorageRead,
        vk::PipelineStageFlagBits2::eClear,
        vk::AccessFlagBits2::eTransferWrite);
    command_buffer.fillBuffer(counters_.GetHandle(), 0, vk::WholeSize, 0);
    global_barrier(
        vk::PipelineStageFlagBits2::eClear,
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite);

    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute_pipeline_.get());
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        compute_pipeline_layout_.GetHandle(),
        0,
        std::span{&compute_set_, 1},
        {});

    const FractalPushConstants push_constants = MakeFractalPushConstants(settings, render_transforms_.screen_to_world);
    command_buffer.pushConstants(
        compute_pipeline_layout_.GetHandle(),
        vk::ShaderStageFlagBits::eCompute,
        0,
        sizeof(push_constants),
        &push_constants);

    constexpr u32 group_size = 16;
    const auto resolution = settings.viewport.size;
    command_buffer.dispatch(
        (resolution.x() + group_size - 1) / group_size,
        (resolution.y() + group_size - 1) / group_size,
        1);

    global_barrier(
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderStorageRead);
}

void CountingRenderer::Render(vk::CommandBuffer command_buffer, const FractalSettings& settings)
{
    if (!draw_pipeline_) return;

    CmdSetGlStyleViewport(command_buffer, settings.viewport, app_->GetWindow().GetSize());
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, draw_pipeline_.get());
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        draw_pipeline_layout_.GetHandle(),
        0,
        std::span{&draw_set_, 1},
        {});

    const DrawPushConstants push_constants{.resolution = settings.viewport.size.Cast<float>()};
    command_buffer.pushConstants(
        draw_pipeline_layout_.GetHandle(),
        vk::ShaderStageFlagBits::eFragment,
        0,
        sizeof(push_constants),
        &push_constants);

    command_buffer.draw(6, 1, 0, 0);
}
