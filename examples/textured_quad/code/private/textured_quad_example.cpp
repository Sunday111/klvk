#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/texture/procedural_texture_generator.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vk_object.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

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
        VkDevice device = context.GetDevice();

        // Generate circle mask texture
        {
            constexpr auto size = edt::Vec2<size_t>{} + 128;
            const auto pixels = klvk::ProceduralTextureGenerator::CircleMask(size, 2);
            texture_ = klvk::Texture::CreateR8(context, size.Cast<uint32_t>(), std::span{pixels});
        }

        // Descriptor set that binds the texture to the fragment shader
        descriptor_sets_ = klvk::DescriptorSets::Builder(context)
                               .Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .Build();
        descriptor_sets_.WriteImage(0, 0, texture_->GetView(), texture_->GetSampler());

        // Pipeline
        {
            const VkPushConstantRange push_constant_range{
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(PushConstants),
            };
            const VkDescriptorSetLayout set_layout = descriptor_sets_.GetLayout();
            pipeline_layout_ = klvk::VkObject<VkPipelineLayout>{
                device,
                klvk::Vulkan::CreatePipelineLayout(
                    device,
                    {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                        .setLayoutCount = 1,
                        .pSetLayouts = &set_layout,
                        .pushConstantRangeCount = 1,
                        .pPushConstantRanges = &push_constant_range,
                    })};

            const std::filesystem::path shader_dir = GetShaderDir() / "textured_quad_2d";
            pipeline_ = klvk::VkObject<VkPipeline>{
                device,
                klvk::GraphicsPipelineBuilder(*this)
                    .Layout(pipeline_layout_)
                    .VertexShaderFile(shader_dir / "textured_quad_2d.vert")
                    .FragmentShaderFile(shader_dir / "textured_quad_2d.frag")
                    .AlphaBlend()
                    .Build()};
        }
    }

    void Tick() override
    {
        klvk::Application::Tick();

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        const VkDescriptorSet descriptor_set = descriptor_sets_.Get(0);
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0,
            std::span{&descriptor_set, 1});

        const PushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };
        klvk::Vulkan::CmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, push_constants);

        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
    }

private:
    std::unique_ptr<klvk::Texture> texture_;
    klvk::DescriptorSets descriptor_sets_;
    klvk::VkObject<VkPipelineLayout> pipeline_layout_;
    klvk::VkObject<VkPipeline> pipeline_;
};

void Main()
{
    TexturedQuadApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
