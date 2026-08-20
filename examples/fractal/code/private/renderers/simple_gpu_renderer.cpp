#include "simple_gpu_renderer.hpp"

#include "../fractal_settings.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"

SimpleGpuRenderer::SimpleGpuRenderer(klvk::Application& app, size_t max_iterations_)
    : app_(&app),
      max_iterations(max_iterations_),
      fullscreen_shader_(app.GetDeviceContext(), "fractal_example/fullscreen"),
      fractal_shader_(app.GetDeviceContext(), "fractal_example/fractal")
{
    klvk::DeviceContext& context = app.GetDeviceContext();
    vk::Device device = context.GetDevice();

    fractal_shader_.SetDefineValue(fractal_shader_.GetDefine("MAX_ITERATIONS"), static_cast<i32>(max_iterations));
    def_inside_out_space_ = fractal_shader_.GetDefine("INSIDE_OUT_SPACE");
    def_color_mode_ = fractal_shader_.GetDefine("COLOR_MODE");

    color_table_ = klvk::GpuBuffer(
        context,
        vk::BufferUsageFlagBits::eStorageBuffer,
        (max_iterations + 1) * sizeof(edt::Vec4f),
        true);

    const vk::DescriptorSetLayoutBinding binding = vk::DescriptorSetLayoutBinding{}
                                                       .setBinding(0)
                                                       .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                                                       .setDescriptorCount(1)
                                                       .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    set_layout_description_.bindings = {binding};
    const vk::DescriptorSetLayoutCreateInfo layout_info = vk::DescriptorSetLayoutCreateInfo{}.setBindings(binding);
    set_layout_ = device.createDescriptorSetLayoutUnique(layout_info);

    const vk::DescriptorPoolSize pool_size =
        vk::DescriptorPoolSize{}.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1);
    const vk::DescriptorPoolCreateInfo pool_info = vk::DescriptorPoolCreateInfo{}.setMaxSets(1).setPoolSizes(pool_size);
    descriptor_pool_ = device.createDescriptorPoolUnique(pool_info);
    const vk::DescriptorSetAllocateInfo allocate_info =
        vk::DescriptorSetAllocateInfo{}.setDescriptorPool(descriptor_pool_.get()).setSetLayouts(set_layout_.get());
    descriptor_set_ = device.allocateDescriptorSets(allocate_info).front();

    const vk::DescriptorBufferInfo buffer_info =
        vk::DescriptorBufferInfo{}.setBuffer(color_table_.GetHandle()).setRange(vk::WholeSize);
    const vk::WriteDescriptorSet write = vk::WriteDescriptorSet{}
                                             .setDstSet(descriptor_set_)
                                             .setDstBinding(0)
                                             .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                                             .setBufferInfo(buffer_info);
    device.updateDescriptorSets(std::span{&write, 1}, {});

    const vk::PushConstantRange push_constant_range =
        vk::PushConstantRange{}.setStageFlags(vk::ShaderStageFlagBits::eFragment).setSize(sizeof(FractalPushConstants));
    const klvk::DescriptorSetLayoutView set_layout_view{
        .handle = set_layout_.get(),
        .description = &set_layout_description_,
    };
    pipeline_layout_ =
        klvk::PipelineLayout{context, std::span{&set_layout_view, 1}, std::span{&push_constant_range, 1}};
}

SimpleGpuRenderer::~SimpleGpuRenderer() noexcept
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
}

void SimpleGpuRenderer::ApplySettings(const FractalSettings& settings)
{
    fractal_shader_.SetDefineValue(def_inside_out_space_, settings.inside_out_space ? 1 : 0);
    fractal_shader_.SetDefineValue(def_color_mode_, settings.color_mode);

    // Rebuild only when a define actually changed - color edits skip the wait.
    if (!pipeline_ || pipeline_shader_version_ != fractal_shader_.GetVersion())
    {
        klvk::DeviceContext& context = app_->GetDeviceContext();
        context.WaitIdle();  // the old pipeline may still be referenced by the frame in flight
        pipeline_.reset();

        auto stages = fullscreen_shader_.MakeStages();
        stages.Append(fractal_shader_.MakeStages());
        pipeline_ = CreateFullscreenPipeline(*app_, pipeline_layout_, stages);
        pipeline_shader_version_ = fractal_shader_.GetVersion();
    }

    std::vector<edt::Vec4f> colors(max_iterations + 1);
    settings.ComputeColors(
        colors.size(),
        [&](size_t index, const edt::Vec3f& color) { colors[index] = edt::Vec4f{color, 1.f}; });
    color_table_.Write(std::as_bytes(std::span{colors}));
}

void SimpleGpuRenderer::Render(vk::CommandBuffer command_buffer, const FractalSettings& settings)
{
    UpdateFractalRenderTransforms(render_transforms_, settings);

    CmdSetGlStyleViewport(command_buffer, settings.viewport, app_->GetWindow().GetSize());
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipeline_layout_.GetHandle(),
        0,
        std::span{&descriptor_set_, 1},
        {});

    const FractalPushConstants push_constants = MakeFractalPushConstants(settings, render_transforms_.screen_to_world);
    command_buffer.pushConstants(
        pipeline_layout_.GetHandle(),
        vk::ShaderStageFlagBits::eFragment,
        0,
        sizeof(push_constants),
        &push_constants);

    command_buffer.draw(6, 1, 0, 0);
}
