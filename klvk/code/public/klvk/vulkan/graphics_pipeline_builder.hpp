#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class Application;
class DeviceContext;

// Assembles a VkGraphicsPipelineCreateInfo without the ~90 lines of boilerplate
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

    GraphicsPipelineBuilder& Layout(VkPipelineLayout layout);

    // Reference externally owned stages (e.g. klvk::Shader::MakeShaderStages()).
    // The stages - and anything they point at, such as specialization info - must
    // stay alive until Build() is called.
    GraphicsPipelineBuilder& Stages(std::span<const VkPipelineShaderStageCreateInfo> stages);

    // Compile a GLSL source through the device's shader cache and own the module.
    GraphicsPipelineBuilder& VertexShaderFile(const std::filesystem::path& path);
    GraphicsPipelineBuilder& FragmentShaderFile(const std::filesystem::path& path);
    GraphicsPipelineBuilder& GeometryShaderFile(const std::filesystem::path& path);

    GraphicsPipelineBuilder& Topology(VkPrimitiveTopology topology);
    GraphicsPipelineBuilder& PolygonMode(VkPolygonMode mode);
    GraphicsPipelineBuilder& CullMode(VkCullModeFlags mode, VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE);

    GraphicsPipelineBuilder& VertexBinding(uint32_t binding, uint32_t stride, VkVertexInputRate rate);
    GraphicsPipelineBuilder& VertexAttribute(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset);

    // No blending is the default. AlphaBlend() sets the usual straight-alpha
    // over-operator used by the 2d/sprite examples; Blend() takes an explicit
    // attachment state for anything that does not fit the preset.
    GraphicsPipelineBuilder& AlphaBlend();
    GraphicsPipelineBuilder& Blend(const VkPipelineColorBlendAttachmentState& attachment);

    // Enables depth testing and writing and pulls the depth attachment format
    // from the application (or the format given here).
    GraphicsPipelineBuilder& DepthTest(VkCompareOp compare_op = VK_COMPARE_OP_LESS);
    GraphicsPipelineBuilder& DepthFormat(VkFormat format);

    // Defaults to the swapchain format. Set explicitly for offscreen targets.
    GraphicsPipelineBuilder& ColorFormat(VkFormat format);

    [[nodiscard]] VkPipeline Build();

private:
    GraphicsPipelineBuilder& ShaderFile(VkShaderStageFlagBits stage, const std::filesystem::path& path);

    Application* app_ = nullptr;
    DeviceContext* context_ = nullptr;

    std::vector<VkPipelineShaderStageCreateInfo> owned_stages_;
    std::vector<VkShaderModule> owned_modules_;
    std::span<const VkPipelineShaderStageCreateInfo> external_stages_;

    std::vector<VkVertexInputBindingDescription> vertex_bindings_;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes_;

    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPrimitiveTopology topology_ = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode polygon_mode_ = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode_ = VK_CULL_MODE_NONE;
    VkFrontFace front_face_ = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineColorBlendAttachmentState blend_attachment_;
    bool depth_test_ = false;
    VkCompareOp depth_compare_op_ = VK_COMPARE_OP_LESS;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
};

}  // namespace klvk
