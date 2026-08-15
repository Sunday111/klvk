#include <edt/math/matrix.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/vulkan/vulkan_object.hpp"
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
        vk::Device device = context.GetDevice();

        const vk::PushConstantRange push_constant_range =
            vk::PushConstantRange{}.setStageFlags(vk::ShaderStageFlagBits::eVertex).setSize(sizeof(PushConstants));
        pipeline_layout_ = klvk::PipelineLayout{context, {}, std::span{&push_constant_range, 1}};

        const std::filesystem::path shader_dir = GetShaderDir() / "stencil";

        auto build_winding_pipeline = [&](vk::StencilOp pass_op, vk::StencilOp back_pass_op)
        {
            const vk::StencilOpState front = vk::StencilOpState{}
                                                 .setFailOp(vk::StencilOp::eKeep)
                                                 .setPassOp(pass_op)
                                                 .setDepthFailOp(vk::StencilOp::eKeep)
                                                 .setCompareOp(vk::CompareOp::eAlways);
            const vk::StencilOpState back = vk::StencilOpState{}
                                                .setFailOp(vk::StencilOp::eKeep)
                                                .setPassOp(back_pass_op)
                                                .setDepthFailOp(vk::StencilOp::eKeep)
                                                .setCompareOp(vk::CompareOp::eAlways);
            return klvk::VulkanObject<vk::Pipeline>{
                device,
                klvk::GraphicsPipelineBuilder(*this)
                    .Layout(pipeline_layout_)
                    .VertexShaderFile(shader_dir / "star_winding.vert.slang")
                    .FragmentShaderFile(shader_dir / "star_winding.frag.slang")
                    .StencilTest(front, back)
                    .DynamicStencilMasks()
                    .ColorWriteMask({})
                    .Build()};
        };

        // Front and back faces cancel, so the sum over overlapping loops is the
        // signed winding number.
        non_zero_pipeline_ = build_winding_pipeline(vk::StencilOp::eIncrementAndWrap, vk::StencilOp::eDecrementAndWrap);
        even_odd_pipeline_ = build_winding_pipeline(vk::StencilOp::eInvert, vk::StencilOp::eInvert);

        // Anything the winding pass marked is painted and reset in one operation,
        // so the next shape starts from a clean stencil without another clear.
        const vk::StencilOpState cover = vk::StencilOpState{}
                                             .setFailOp(vk::StencilOp::eZero)
                                             .setPassOp(vk::StencilOp::eZero)
                                             .setDepthFailOp(vk::StencilOp::eZero)
                                             .setCompareOp(vk::CompareOp::eNotEqual);
        cover_pipeline_ = klvk::VulkanObject<vk::Pipeline>{
            device,
            klvk::GraphicsPipelineBuilder(*this)
                .Layout(pipeline_layout_)
                .VertexShaderFile(shader_dir / "cover.vert.slang")
                .FragmentShaderFile(shader_dir / "cover.frag.slang")
                .StencilTest(cover, cover)
                .DynamicStencilMasks()
                .Build()};
    }

    void DrawStar(const PushConstants& push_constants, vk::Pipeline winding_pipeline, u32 winding_write_mask)
    {
        vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        command_buffer.pushConstants(
            pipeline_layout_.GetHandle(),
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(push_constants),
            &push_constants);

        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, winding_pipeline);
        command_buffer.setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack, 0xFF);
        command_buffer.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack, winding_write_mask);
        command_buffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, 0);
        command_buffer.draw(kWindingVertexCount, 1, 0, 0);

        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, cover_pipeline_);
        command_buffer.setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack, winding_write_mask);
        command_buffer.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack, 0xFF);
        command_buffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, 0);
        command_buffer.draw(kCoverVertexCount, 1, 0, 0);
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
    klvk::VulkanObject<vk::Pipeline> non_zero_pipeline_;
    klvk::VulkanObject<vk::Pipeline> even_odd_pipeline_;
    klvk::VulkanObject<vk::Pipeline> cover_pipeline_;
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
