#include <edt/math/matrix.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vk_object.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

namespace
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

// Matches the push constant block in star_winding.vert and cover.vert.
struct PushConstants
{
    Vec2f translation{};
    float scale = 1.f;
    float padding = 0.f;
    Vec4f color{};
};

// Vertices of the fan that sweeps the star outline: three triangles from the
// first of five points.
constexpr u32 kWindingVertexCount = 9;
constexpr u32 kCoverVertexCount = 6;

// Winding numbers never exceed the point count, so a single bit is enough for
// the parity rule and the low nibble is enough for the non-zero rule.
constexpr u32 kNonZeroWriteMask = 0x0F;
constexpr u32 kEvenOddWriteMask = 0x01;

// Draws one self-intersecting star under each fill rule. The core of the star is
// wound twice, so it is inside under the non-zero rule and a hole under even-odd.
class StencilApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();

        SetClearColor({0.05f, 0.05f, 0.08f, 1.f});
        SetStencilBufferEnabled(true);
        GetWindow().SetSize(1000, 500);
        GetWindow().SetTitle("Stencil");

        klvk::DeviceContext& context = GetDeviceContext();
        VkDevice device = context.GetDevice();

        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(PushConstants),
        };
        pipeline_layout_ = klvk::PipelineLayout{context, {}, std::span{&push_constant_range, 1}};

        const std::filesystem::path shader_dir = GetShaderDir() / "stencil";

        auto build_winding_pipeline = [&](VkStencilOp pass_op, VkStencilOp back_pass_op)
        {
            const VkStencilOpState front{
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = pass_op,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
            };
            const VkStencilOpState back{
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = back_pass_op,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
            };
            return klvk::VkObject<VkPipeline>{
                device,
                klvk::GraphicsPipelineBuilder(*this)
                    .Layout(pipeline_layout_)
                    .VertexShaderFile(shader_dir / "star_winding.vert.slang")
                    .FragmentShaderFile(shader_dir / "star_winding.frag.slang")
                    .StencilTest(front, back)
                    .DynamicStencilMasks()
                    .ColorWriteMask(0)
                    .Build()};
        };

        // Front and back faces cancel, so the sum over overlapping loops is the
        // signed winding number.
        non_zero_pipeline_ = build_winding_pipeline(VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP);
        even_odd_pipeline_ = build_winding_pipeline(VK_STENCIL_OP_INVERT, VK_STENCIL_OP_INVERT);

        // Anything the winding pass marked is painted and reset in one operation,
        // so the next shape starts from a clean stencil without another clear.
        const VkStencilOpState cover{
            .failOp = VK_STENCIL_OP_ZERO,
            .passOp = VK_STENCIL_OP_ZERO,
            .depthFailOp = VK_STENCIL_OP_ZERO,
            .compareOp = VK_COMPARE_OP_NOT_EQUAL,
        };
        cover_pipeline_ = klvk::VkObject<VkPipeline>{
            device,
            klvk::GraphicsPipelineBuilder(*this)
                .Layout(pipeline_layout_)
                .VertexShaderFile(shader_dir / "cover.vert.slang")
                .FragmentShaderFile(shader_dir / "cover.frag.slang")
                .StencilTest(cover, cover)
                .DynamicStencilMasks()
                .Build()};
    }

    void DrawStar(const PushConstants& push_constants, VkPipeline winding_pipeline, u32 winding_write_mask)
    {
        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        klvk::Vulkan::CmdPushConstants(
            command_buffer,
            pipeline_layout_.GetHandle(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            push_constants);

        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, winding_pipeline);
        klvk::Vulkan::CmdSetStencilCompareMask(command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
        klvk::Vulkan::CmdSetStencilWriteMask(command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, winding_write_mask);
        klvk::Vulkan::CmdSetStencilReference(command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
        klvk::Vulkan::CmdDraw(command_buffer, kWindingVertexCount, 1, 0, 0);

        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cover_pipeline_);
        klvk::Vulkan::CmdSetStencilCompareMask(command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, winding_write_mask);
        klvk::Vulkan::CmdSetStencilWriteMask(command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
        klvk::Vulkan::CmdSetStencilReference(command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
        klvk::Vulkan::CmdDraw(command_buffer, kCoverVertexCount, 1, 0, 0);
    }

    void Tick() override
    {
        klvk::Application::Tick();

        DrawStar(
            {.translation = {-0.5f, 0.f}, .scale = 0.4f, .color = {0.35f, 0.75f, 1.f, 1.f}},
            non_zero_pipeline_,
            kNonZeroWriteMask);
        DrawStar(
            {.translation = {0.5f, 0.f}, .scale = 0.4f, .color = {1.f, 0.6f, 0.25f, 1.f}},
            even_odd_pipeline_,
            kEvenOddWriteMask);
    }

private:
    klvk::PipelineLayout pipeline_layout_;
    klvk::VkObject<VkPipeline> non_zero_pipeline_;
    klvk::VkObject<VkPipeline> even_odd_pipeline_;
    klvk::VkObject<VkPipeline> cover_pipeline_;
};

void Main(int argc, char** argv)
{
    StencilApp app;
    app.RunWithArguments(argc, argv);
}

}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
