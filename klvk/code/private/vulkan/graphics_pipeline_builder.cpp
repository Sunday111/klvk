#include "klvk/vulkan/graphics_pipeline_builder.hpp"

#include <array>
#include <unordered_set>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
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

struct VertexFormat
{
    ShaderScalarType scalar = ShaderScalarType::Unknown;
    u32 components = 0;
    u32 bytes = 0;
    bool normalized = false;
};

VertexFormat DescribeVertexFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R32_SFLOAT:
        return {.scalar = ShaderScalarType::Float32, .components = 1, .bytes = 4, .normalized = false};
    case VK_FORMAT_R32G32_SFLOAT:
        return {.scalar = ShaderScalarType::Float32, .components = 2, .bytes = 8, .normalized = false};
    case VK_FORMAT_R32G32B32_SFLOAT:
        return {.scalar = ShaderScalarType::Float32, .components = 3, .bytes = 12, .normalized = false};
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return {.scalar = ShaderScalarType::Float32, .components = 4, .bytes = 16, .normalized = false};
    case VK_FORMAT_R32_SINT:
        return {.scalar = ShaderScalarType::Int32, .components = 1, .bytes = 4, .normalized = false};
    case VK_FORMAT_R32G32_SINT:
        return {.scalar = ShaderScalarType::Int32, .components = 2, .bytes = 8, .normalized = false};
    case VK_FORMAT_R32G32B32_SINT:
        return {.scalar = ShaderScalarType::Int32, .components = 3, .bytes = 12, .normalized = false};
    case VK_FORMAT_R32G32B32A32_SINT:
        return {.scalar = ShaderScalarType::Int32, .components = 4, .bytes = 16, .normalized = false};
    case VK_FORMAT_R32_UINT:
        return {.scalar = ShaderScalarType::UInt32, .components = 1, .bytes = 4, .normalized = false};
    case VK_FORMAT_R32G32_UINT:
        return {.scalar = ShaderScalarType::UInt32, .components = 2, .bytes = 8, .normalized = false};
    case VK_FORMAT_R32G32B32_UINT:
        return {.scalar = ShaderScalarType::UInt32, .components = 3, .bytes = 12, .normalized = false};
    case VK_FORMAT_R32G32B32A32_UINT:
        return {.scalar = ShaderScalarType::UInt32, .components = 4, .bytes = 16, .normalized = false};
    case VK_FORMAT_R8G8B8A8_UNORM:
        return {.scalar = ShaderScalarType::Float32, .components = 4, .bytes = 4, .normalized = true};
    default:
        ErrorHandling::ThrowWithMessage(
            "GraphicsPipelineBuilder: unsupported reflected vertex format {}",
            static_cast<u32>(format));
    }
    return {};
}

void ValidateVertexInput(
    const ShaderStages& stages,
    const std::vector<VkVertexInputBindingDescription>& bindings,
    const std::vector<VkVertexInputAttributeDescription>& attributes)
{
    std::unordered_set<u32> binding_indices;
    for (const auto& binding : bindings)
    {
        ErrorHandling::Ensure(
            binding_indices.insert(binding.binding).second,
            "GraphicsPipelineBuilder: duplicate vertex binding {}",
            binding.binding);
    }
    std::unordered_set<u32> attribute_locations;
    for (const auto& attribute : attributes)
    {
        ErrorHandling::Ensure(
            attribute_locations.insert(attribute.location).second,
            "GraphicsPipelineBuilder: duplicate vertex attribute location {}",
            attribute.location);
    }

    const auto vertex_interface = std::ranges::find_if(
        stages.GetInterfaces(),
        [](const auto& interface) { return interface->stage == VK_SHADER_STAGE_VERTEX_BIT; });
    if (vertex_interface == stages.GetInterfaces().end()) return;

    size_t required_count = 0;
    for (const ShaderInterfaceVariable& input : (*vertex_interface)->inputs)
    {
        if (input.built_in) continue;
        ErrorHandling::Ensure(
            ShaderScalarByteSize(input.type.scalar) == sizeof(u32),
            "GraphicsPipelineBuilder: vertex shader input '{}' uses a non-32-bit scalar type",
            input.name);
        required_count += input.location_count;
        for (u32 location_offset = 0; location_offset != input.location_count; ++location_offset)
        {
            const u32 location = input.location + location_offset;
            const auto attribute =
                std::ranges::find(attributes, location, &VkVertexInputAttributeDescription::location);
            ErrorHandling::Ensure(
                attribute != attributes.end(),
                "GraphicsPipelineBuilder: vertex shader input '{}' requires missing location {}",
                input.name,
                location);
            const auto binding =
                std::ranges::find(bindings, attribute->binding, &VkVertexInputBindingDescription::binding);
            ErrorHandling::Ensure(
                binding != bindings.end(),
                "GraphicsPipelineBuilder: vertex attribute location {} references missing binding {}",
                location,
                attribute->binding);
            const VertexFormat format = DescribeVertexFormat(attribute->format);
            const u32 expected_components = input.type.rows;
            ErrorHandling::Ensure(
                format.scalar == input.type.scalar && format.components == expected_components,
                "GraphicsPipelineBuilder: vertex attribute location {} has an incompatible format",
                location);
            ErrorHandling::Ensure(
                attribute->offset + format.bytes <= binding->stride,
                "GraphicsPipelineBuilder: vertex attribute location {} exceeds binding {} stride",
                location,
                attribute->binding);
        }
    }
    ErrorHandling::Ensure(
        attributes.size() == required_count,
        "GraphicsPipelineBuilder: {} vertex attributes were supplied but shaders require {}",
        attributes.size(),
        required_count);
}
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

GraphicsPipelineBuilder::~GraphicsPipelineBuilder() = default;

GraphicsPipelineBuilder& GraphicsPipelineBuilder::Layout(const PipelineLayout& layout)
{
    ErrorHandling::Ensure(!unchecked_layout_, "Cannot mix reflected and unchecked pipeline layouts");
    reflected_layout_ = &layout;
    layout_ = layout.GetHandle();
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::UncheckedLayout(VkPipelineLayout layout)
{
    ErrorHandling::Ensure(reflected_layout_ == nullptr, "Cannot mix reflected and unchecked pipeline layouts");
    unchecked_layout_ = true;
    layout_ = layout;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::Stages(const ShaderStages& stages)
{
    ErrorHandling::Ensure(unchecked_stages_.empty(), "Cannot mix reflected and unchecked shader stages");
    reflected_stages_ = stages;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::UncheckedStages(
    std::span<const VkPipelineShaderStageCreateInfo> stages)
{
    ErrorHandling::Ensure(
        reflected_stages_.GetCreateInfos().empty() && owned_modules_.empty(),
        "Cannot mix reflected and unchecked shader stages");
    unchecked_stages_ = stages;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::ShaderFile(
    VkShaderStageFlagBits stage,
    const std::filesystem::path& path)
{
    ErrorHandling::Ensure(unchecked_stages_.empty(), "Cannot mix reflected and unchecked shader stages");
    ShaderModule module = context_->LoadShaderModule(path);
    ErrorHandling::Ensure(
        module.GetInterface()->stage == stage,
        "Shader '{}' reflection stage does not match the pipeline stage",
        path.string());
    owned_modules_.emplace_back(stage, std::move(module));
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::VertexShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(VK_SHADER_STAGE_VERTEX_BIT, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::TessellationControlShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::TessellationEvaluationShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, path);
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

GraphicsPipelineBuilder& GraphicsPipelineBuilder::PatchControlPoints(u32 count)
{
    patch_control_points_ = count;
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

GraphicsPipelineBuilder& GraphicsPipelineBuilder::VertexBinding(u32 binding, u32 stride, VkVertexInputRate rate)
{
    vertex_bindings_.push_back({.binding = binding, .stride = stride, .inputRate = rate});
    return *this;
}

GraphicsPipelineBuilder&
GraphicsPipelineBuilder::VertexAttribute(u32 location, u32 binding, VkFormat format, u32 offset)
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

GraphicsPipelineBuilder& GraphicsPipelineBuilder::Blend(const VkPipelineColorBlendAttachmentState& attachment)
{
    blend_attachment_ = attachment;
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

    ShaderStages owned_stages;
    if (!owned_modules_.empty())
    {
        ErrorHandling::Ensure(
            reflected_stages_.GetCreateInfos().empty(),
            "GraphicsPipelineBuilder: file stages and external reflected stages cannot be mixed");
        std::vector<VkPipelineShaderStageCreateInfo> create_infos;
        std::vector<std::shared_ptr<const ShaderInterface>> interfaces;
        for (const auto& [stage, module] : owned_modules_)
        {
            create_infos.push_back({
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = stage,
                .module = module.GetHandle(),
                .pName = "main",
            });
            interfaces.push_back(module.GetInterface());
        }
        owned_stages = ShaderStages{std::move(create_infos), std::move(interfaces)};
    }
    const ShaderStages* reflected = !owned_stages.GetCreateInfos().empty()
                                        ? &owned_stages
                                        : (!reflected_stages_.GetCreateInfos().empty() ? &reflected_stages_ : nullptr);
    const bool reflected_path = reflected != nullptr;
    ErrorHandling::Ensure(
        reflected_path == (reflected_layout_ != nullptr),
        "GraphicsPipelineBuilder: reflected stages and reflected layout must be used together");
    if (reflected_path)
    {
        (void)reflected_layout_->Validate(*reflected);
        ValidateVertexInput(*reflected, vertex_bindings_, vertex_attributes_);
    }
    else
    {
        ErrorHandling::Ensure(
            unchecked_layout_ && !unchecked_stages_.empty(),
            "GraphicsPipelineBuilder: unchecked construction requires UncheckedLayout and UncheckedStages");
    }
    const std::span<const VkPipelineShaderStageCreateInfo> stages =
        reflected_path ? reflected->GetCreateInfos() : unchecked_stages_;
    ErrorHandling::Ensure(!stages.empty(), "GraphicsPipelineBuilder: no shader stages were set");

    const auto has_stage = [stages](VkShaderStageFlagBits stage)
    {
        return std::ranges::any_of(stages, [stage](const auto& info) { return info.stage == stage; });
    };
    const bool has_geometry = has_stage(VK_SHADER_STAGE_GEOMETRY_BIT);
    const bool has_tessellation_control = has_stage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    const bool has_tessellation_evaluation = has_stage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    const bool has_tessellation = has_tessellation_control || has_tessellation_evaluation;
    ErrorHandling::Ensure(
        !has_geometry || context_->IsGeometryShaderEnabled(),
        "GraphicsPipelineBuilder: geometry shader stage requires the geometryShader device feature");
    ErrorHandling::Ensure(
        has_tessellation_control == has_tessellation_evaluation,
        "GraphicsPipelineBuilder: tessellation control and evaluation stages must be supplied together");
    ErrorHandling::Ensure(
        !has_tessellation || context_->IsTessellationShaderEnabled(),
        "GraphicsPipelineBuilder: tessellation stages require the tessellationShader device feature");
    ErrorHandling::Ensure(
        has_tessellation == (topology_ == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST),
        "GraphicsPipelineBuilder: tessellation stages require patch-list topology, and patch-list topology requires "
        "tessellation stages");
    ErrorHandling::Ensure(
        has_tessellation == (patch_control_points_ != 0),
        "GraphicsPipelineBuilder: tessellation pipelines require a non-zero patch control-point count, and other "
        "pipelines must not set one");
    if (has_tessellation)
    {
        const VkPhysicalDeviceProperties properties =
            Vulkan::GetPhysicalDeviceProperties(context_->GetPhysicalDevice());
        ErrorHandling::Ensure(
            patch_control_points_ <= properties.limits.maxTessellationPatchSize,
            "GraphicsPipelineBuilder: {} patch control points exceed the device limit of {}",
            patch_control_points_,
            properties.limits.maxTessellationPatchSize);
    }

    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<u32>(vertex_bindings_.size()),
        .pVertexBindingDescriptions = vertex_bindings_.data(),
        .vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes_.size()),
        .pVertexAttributeDescriptions = vertex_attributes_.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology_,
    };
    const VkPipelineTessellationStateCreateInfo tessellation{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .patchControlPoints = patch_control_points_,
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
    const std::array blend_attachments{blend_attachment_};
    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = blend_attachments.size(),
        .pAttachments = blend_attachments.data(),
    };
    const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    std::array color_formats{color_format_};
    if (color_formats.front() == VK_FORMAT_UNDEFINED)
    {
        ErrorHandling::Ensure(
            app_ != nullptr,
            "GraphicsPipelineBuilder: no color format set and no application to default from");
        color_formats.front() = app_->GetSwapchainFormat();
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
        .colorAttachmentCount = color_formats.size(),
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = depth_format,
    };

    const std::array pipeline_infos{VkGraphicsPipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = has_tessellation ? &tessellation : nullptr,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = layout_,
    }};
    return Vulkan::CreateGraphicsPipelines(context_->GetDevice(), VK_NULL_HANDLE, pipeline_infos).front();
}

}  // namespace klvk
