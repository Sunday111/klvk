#include "graphics_utils.hpp"

#include "fractal_settings.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

FractalPushConstants MakeFractalPushConstants(const FractalSettings& settings, const edt::Mat3f& screen_to_world)
{
    FractalPushConstants push_constants{
        .resolution = settings.viewport.size.Cast<float>(),
        .julia_constant = settings.fractal_constant,
        .fractal_power = settings.fractal_power,
    };
    for (size_t column = 0; column != 3; ++column)
    {
        const edt::Vec3f matrix_column = screen_to_world.GetColumn(column);
        push_constants.screen_to_world_columns[column] =
            edt::Vec4f{matrix_column.x(), matrix_column.y(), matrix_column.z(), 0.f};
    }
    return push_constants;
}

VkPipeline CreateFullscreenPipeline(
    klvk::Application& app,
    VkPipelineLayout pipeline_layout,
    std::span<const VkPipelineShaderStageCreateInfo> stages)
{
    return klvk::GraphicsPipelineBuilder(app).Layout(pipeline_layout).Stages(stages).Build();
}

void CmdSetGlStyleViewport(VkCommandBuffer command_buffer, const klvk::Viewport& viewport, edt::Vec2u32 framebuffer)
{
    const auto position = viewport.position.Cast<float>();
    const auto size = viewport.size.Cast<float>();
    const VkViewport vk_viewport{
        .x = position.x(),
        .y = static_cast<float>(framebuffer.y()) - position.y(),
        .width = size.x(),
        .height = -size.y(),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    klvk::Vulkan::CmdSetViewport(command_buffer, 0, std::span{&vk_viewport, 1});

    const VkRect2D scissor{
        .offset =
            {static_cast<i32>(viewport.position.x()),
             static_cast<i32>(framebuffer.y() - viewport.position.y() - viewport.size.y())},
        .extent = {viewport.size.x(), viewport.size.y()},
    };
    klvk::Vulkan::CmdSetScissor(command_buffer, 0, std::span{&scissor, 1});
}
