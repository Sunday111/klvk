#include "klvk/vulkan/graphics_pipeline_builder.hpp"

#include <array>
#include <unordered_set>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/depth_stencil_format.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

namespace
{
constexpr vk::ColorComponentFlags kAllColorComponents = vk::ColorComponentFlagBits::eR |
                                                        vk::ColorComponentFlagBits::eG |
                                                        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

struct VertexFormat
{
    ShaderScalarType scalar = ShaderScalarType::Unknown;
    u32 components = 0;
    u32 bytes = 0;
    bool normalized = false;
};

VertexFormat DescribeVertexFormat(vk::Format format)
{
    switch (format)
    {
    case vk::Format::eR32Sfloat:
        return {.scalar = ShaderScalarType::Float32, .components = 1, .bytes = 4, .normalized = false};
    case vk::Format::eR32G32Sfloat:
        return {.scalar = ShaderScalarType::Float32, .components = 2, .bytes = 8, .normalized = false};
    case vk::Format::eR32G32B32Sfloat:
        return {.scalar = ShaderScalarType::Float32, .components = 3, .bytes = 12, .normalized = false};
    case vk::Format::eR32G32B32A32Sfloat:
        return {.scalar = ShaderScalarType::Float32, .components = 4, .bytes = 16, .normalized = false};
    case vk::Format::eR32Sint:
        return {.scalar = ShaderScalarType::Int32, .components = 1, .bytes = 4, .normalized = false};
    case vk::Format::eR32G32Sint:
        return {.scalar = ShaderScalarType::Int32, .components = 2, .bytes = 8, .normalized = false};
    case vk::Format::eR32G32B32Sint:
        return {.scalar = ShaderScalarType::Int32, .components = 3, .bytes = 12, .normalized = false};
    case vk::Format::eR32G32B32A32Sint:
        return {.scalar = ShaderScalarType::Int32, .components = 4, .bytes = 16, .normalized = false};
    case vk::Format::eR32Uint:
        return {.scalar = ShaderScalarType::UInt32, .components = 1, .bytes = 4, .normalized = false};
    case vk::Format::eR32G32Uint:
        return {.scalar = ShaderScalarType::UInt32, .components = 2, .bytes = 8, .normalized = false};
    case vk::Format::eR32G32B32Uint:
        return {.scalar = ShaderScalarType::UInt32, .components = 3, .bytes = 12, .normalized = false};
    case vk::Format::eR32G32B32A32Uint:
        return {.scalar = ShaderScalarType::UInt32, .components = 4, .bytes = 16, .normalized = false};
    case vk::Format::eR8G8B8A8Unorm:
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
    const std::vector<vk::VertexInputBindingDescription>& bindings,
    const std::vector<vk::VertexInputAttributeDescription>& attributes)
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
        [](const auto& interface) { return interface->stage == vk::ShaderStageFlagBits::eVertex; });
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
                std::ranges::find(attributes, location, &vk::VertexInputAttributeDescription::location);
            ErrorHandling::Ensure(
                attribute != attributes.end(),
                "GraphicsPipelineBuilder: vertex shader input '{}' requires missing location {}",
                input.name,
                location);
            const auto binding =
                std::ranges::find(bindings, attribute->binding, &vk::VertexInputBindingDescription::binding);
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
      blend_attachment_{vk::PipelineColorBlendAttachmentState{}.setColorWriteMask(kAllColorComponents)}
{
}

GraphicsPipelineBuilder::GraphicsPipelineBuilder(DeviceContext& context)
    : context_(&context),
      blend_attachment_{vk::PipelineColorBlendAttachmentState{}.setColorWriteMask(kAllColorComponents)}
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

GraphicsPipelineBuilder& GraphicsPipelineBuilder::UncheckedLayout(vk::PipelineLayout layout)
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
    std::span<const vk::PipelineShaderStageCreateInfo> stages)
{
    ErrorHandling::Ensure(
        reflected_stages_.GetCreateInfos().empty() && owned_modules_.empty(),
        "Cannot mix reflected and unchecked shader stages");
    unchecked_stages_ = stages;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::ShaderFile(
    vk::ShaderStageFlagBits stage,
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
    return ShaderFile(vk::ShaderStageFlagBits::eVertex, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::TessellationControlShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(vk::ShaderStageFlagBits::eTessellationControl, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::TessellationEvaluationShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(vk::ShaderStageFlagBits::eTessellationEvaluation, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::FragmentShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(vk::ShaderStageFlagBits::eFragment, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::GeometryShaderFile(const std::filesystem::path& path)
{
    return ShaderFile(vk::ShaderStageFlagBits::eGeometry, path);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::Topology(vk::PrimitiveTopology topology)
{
    topology_ = topology;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::PatchControlPoints(u32 count)
{
    patch_control_points_ = count;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::PolygonMode(vk::PolygonMode mode)
{
    polygon_mode_ = mode;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::CullMode(vk::CullModeFlags mode, vk::FrontFace front_face)
{
    cull_mode_ = mode;
    front_face_ = front_face;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::VertexBinding(u32 binding, u32 stride, vk::VertexInputRate rate)
{
    vertex_bindings_.push_back(vk::VertexInputBindingDescription{binding, stride, rate});
    return *this;
}

GraphicsPipelineBuilder&
GraphicsPipelineBuilder::VertexAttribute(u32 location, u32 binding, vk::Format format, u32 offset)
{
    vertex_attributes_.push_back(vk::VertexInputAttributeDescription{location, binding, format, offset});
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::AlphaBlend()
{
    blend_attachment_ = vk::PipelineColorBlendAttachmentState{}
                            .setBlendEnable(true)
                            .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
                            .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                            .setColorBlendOp(vk::BlendOp::eAdd)
                            .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                            .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                            .setAlphaBlendOp(vk::BlendOp::eAdd)
                            .setColorWriteMask(kAllColorComponents);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::Blend(const vk::PipelineColorBlendAttachmentState& attachment)
{
    blend_attachment_ = attachment;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::DepthTest(vk::CompareOp compare_op)
{
    depth_test_ = true;
    depth_compare_op_ = compare_op;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::DepthFormat(vk::Format format)
{
    depth_format_ = format;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::StencilTest(
    const vk::StencilOpState& front,
    const vk::StencilOpState& back)
{
    stencil_test_ = true;
    stencil_front_ = front;
    stencil_back_ = back;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::StencilFormat(vk::Format format)
{
    stencil_format_ = format;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::DynamicStencilMasks()
{
    dynamic_stencil_masks_ = true;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::ColorWriteMask(vk::ColorComponentFlags mask)
{
    blend_attachment_.colorWriteMask = mask;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::ColorFormat(vk::Format format)
{
    color_format_ = format;
    return *this;
}

vk::UniquePipeline GraphicsPipelineBuilder::Build()
{
    ErrorHandling::Ensure(layout_ != nullptr, "GraphicsPipelineBuilder: pipeline layout was not set");

    ShaderStages owned_stages;
    if (!owned_modules_.empty())
    {
        ErrorHandling::Ensure(
            reflected_stages_.GetCreateInfos().empty(),
            "GraphicsPipelineBuilder: file stages and external reflected stages cannot be mixed");
        std::vector<vk::PipelineShaderStageCreateInfo> create_infos;
        std::vector<std::shared_ptr<const ShaderInterface>> interfaces;
        for (const auto& [stage, module] : owned_modules_)
        {
            create_infos.push_back(
                vk::PipelineShaderStageCreateInfo{}.setStage(stage).setModule(module.GetHandle()).setPName("main"));
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
    const std::span<const vk::PipelineShaderStageCreateInfo> stages =
        reflected_path ? reflected->GetCreateInfos() : unchecked_stages_;
    ErrorHandling::Ensure(!stages.empty(), "GraphicsPipelineBuilder: no shader stages were set");

    const auto has_stage = [stages](vk::ShaderStageFlagBits stage)
    {
        return std::ranges::any_of(stages, [stage](const auto& info) { return info.stage == stage; });
    };
    const bool has_geometry = has_stage(vk::ShaderStageFlagBits::eGeometry);
    const bool has_tessellation_control = has_stage(vk::ShaderStageFlagBits::eTessellationControl);
    const bool has_tessellation_evaluation = has_stage(vk::ShaderStageFlagBits::eTessellationEvaluation);
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
        has_tessellation == (topology_ == vk::PrimitiveTopology::ePatchList),
        "GraphicsPipelineBuilder: tessellation stages require patch-list topology, and patch-list topology requires "
        "tessellation stages");
    ErrorHandling::Ensure(
        has_tessellation == (patch_control_points_ != 0),
        "GraphicsPipelineBuilder: tessellation pipelines require a non-zero patch control-point count, and other "
        "pipelines must not set one");
    if (has_tessellation)
    {
        const vk::PhysicalDeviceProperties properties = context_->GetPhysicalDevice().getProperties();
        ErrorHandling::Ensure(
            patch_control_points_ <= properties.limits.maxTessellationPatchSize,
            "GraphicsPipelineBuilder: {} patch control points exceed the device limit of {}",
            patch_control_points_,
            properties.limits.maxTessellationPatchSize);
    }

    const auto vertex_input = vk::PipelineVertexInputStateCreateInfo{}
                                  .setVertexBindingDescriptions(vertex_bindings_)
                                  .setVertexAttributeDescriptions(vertex_attributes_);
    const auto input_assembly = vk::PipelineInputAssemblyStateCreateInfo{}.setTopology(topology_);
    const auto tessellation = vk::PipelineTessellationStateCreateInfo{}.setPatchControlPoints(patch_control_points_);
    const auto viewport_state = vk::PipelineViewportStateCreateInfo{}.setViewportCount(1).setScissorCount(1);
    const auto rasterization = vk::PipelineRasterizationStateCreateInfo{}
                                   .setPolygonMode(polygon_mode_)
                                   .setCullMode(cull_mode_)
                                   .setFrontFace(front_face_)
                                   .setLineWidth(1.f);
    const auto multisample =
        vk::PipelineMultisampleStateCreateInfo{}.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    const auto depth_stencil = vk::PipelineDepthStencilStateCreateInfo{}
                                   .setDepthTestEnable(depth_test_)
                                   .setDepthWriteEnable(depth_test_)
                                   .setDepthCompareOp(depth_compare_op_)
                                   .setStencilTestEnable(stencil_test_)
                                   .setFront(stencil_front_)
                                   .setBack(stencil_back_);
    const std::array blend_attachments{blend_attachment_};
    const auto color_blend = vk::PipelineColorBlendStateCreateInfo{}.setAttachments(blend_attachments);
    std::vector dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    if (dynamic_stencil_masks_)
    {
        dynamic_states.push_back(vk::DynamicState::eStencilCompareMask);
        dynamic_states.push_back(vk::DynamicState::eStencilWriteMask);
        dynamic_states.push_back(vk::DynamicState::eStencilReference);
    }
    const auto dynamic_state = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamic_states);

    std::array color_formats{color_format_};
    if (color_formats.front() == vk::Format::eUndefined)
    {
        ErrorHandling::Ensure(
            app_ != nullptr,
            "GraphicsPipelineBuilder: no color format set and no application to default from");
        color_formats.front() = app_->GetSwapchainFormat();
    }
    vk::Format depth_format = vk::Format::eUndefined;
    if (depth_test_)
    {
        depth_format = depth_format_;
        if (depth_format == vk::Format::eUndefined)
        {
            ErrorHandling::Ensure(
                app_ != nullptr,
                "GraphicsPipelineBuilder: depth test enabled without a depth format or application");
            depth_format = app_->GetDepthFormat();
        }
    }
    vk::Format stencil_format = vk::Format::eUndefined;
    if (stencil_test_)
    {
        stencil_format = stencil_format_;
        if (stencil_format == vk::Format::eUndefined)
        {
            ErrorHandling::Ensure(
                app_ != nullptr,
                "GraphicsPipelineBuilder: stencil test enabled without a stencil format or application");
            stencil_format = app_->GetDepthFormat();
        }
        ErrorHandling::Ensure(
            FormatHasStencil(stencil_format),
            "GraphicsPipelineBuilder: stencil test enabled with a format that has no stencil plane");
    }
    const auto rendering_info = vk::PipelineRenderingCreateInfo{}
                                    .setColorAttachmentFormats(color_formats)
                                    .setDepthAttachmentFormat(depth_format)
                                    .setStencilAttachmentFormat(stencil_format);

    const auto pipeline_info = vk::GraphicsPipelineCreateInfo{}
                                   .setPNext(&rendering_info)
                                   .setStages(stages)
                                   .setPVertexInputState(&vertex_input)
                                   .setPInputAssemblyState(&input_assembly)
                                   .setPTessellationState(has_tessellation ? &tessellation : nullptr)
                                   .setPViewportState(&viewport_state)
                                   .setPRasterizationState(&rasterization)
                                   .setPMultisampleState(&multisample)
                                   .setPDepthStencilState(&depth_stencil)
                                   .setPColorBlendState(&color_blend)
                                   .setPDynamicState(&dynamic_state)
                                   .setLayout(layout_);
    const vk::Device device = context_->GetDevice();
    auto outcome = device.createGraphicsPipelineUnique(nullptr, pipeline_info);
    vk::UniquePipeline pipeline = std::move(outcome.value);
    VulkanCheck(outcome.result, "vkCreateGraphicsPipelines");
    return pipeline;
}

}  // namespace klvk
