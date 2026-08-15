#include <edt/math/math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/vulkan/vulkan_object.hpp"
#include "klvk/window.hpp"

// Matches the push constant block in just_color_2d.vert.
struct PushConstants
{
    std::array<edt::Vec4f, 3> transform_columns{};
    edt::Vec4f color{};
};

class QuadApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();

        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Quad");

        klvk::DeviceContext& context = GetDeviceContext();
        vk::Device device = context.GetDevice();

        const vk::PushConstantRange push_constant_range =
            vk::PushConstantRange{}.setStageFlags(vk::ShaderStageFlagBits::eVertex).setSize(sizeof(PushConstants));
        pipeline_layout_ = klvk::PipelineLayout{context, {}, std::span{&push_constant_range, 1}};

        const std::filesystem::path shader_dir = GetShaderDir() / "just_color_2d";
        pipeline_ = klvk::VulkanObject<vk::Pipeline>{
            device,
            klvk::GraphicsPipelineBuilder(*this)
                .Layout(pipeline_layout_)
                .VertexShaderFile(shader_dir / "just_color_2d.vert.slang")
                .FragmentShaderFile(shader_dir / "just_color_2d.frag.slang")
                .Build()};
    }

    void Tick() override
    {
        klvk::Application::Tick();

        auto m = edt::Math::ScaleMatrix(edt::Vec2f{} + 0.5f);
        m = edt::Math::RotationMatrix2d(GetTimeSeconds()).MatMul(m);
        m = edt::Math::TranslationMatrix(edt::Vec2f{0.5, 0}).MatMul(m);

        PushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };
        for (size_t column = 0; column != 3; ++column)
        {
            const edt::Vec3f matrix_column = m.GetColumn(column);
            push_constants.transform_columns[column] = edt::Vec4f{matrix_column, 0.f};
        }

        vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
        command_buffer.pushConstants(
            pipeline_layout_.GetHandle(),
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(push_constants),
            &push_constants);
        command_buffer.draw(6, 1, 0, 0);
    }

private:
    klvk::PipelineLayout pipeline_layout_;
    klvk::VulkanObject<vk::Pipeline> pipeline_;
};

void Main(int argc, char** argv)
{
    QuadApp app;
    app.RunWithArguments(argc, argv);
}

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
