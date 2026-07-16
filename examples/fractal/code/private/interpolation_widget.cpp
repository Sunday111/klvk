#include "interpolation_widget.hpp"

#include "fractal_settings.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

InterpolationWidget::InterpolationWidget(klvk::Application& app, size_t num_colors)
    : app_(&app),
      num_colors_(num_colors),
      fullscreen_shader_(app.GetDeviceContext(), "fractal_example/fullscreen"),
      widget_shader_(app.GetDeviceContext(), "fractal_example/interpolation_widget")
{
    klvk::DeviceContext& context = app.GetDeviceContext();
    VkDevice device = context.GetDevice();

    widget_shader_.SetDefineValue(widget_shader_.GetDefine("COLORS_COUNT"), static_cast<i32>(num_colors));

    const VkDescriptorSetLayoutBinding binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &binding,
        });

    constexpr auto frames = static_cast<u32>(klvk::Application::kFramesInFlight);
    const VkDescriptorPoolSize pool_size{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = frames};
    descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = frames,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size,
        });

    std::array<VkDescriptorSetLayout, klvk::Application::kFramesInFlight> layouts{};
    layouts.fill(set_layout_);
    const std::vector<VkDescriptorSet> sets = klvk::Vulkan::AllocateDescriptorSets(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptor_pool_,
            .descriptorSetCount = frames,
            .pSetLayouts = layouts.data(),
        });

    for (size_t index = 0; index != klvk::Application::kFramesInFlight; ++index)
    {
        descriptor_sets_[index] = sets[index];
        color_buffers_[index] =
            klvk::GpuBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, num_colors_ * sizeof(edt::Vec4f), true);

        const VkDescriptorBufferInfo buffer_info{.buffer = color_buffers_[index].GetHandle(), .range = VK_WHOLE_SIZE};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets_[index],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffer_info,
        };
        klvk::Vulkan::UpdateDescriptorSets(device, std::span{&write, 1});
    }

    pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &set_layout_,
        });

    auto stages = fullscreen_shader_.MakeShaderStages();
    const auto fragment_stages = widget_shader_.MakeShaderStages();
    stages.insert(stages.end(), fragment_stages.begin(), fragment_stages.end());
    pipeline_ = CreateFullscreenPipeline(*app_, pipeline_layout_, stages);
}

InterpolationWidget::~InterpolationWidget() noexcept
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
    VkDevice device = context.GetDevice();
    klvk::Vulkan::DestroyPipelineNE(device, pipeline_);
    klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
    klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
    klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, set_layout_);
}

void InterpolationWidget::Render(
    VkCommandBuffer command_buffer,
    const klvk::Viewport& viewport,
    const FractalSettings& settings)
{
    const size_t frame_index = app_->GetFrameInFlightIndex();

    std::vector<edt::Vec4f> colors(num_colors_);
    settings.ComputeColors(
        colors.size(),
        [&](size_t index, const edt::Vec3f& color)
        { colors[index] = edt::Vec4f{color.x(), color.y(), color.z(), 1.f}; });
    color_buffers_[frame_index].Write(std::as_bytes(std::span{colors}));

    CmdSetGlStyleViewport(command_buffer, viewport, app_->GetWindow().GetSize());
    klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    klvk::Vulkan::CmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_,
        0,
        std::span{&descriptor_sets_[frame_index], 1});
    klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
}
