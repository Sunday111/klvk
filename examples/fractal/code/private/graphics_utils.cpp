#include "graphics_utils.hpp"

#include "fractal_settings.hpp"
#include "klvk/vulkan/device_context.hpp"

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
    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState blend_attachment{
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    const VkFormat color_format = app.GetSwapchainFormat();
    const VkPipelineRenderingCreateInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };

    const VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout,
    };
    return klvk::Vulkan::CreateGraphicsPipelines(
               app.GetDeviceContext().GetDevice(),
               VK_NULL_HANDLE,
               std::span{&pipeline_info, 1})
        .front();
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
            {static_cast<int32_t>(viewport.position.x()),
             static_cast<int32_t>(framebuffer.y() - viewport.position.y() - viewport.size.y())},
        .extent = {viewport.size.x(), viewport.size.y()},
    };
    klvk::Vulkan::CmdSetScissor(command_buffer, 0, std::span{&scissor, 1});
}
