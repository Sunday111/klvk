#include "simple_gpu_renderer.hpp"

#include "../fractal_settings.hpp"
#include "klvk/vulkan/device_context.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

SimpleGpuRenderer::SimpleGpuRenderer(klvk::Application& app, size_t max_iterations_)
    : app_(&app),
      max_iterations(max_iterations_)
{
    klvk::DeviceContext& context = app.GetDeviceContext();
    VkDevice device = context.GetDevice();

    vertex_shader_ = LoadShaderModule(app, "fullscreen.vert.spv");
    fragment_shader_ = LoadShaderModule(app, "fractal.frag.spv");

    color_table_ = klvk::GpuBuffer(
        context,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        (max_iterations + 1) * sizeof(edt::Vec4f),
        true);

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

    const VkDescriptorPoolSize pool_size{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
    descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size,
        });
    descriptor_set_ = klvk::Vulkan::AllocateDescriptorSets(
                          device,
                          {
                              .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                              .descriptorPool = descriptor_pool_,
                              .descriptorSetCount = 1,
                              .pSetLayouts = &set_layout_,
                          })
                          .front();

    const VkDescriptorBufferInfo buffer_info{.buffer = color_table_.GetHandle(), .range = VK_WHOLE_SIZE};
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set_,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &buffer_info,
    };
    klvk::Vulkan::UpdateDescriptorSets(device, std::span{&write, 1});

    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(FractalPushConstants),
    };
    pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &set_layout_,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range,
        });
}

SimpleGpuRenderer::~SimpleGpuRenderer() noexcept
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
    VkDevice device = context.GetDevice();
    klvk::Vulkan::DestroyPipelineNE(device, pipeline_);
    klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
    klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
    klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, set_layout_);
    klvk::Vulkan::DestroyShaderModuleNE(device, fragment_shader_);
    klvk::Vulkan::DestroyShaderModuleNE(device, vertex_shader_);
}

void SimpleGpuRenderer::ApplySettings(const FractalSettings& settings)
{
    klvk::DeviceContext& context = app_->GetDeviceContext();

    // The old pipeline may still be referenced by the frame in flight.
    context.WaitIdle();
    klvk::Vulkan::DestroyPipelineNE(context.GetDevice(), pipeline_);

    // klgl recompiles the shader with new defines here; specialization
    // constants with a pipeline rebuild are the Vulkan equivalent.
    const struct
    {
        int32_t max_iterations;
        int32_t inside_out_space;
        int32_t color_mode;
    } spec_data{
        static_cast<int32_t>(max_iterations),
        settings.inside_out_space ? 1 : 0,
        settings.color_mode,
    };
    const std::array<VkSpecializationMapEntry, 3> spec_entries{
        VkSpecializationMapEntry{.constantID = 0, .offset = 0, .size = sizeof(int32_t)},
        VkSpecializationMapEntry{.constantID = 1, .offset = 4, .size = sizeof(int32_t)},
        VkSpecializationMapEntry{.constantID = 2, .offset = 8, .size = sizeof(int32_t)},
    };
    const VkSpecializationInfo spec_info{
        .mapEntryCount = static_cast<uint32_t>(spec_entries.size()),
        .pMapEntries = spec_entries.data(),
        .dataSize = sizeof(spec_data),
        .pData = &spec_data,
    };
    pipeline_ = CreateFullscreenPipeline(*app_, pipeline_layout_, vertex_shader_, fragment_shader_, &spec_info);

    std::vector<edt::Vec4f> colors(max_iterations + 1);
    settings.ComputeColors(
        colors.size(),
        [&](size_t index, const edt::Vec3f& color) { colors[index] = edt::Vec4f{color.x(), color.y(), color.z(), 1.f}; });
    color_table_.Write(std::as_bytes(std::span{colors}));
}

void SimpleGpuRenderer::Render(VkCommandBuffer command_buffer, const FractalSettings& settings)
{
    render_transforms_.Update(settings.camera, settings.viewport);

    CmdSetGlStyleViewport(command_buffer, settings.viewport, app_->GetWindow().GetSize());
    klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    klvk::Vulkan::CmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_,
        0,
        std::span{&descriptor_set_, 1});

    const FractalPushConstants push_constants =
        MakeFractalPushConstants(settings, render_transforms_.screen_to_world);
    klvk::Vulkan::CmdPushConstants(
        command_buffer,
        pipeline_layout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        push_constants);

    klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
}
