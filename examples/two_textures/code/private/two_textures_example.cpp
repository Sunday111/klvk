#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
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

// Matches the push constant block in two_textures_2d.vert.
struct PushConstants
{
    edt::Vec4f color{};
    edt::Vec2f scale{0.5f, 0.5f};
    edt::Vec2f translation{};
};

class TwoTexturesApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();

        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Two textures");

        GenerateTextures();
        PrepareDescriptors();
        PreparePipeline();
    }

    void GenerateTextures()
    {
        klvk::DeviceContext& context = GetDeviceContext();

        // Generate triangle mask texture and mirror it
        constexpr auto texture_size = edt::Vec2<size_t>{} + 128;
        auto pixels = klvk::ProceduralTextureGenerator::TriangleMask(texture_size, 2);
        right_triangle_texture_ = klvk::Texture::CreateR8(context, texture_size.Cast<u32>(), std::span{pixels});

        klvk::ProceduralTextureGenerator::MirrorX(texture_size, pixels);
        left_triangle_texture_ = klvk::Texture::CreateR8(context, texture_size.Cast<u32>(), std::span{pixels});

        // Generate circle mask texture
        pixels = klvk::ProceduralTextureGenerator::CircleMask(texture_size, 2);
        circle_texture_ = klvk::Texture::CreateR8(context, texture_size.Cast<u32>(), std::span{pixels});
    }

    // One descriptor set per texture pair, both written once here:
    // the circle with the right triangle and the circle with the left one.
    void PrepareDescriptors()
    {
        descriptor_sets_ = klvk::DescriptorSets::Builder(GetDeviceContext())
                               .Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .Binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .Build(2);
        auto write_pair = [&](size_t set, const klvk::Texture& triangle)
        {
            descriptor_sets_.WriteImage(set, 0, circle_texture_->GetView(), circle_texture_->GetSampler());
            descriptor_sets_.WriteImage(set, 1, triangle.GetView(), triangle.GetSampler());
        };
        write_pair(kRightSet, *right_triangle_texture_);
        write_pair(kLeftSet, *left_triangle_texture_);
    }

    void PreparePipeline()
    {
        klvk::DeviceContext& context = GetDeviceContext();
        VkDevice device = context.GetDevice();

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

        const std::filesystem::path shader_dir = GetShaderDir() / "two_textures_2d";
        pipeline_ = klvk::VkObject<VkPipeline>{
            device,
            klvk::GraphicsPipelineBuilder(*this)
                .Layout(pipeline_layout_)
                .VertexShaderFile(shader_dir / "two_textures_2d.vert")
                .FragmentShaderFile(shader_dir / "two_textures_2d.frag")
                .AlphaBlend()
                .Build()};
    }

    void Tick() override
    {
        klvk::Application::Tick();

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        PushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };

        auto draw = [&](VkDescriptorSet set, const edt::Vec2f& translation)
        {
            push_constants.translation = translation;
            klvk::Vulkan::CmdBindDescriptorSets(
                command_buffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_layout_,
                0,
                std::span{&set, 1});
            klvk::Vulkan::CmdPushConstants(
                command_buffer,
                pipeline_layout_,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                push_constants);
            klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
        };

        draw(descriptor_sets_.Get(kRightSet), {0.5f, 0.f});
        draw(descriptor_sets_.Get(kLeftSet), {-0.5f, 0.f});
    }

private:
    static constexpr size_t kRightSet = 0;
    static constexpr size_t kLeftSet = 1;

    std::unique_ptr<klvk::Texture> circle_texture_;
    std::unique_ptr<klvk::Texture> right_triangle_texture_;
    std::unique_ptr<klvk::Texture> left_triangle_texture_;
    klvk::DescriptorSets descriptor_sets_;
    klvk::VkObject<VkPipelineLayout> pipeline_layout_;
    klvk::VkObject<VkPipeline> pipeline_;
};

void Main(int argc, char** argv)
{
    TwoTexturesApp app;
    app.RunWithArguments(argc, argv);
}

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
