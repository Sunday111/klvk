#include "graphics_utils.hpp"

#include "fractal_settings.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"

void UpdateFractalRenderTransforms(klvk::RenderTransforms2d& render_transforms, const FractalSettings& settings)
{
    render_transforms.Update(settings.camera, settings.viewport);
    if (settings.view_rotation_radians == 0.f) return;

    const edt::Mat3f view_rotation = edt::Math::MatMul(
        edt::Math::TranslationMatrix(settings.camera.eye),
        edt::Math::RotationMatrix2d(-settings.view_rotation_radians),
        edt::Math::TranslationMatrix(-settings.camera.eye));
    render_transforms.screen_to_world = edt::Math::MatMul(view_rotation, render_transforms.screen_to_world);
}

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
        push_constants.screen_to_world_columns[column] = edt::Vec4f{matrix_column, 0.f};
    }
    return push_constants;
}

vk::UniquePipeline CreateFullscreenPipeline(
    klvk::Application& app,
    const klvk::PipelineLayout& pipeline_layout,
    const klvk::ShaderStages& stages)
{
    return klvk::GraphicsPipelineBuilder(app).Layout(pipeline_layout).Stages(stages).Build();
}

void CmdSetGlStyleViewport(vk::CommandBuffer command_buffer, const klvk::Viewport& viewport, edt::Vec2u32 framebuffer)
{
    const auto position = viewport.position.Cast<float>();
    const auto size = viewport.size.Cast<float>();
    const vk::Viewport vk_viewport = vk::Viewport{}
                                         .setX(position.x())
                                         .setY(static_cast<float>(framebuffer.y()) - position.y())
                                         .setWidth(size.x())
                                         .setHeight(-size.y())
                                         .setMinDepth(0.f)
                                         .setMaxDepth(1.f);
    command_buffer.setViewport(0, std::span{&vk_viewport, 1});

    const vk::Rect2D scissor =
        vk::Rect2D{}
            .setOffset(
                vk::Offset2D{
                    static_cast<i32>(viewport.position.x()),
                    static_cast<i32>(framebuffer.y() - viewport.position.y() - viewport.size.y())})
            .setExtent(vk::Extent2D{viewport.size.x(), viewport.size.y()});
    command_buffer.setScissor(0, std::span{&scissor, 1});
}
