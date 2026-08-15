#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_module.hpp"
#include "klvk/shader/shader_stages.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class Application;
class DeviceContext;

// Assembles a vk::GraphicsPipelineCreateInfo without the ~90 lines of boilerplate
// every example repeats. The defaults are the common case: one swapchain-format
// color attachment, triangle list, fill, no culling, no blending, no depth, and
// dynamic viewport/scissor. Only the knobs that differ between pipelines are
// exposed; call the setters that matter and leave the rest.
//
// The builder owns every sub-structure it points at, so a builder must outlive
// the Build() call - which is the normal fluent usage. Shader modules loaded via
// the *ShaderFile helpers are owned too and destroyed once Build() returns.
class GraphicsPipelineBuilder
{
public:
    explicit GraphicsPipelineBuilder(Application& app);
    explicit GraphicsPipelineBuilder(DeviceContext& context);
    GraphicsPipelineBuilder(const GraphicsPipelineBuilder&) = delete;
    GraphicsPipelineBuilder(GraphicsPipelineBuilder&&) = delete;
    ~GraphicsPipelineBuilder();

    GraphicsPipelineBuilder& Layout(const PipelineLayout& layout);
    GraphicsPipelineBuilder& UncheckedLayout(vk::PipelineLayout layout);

    // Reference externally owned stages (e.g. klvk::Shader::MakeStages()).
    // The stages - and anything they point at, such as specialization info - must
    // stay alive until Build() is called.
    GraphicsPipelineBuilder& Stages(const ShaderStages& stages);
    GraphicsPipelineBuilder& UncheckedStages(std::span<const vk::PipelineShaderStageCreateInfo> stages);

    // Compile a GLSL source through the device's shader cache and own the module.
    GraphicsPipelineBuilder& VertexShaderFile(const std::filesystem::path& path);
    GraphicsPipelineBuilder& TessellationControlShaderFile(const std::filesystem::path& path);
    GraphicsPipelineBuilder& TessellationEvaluationShaderFile(const std::filesystem::path& path);
    GraphicsPipelineBuilder& FragmentShaderFile(const std::filesystem::path& path);
    GraphicsPipelineBuilder& GeometryShaderFile(const std::filesystem::path& path);

    GraphicsPipelineBuilder& Topology(vk::PrimitiveTopology topology);
    GraphicsPipelineBuilder& PatchControlPoints(u32 count);
    GraphicsPipelineBuilder& PolygonMode(vk::PolygonMode mode);
    GraphicsPipelineBuilder& CullMode(
        vk::CullModeFlags mode,
        vk::FrontFace front_face = vk::FrontFace::eCounterClockwise);

    GraphicsPipelineBuilder& VertexBinding(u32 binding, u32 stride, vk::VertexInputRate rate);
    GraphicsPipelineBuilder& VertexAttribute(u32 location, u32 binding, vk::Format format, u32 offset);

    // No blending is the default. AlphaBlend() sets the usual straight-alpha
    // over-operator used by the 2d/sprite examples; Blend() takes an explicit
    // attachment state for anything that does not fit the preset.
    GraphicsPipelineBuilder& AlphaBlend();
    GraphicsPipelineBuilder& Blend(const vk::PipelineColorBlendAttachmentState& attachment);

    // Enables depth testing and writing and pulls the depth attachment format
    // from the application (or the format given here).
    GraphicsPipelineBuilder& DepthTest(vk::CompareOp compare_op = vk::CompareOp::eLess);
    GraphicsPipelineBuilder& DepthFormat(vk::Format format);

    // Enables stencil testing independently of depth: a stencil-only pipeline
    // leaves depth testing off and still declares the stencil attachment format,
    // taken from the application unless StencilFormat sets it.
    GraphicsPipelineBuilder& StencilTest(const vk::StencilOpState& front, const vk::StencilOpState& back);
    GraphicsPipelineBuilder& StencilFormat(vk::Format format);

    // Makes compare mask, write mask and reference dynamic, so one pipeline serves
    // every combination the caller sets on the command buffer.
    GraphicsPipelineBuilder& DynamicStencilMasks();

    // Defaults to all channels. A stencil-only pass writes none.
    GraphicsPipelineBuilder& ColorWriteMask(vk::ColorComponentFlags mask);

    // Defaults to the swapchain format. Set explicitly for offscreen targets.
    GraphicsPipelineBuilder& ColorFormat(vk::Format format);

    [[nodiscard]] vk::Pipeline Build();

private:
    GraphicsPipelineBuilder& ShaderFile(vk::ShaderStageFlagBits stage, const std::filesystem::path& path);

    Application* app_ = nullptr;
    DeviceContext* context_ = nullptr;

    std::vector<std::pair<vk::ShaderStageFlagBits, ShaderModule>> owned_modules_;
    ShaderStages reflected_stages_;
    std::span<const vk::PipelineShaderStageCreateInfo> unchecked_stages_;

    std::vector<vk::VertexInputBindingDescription> vertex_bindings_;
    std::vector<vk::VertexInputAttributeDescription> vertex_attributes_;

    vk::PipelineLayout layout_ = nullptr;
    const PipelineLayout* reflected_layout_ = nullptr;
    bool unchecked_layout_ = false;
    vk::PrimitiveTopology topology_ = vk::PrimitiveTopology::eTriangleList;
    u32 patch_control_points_ = 0;
    vk::PolygonMode polygon_mode_ = vk::PolygonMode::eFill;
    vk::CullModeFlags cull_mode_ = vk::CullModeFlagBits::eNone;
    vk::FrontFace front_face_ = vk::FrontFace::eCounterClockwise;
    vk::PipelineColorBlendAttachmentState blend_attachment_;
    bool depth_test_ = false;
    vk::CompareOp depth_compare_op_ = vk::CompareOp::eLess;
    bool stencil_test_ = false;
    bool dynamic_stencil_masks_ = false;
    vk::StencilOpState stencil_front_{};
    vk::StencilOpState stencil_back_{};
    vk::Format color_format_ = vk::Format::eUndefined;
    vk::Format depth_format_ = vk::Format::eUndefined;
    vk::Format stencil_format_ = vk::Format::eUndefined;
};

}  // namespace klvk
