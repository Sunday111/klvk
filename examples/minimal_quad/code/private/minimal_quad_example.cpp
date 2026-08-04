#include <edt/math/math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vk_object.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
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
        VkDevice device = context.GetDevice();

        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(PushConstants),
        };
        pipeline_layout_ = klvk::PipelineLayout{context, {}, std::span{&push_constant_range, 1}};

        const std::filesystem::path shader_dir = GetShaderDir() / "just_color_2d";
        pipeline_ = klvk::VkObject<VkPipeline>{
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

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdPushConstants(
            command_buffer,
            pipeline_layout_.GetHandle(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            push_constants);
        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
    }

private:
    klvk::PipelineLayout pipeline_layout_;
    klvk::VkObject<VkPipeline> pipeline_;
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
