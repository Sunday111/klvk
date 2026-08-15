#include <edt/math/math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/texture/procedural_texture_generator.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/vulkan/vulkan_object.hpp"
#include "klvk/window.hpp"

// Matches the push constant block in textured_quad_2d.vert.
struct PushConstants
{
    edt::Vec4f color{1.f, 0.f, 0.f, 1.f};
    edt::Vec2f scale{0.5f, 0.5f};
    edt::Vec2f translation{0.f, 0.f};
};

class TexturedQuadApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();

        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Textured Quad");

        klvk::DeviceContext& context = GetDeviceContext();
        vk::Device device = context.GetDevice();

        // Generate circle mask texture
        {
            constexpr auto size = edt::Vec2<size_t>{} + 128;
            const auto pixels = klvk::ProceduralTextureGenerator::CircleMask(size, 2);
            texture_ = klvk::Texture::CreateR8(context, size.Cast<u32>(), std::span{pixels});
        }

        // Descriptor set that binds the texture to the fragment shader
        descriptor_sets_ =
            klvk::DescriptorSets::Builder(context)
                .Binding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
                .Build();
        descriptor_sets_.WriteImage(0, 0, texture_->GetView(), texture_->GetSampler());

        // Pipeline
        {
            const vk::PushConstantRange push_constant_range =
                vk::PushConstantRange{}.setStageFlags(vk::ShaderStageFlagBits::eVertex).setSize(sizeof(PushConstants));
            const auto set_layout = descriptor_sets_.GetLayoutView();
            pipeline_layout_ =
                klvk::PipelineLayout{context, std::span{&set_layout, 1}, std::span{&push_constant_range, 1}};

            const std::filesystem::path shader_dir = GetShaderDir() / "textured_quad_2d";
            pipeline_ = klvk::VulkanObject<vk::Pipeline>{
                device,
                klvk::GraphicsPipelineBuilder(*this)
                    .Layout(pipeline_layout_)
                    .VertexShaderFile(shader_dir / "textured_quad_2d.vert.slang")
                    .FragmentShaderFile(shader_dir / "textured_quad_2d.frag.slang")
                    .AlphaBlend()
                    .Build()};
        }
    }

    void Tick() override
    {
        klvk::Application::Tick();

        vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        const vk::DescriptorSet descriptor_set = descriptor_sets_.Get(0);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
        command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipeline_layout_.GetHandle(),
            0,
            std::span{&descriptor_set, 1},
            {});

        const PushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };
        command_buffer.pushConstants(
            pipeline_layout_.GetHandle(),
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(push_constants),
            &push_constants);

        command_buffer.draw(6, 1, 0, 0);
    }

private:
    std::unique_ptr<klvk::Texture> texture_;
    klvk::DescriptorSets descriptor_sets_;
    klvk::PipelineLayout pipeline_layout_;
    klvk::VulkanObject<vk::Pipeline> pipeline_;
};

void Main(int argc, char** argv)
{
    TexturedQuadApp app;
    app.RunWithArguments(argc, argv);
}

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
