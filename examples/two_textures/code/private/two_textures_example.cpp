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
        descriptor_sets_ =
            klvk::DescriptorSets::Builder(GetDeviceContext())
                .Binding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
                .Binding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
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
        vk::Device device = context.GetDevice();

        const vk::PushConstantRange push_constant_range =
            vk::PushConstantRange{}.setStageFlags(vk::ShaderStageFlagBits::eVertex).setSize(sizeof(PushConstants));
        const auto set_layout = descriptor_sets_.GetLayoutView();
        pipeline_layout_ = klvk::PipelineLayout{context, std::span{&set_layout, 1}, std::span{&push_constant_range, 1}};

        const std::filesystem::path shader_dir = GetShaderDir() / "two_textures_2d";
        pipeline_ = klvk::VulkanObject<vk::Pipeline>{
            device,
            klvk::GraphicsPipelineBuilder(*this)
                .Layout(pipeline_layout_)
                .VertexShaderFile(shader_dir / "two_textures_2d.vert.slang")
                .FragmentShaderFile(shader_dir / "two_textures_2d.frag.slang")
                .AlphaBlend()
                .Build()};
    }

    void Tick() override
    {
        klvk::Application::Tick();

        vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);

        PushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };

        auto draw = [&](vk::DescriptorSet set, const edt::Vec2f& translation)
        {
            push_constants.translation = translation;
            command_buffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                pipeline_layout_.GetHandle(),
                0,
                std::span{&set, 1},
                {});
            command_buffer.pushConstants(
                pipeline_layout_.GetHandle(),
                vk::ShaderStageFlagBits::eVertex,
                0,
                sizeof(push_constants),
                &push_constants);
            command_buffer.draw(6, 1, 0, 0);
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
    klvk::PipelineLayout pipeline_layout_;
    klvk::VulkanObject<vk::Pipeline> pipeline_;
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
