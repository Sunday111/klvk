#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

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
        pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &push_constant_range,
            });

        auto load_shader = [&](const char* name)
        {
            std::string spirv;
            klvk::Filesystem::ReadFile(GetShaderDir() / "just_color_2d" / name, spirv);
            return context.CreateShaderModule(spirv, name);
        };

        VkShaderModule vertex_shader = load_shader("just_color_2d.vert.spv");
        VkShaderModule fragment_shader = load_shader("just_color_2d.frag.spv");
        auto destroy_modules = klvk::OnScopeLeave(
            [&]
            {
                klvk::Vulkan::DestroyShaderModuleNE(device, vertex_shader);
                klvk::Vulkan::DestroyShaderModuleNE(device, fragment_shader);
            });

        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertex_shader,
                .pName = "main",
            },
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragment_shader,
                .pName = "main",
            },
        };

        const VkPipelineVertexInputStateCreateInfo vertex_input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        };
        const VkPipelineInputAssemblyStateCreateInfo input_assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };
        const VkPipelineViewportStateCreateInfo viewport_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };
        const VkPipelineRasterizationStateCreateInfo rasterization{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.f,
        };
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        const VkPipelineColorBlendAttachmentState blend_attachment{
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };
        const VkPipelineColorBlendStateCreateInfo color_blend{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &blend_attachment,
        };
        const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
            .pDynamicStates = dynamic_states.data(),
        };

        const VkFormat color_format = GetSwapchainFormat();
        const VkPipelineRenderingCreateInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_format,
        };

        const VkGraphicsPipelineCreateInfo pipeline_info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering_info,
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisample,
            .pColorBlendState = &color_blend,
            .pDynamicState = &dynamic_state,
            .layout = pipeline_layout_,
        };
        pipeline_ = klvk::Vulkan::CreateGraphicsPipelines(device, VK_NULL_HANDLE, std::span{&pipeline_info, 1}).front();
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
            push_constants.transform_columns[column] =
                edt::Vec4f{matrix_column.x(), matrix_column.y(), matrix_column.z(), 0.f};
        }

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdPushConstants(
            command_buffer,
            pipeline_layout_,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            push_constants);
        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
    }

public:
    ~QuadApp() override
    {
        // Initialize either succeeded entirely or threw before creating the pipeline.
        if (pipeline_ == VK_NULL_HANDLE) return;

        klvk::DeviceContext& context = GetDeviceContext();
        context.WaitIdle();
        VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroyPipelineNE(device, pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
    }

private:
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

void Main()
{
    QuadApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
