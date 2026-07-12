#include "klvk/vulkan/graphics_pipeline_builder.hpp"

#include <array>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"
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
constexpr VkColorComponentFlags kAllColorComponents =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
}  // namespace

GraphicsPipelineBuilder::GraphicsPipelineBuilder(Application& app)
    : app_(&app),
      context_(&app.GetDeviceContext()),
      blend_attachment_{.colorWriteMask = kAllColorComponents}
{
}

GraphicsPipelineBuilder::GraphicsPipelineBuilder(DeviceContext& context)
    : context_(&context),
      blend_attachment_{.colorWriteMask = kAllColorComponents}
{
}

GraphicsPipelineBuilder::~GraphicsPipelineBuilder()
{
    const VkDevice device = context_->GetDevice();
    for (VkShaderModule module : owned_modules_) Vulkan::DestroyShaderModuleNE(device, module);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::Layout(VkPipelineLayout layout)
{
    layout_ = layout;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::Stages(std::span<const VkPipelineShaderStageCreateInfo> stages)
{
    external_stages_ = stages;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::ShaderFile(
    VkShaderStageFlagBits stage,
    const std::filesystem::path& path)
{
    const VkShaderModule module = context_->CreateShaderModuleFromSource(path);
    owned_modules_.push_back(module);
    owned_stages_.push_back(
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = stage,
            .module = module,
            .pName = "main",
        });
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::VertexShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(VK_SHADER_STAGE_VERTEX_BIT, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::FragmentShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(VK_SHADER_STAGE_FRAGMENT_BIT, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::GeometryShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(VK_SHADER_STAGE_GEOMETRY_BIT, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::Topology(VkPrimitiveTopology topology)
{
    topology_ = topology;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::PolygonMode(VkPolygonMode mode)
{
    polygon_mode_ = mode;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::CullMode(VkCullModeFlags mode, VkFrontFace front_face)
{
    cull_mode_ = mode;
    front_face_ = front_face;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::VertexBinding(uint32_t binding, uint32_t stride, VkVertexInputRate rate)
{
    vertex_bindings_.push_back({.binding = binding, .stride = stride, .inputRate = rate});
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::VertexAttribute(
    uint32_t location,
    uint32_t binding,
    VkFormat format,
    uint32_t offset)
{
    vertex_attributes_.push_back({.location = location, .binding = binding, .format = format, .offset = offset});
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::AlphaBlend()
{
    blend_attachment_ = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = kAllColorComponents,
    };
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::DepthTest(VkCompareOp compare_op)
{
    depth_test_ = true;
    depth_compare_op_ = compare_op;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::DepthFormat(VkFormat format)
{
    depth_format_ = format;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::ColorFormat(VkFormat format)
{
    color_format_ = format;
    return *this;
}

VkPipeline GraphicsPipelineBuilder::Build()
{
    ErrorHandling::Ensure(layout_ != VK_NULL_HANDLE, "GraphicsPipelineBuilder: pipeline layout was not set");

    const std::span<const VkPipelineShaderStageCreateInfo> stages =
        external_stages_.empty() ? std::span<const VkPipelineShaderStageCreateInfo>{owned_stages_} : external_stages_;
    ErrorHandling::Ensure(!stages.empty(), "GraphicsPipelineBuilder: no shader stages were set");

    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_bindings_.size()),
        .pVertexBindingDescriptions = vertex_bindings_.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attributes_.size()),
        .pVertexAttributeDescriptions = vertex_attributes_.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology_,
    };
    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = polygon_mode_,
        .cullMode = cull_mode_,
        .frontFace = front_face_,
        .lineWidth = 1.f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depth_stencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test_ ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = depth_test_ ? VK_TRUE : VK_FALSE,
        .depthCompareOp = depth_compare_op_,
    };
    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment_,
    };
    const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    VkFormat color_format = color_format_;
    if (color_format == VK_FORMAT_UNDEFINED)
    {
        ErrorHandling::Ensure(
            app_ != nullptr,
            "GraphicsPipelineBuilder: no color format set and no application to default from");
        color_format = app_->GetSwapchainFormat();
    }
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    if (depth_test_)
    {
        depth_format = depth_format_;
        if (depth_format == VK_FORMAT_UNDEFINED)
        {
            ErrorHandling::Ensure(
                app_ != nullptr,
                "GraphicsPipelineBuilder: depth test enabled without a depth format or application");
            depth_format = app_->GetDepthFormat();
        }
    }
    const VkPipelineRenderingCreateInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
        .depthAttachmentFormat = depth_format,
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
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = layout_,
    };
    return Vulkan::CreateGraphicsPipelines(context_->GetDevice(), VK_NULL_HANDLE, std::span{&pipeline_info, 1}).front();
}

}  // namespace klvk
