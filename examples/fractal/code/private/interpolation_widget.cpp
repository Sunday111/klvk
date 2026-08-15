#include "interpolation_widget.hpp"

#include "fractal_settings.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"

InterpolationWidget::InterpolationWidget(klvk::Application& app, size_t num_colors)
    : app_(&app),
      num_colors_(num_colors),
      fullscreen_shader_(app.GetDeviceContext(), "fractal_example/fullscreen"),
      widget_shader_(app.GetDeviceContext(), "fractal_example/interpolation_widget")
{
    klvk::DeviceContext& context = app.GetDeviceContext();
    vk::Device device = context.GetDevice();

    widget_shader_.SetDefineValue(widget_shader_.GetDefine("COLORS_COUNT"), static_cast<i32>(num_colors));

    const vk::DescriptorSetLayoutBinding binding = vk::DescriptorSetLayoutBinding{}
                                                       .setBinding(0)
                                                       .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                                                       .setDescriptorCount(1)
                                                       .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    set_layout_description_.bindings = {binding};
    const vk::DescriptorSetLayoutCreateInfo layout_info = vk::DescriptorSetLayoutCreateInfo{}.setBindings(binding);
    set_layout_ = klvk::VulkanValue(device.createDescriptorSetLayoutUnique(layout_info), "vkCreateDescriptorSetLayout");

    constexpr auto frames = static_cast<u32>(klvk::Application::kFramesInFlight);
    const vk::DescriptorPoolSize pool_size =
        vk::DescriptorPoolSize{}.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(frames);
    const vk::DescriptorPoolCreateInfo pool_info =
        vk::DescriptorPoolCreateInfo{}.setMaxSets(frames).setPoolSizes(pool_size);
    descriptor_pool_ = klvk::VulkanValue(device.createDescriptorPoolUnique(pool_info), "vkCreateDescriptorPool");

    std::array<vk::DescriptorSetLayout, klvk::Application::kFramesInFlight> layouts{};
    layouts.fill(set_layout_.get());
    const vk::DescriptorSetAllocateInfo allocate_info =
        vk::DescriptorSetAllocateInfo{}.setDescriptorPool(descriptor_pool_.get()).setSetLayouts(layouts);
    const std::vector<vk::DescriptorSet> sets =
        klvk::VulkanValue(device.allocateDescriptorSets(allocate_info), "vkAllocateDescriptorSets");

    for (size_t index = 0; index != klvk::Application::kFramesInFlight; ++index)
    {
        descriptor_sets_[index] = sets[index];
        color_buffers_[index] =
            klvk::GpuBuffer(context, vk::BufferUsageFlagBits::eStorageBuffer, num_colors_ * sizeof(edt::Vec4f), true);

        const vk::DescriptorBufferInfo buffer_info =
            vk::DescriptorBufferInfo{}.setBuffer(color_buffers_[index].GetHandle()).setRange(vk::WholeSize);
        const vk::WriteDescriptorSet write = vk::WriteDescriptorSet{}
                                                 .setDstSet(descriptor_sets_[index])
                                                 .setDstBinding(0)
                                                 .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                                                 .setBufferInfo(buffer_info);
        device.updateDescriptorSets(std::span{&write, 1}, {});
    }

    const klvk::DescriptorSetLayoutView set_layout_view{
        .handle = set_layout_.get(),
        .description = &set_layout_description_,
    };
    pipeline_layout_ = klvk::PipelineLayout{context, std::span{&set_layout_view, 1}};

    auto stages = fullscreen_shader_.MakeStages();
    stages.Append(widget_shader_.MakeStages());
    pipeline_ = CreateFullscreenPipeline(*app_, pipeline_layout_, stages);
}

InterpolationWidget::~InterpolationWidget() noexcept
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
}

void InterpolationWidget::Render(
    vk::CommandBuffer command_buffer,
    const klvk::Viewport& viewport,
    const FractalSettings& settings)
{
    const size_t frame_index = app_->GetFrameInFlightIndex();

    std::vector<edt::Vec4f> colors(num_colors_);
    settings.ComputeColors(
        colors.size(),
        [&](size_t index, const edt::Vec3f& color) { colors[index] = edt::Vec4f{color, 1.f}; });
    color_buffers_[frame_index].Write(std::as_bytes(std::span{colors}));

    CmdSetGlStyleViewport(command_buffer, viewport, app_->GetWindow().GetSize());
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipeline_layout_.GetHandle(),
        0,
        std::span{&descriptor_sets_[frame_index], 1},
        {});
    command_buffer.draw(6, 1, 0, 0);
}
