#include <imgui.h>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace
{

struct FractalPushConstants
{
    std::array<float, 4> view{};
    std::array<float, 4> julia{};
};

class FractalApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1200, 900);
        GetWindow().SetTitle("Fractal");

        klvk::DeviceContext& context = GetDeviceContext();
        const VkDevice device = context.GetDevice();
        const VkPushConstantRange range{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .size = sizeof(FractalPushConstants),
        };
        pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &range,
            });

        auto load_shader = [&](const char* name)
        {
            std::string spirv;
            klvk::Filesystem::ReadFile(GetShaderDir() / "fractal" / name, spirv);
            return context.CreateShaderModule(spirv, name);
        };
        const VkShaderModule vertex_shader = load_shader("fractal.vert.spv");
        const VkShaderModule fragment_shader = load_shader("fractal.frag.spv");
        auto destroy_shaders = klvk::OnScopeLeave(
            [&]
            {
                klvk::Vulkan::DestroyShaderModuleNE(device, vertex_shader);
                klvk::Vulkan::DestroyShaderModuleNE(device, fragment_shader);
            });
        const std::array stages{
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
            .lineWidth = 1.f,
        };
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        const VkPipelineColorBlendAttachmentState attachment{
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };
        const VkPipelineColorBlendStateCreateInfo blend{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &attachment,
        };
        const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
            .pDynamicStates = dynamic_states.data(),
        };
        const VkFormat format = GetSwapchainFormat();
        const VkPipelineRenderingCreateInfo rendering{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &format,
        };
        const VkGraphicsPipelineCreateInfo pipeline_info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering,
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisample,
            .pColorBlendState = &blend,
            .pDynamicState = &dynamic,
            .layout = pipeline_layout_,
        };
        pipeline_ = klvk::Vulkan::CreateGraphicsPipelines(device, {}, std::span{&pipeline_info, 1}).front();
    }

    void Tick() override
    {
        klvk::Application::Tick();
        ImGui::Begin("Fractal settings");
        ImGui::DragFloat2("Center", center_.data(), scale_ * 0.01f, -4.f, 4.f, "%.7f");
        ImGui::SliderFloat("Scale", &scale_, 0.0001f, 2.5f, "%.5f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderInt("Iterations", &iterations_, 16, 500);
        ImGui::Checkbox("Julia set", &julia_enabled_);
        if (julia_enabled_) ImGui::DragFloat2("Julia constant", julia_.data(), 0.001f, -1.f, 1.f, "%.4f");
        ImGui::SliderFloat("Palette", &palette_phase_, 0.f, 1.f);
        ImGui::End();

        const FractalPushConstants constants{
            .view = {center_[0], center_[1], scale_, GetWindow().GetAspect()},
            .julia =
                {
                    julia_enabled_ ? julia_[0] : 0.f,
                    julia_enabled_ ? julia_[1] : 0.f,
                    static_cast<float>(iterations_),
                    palette_phase_,
                },
        };
        const VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, constants);
        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
    }

public:
    ~FractalApp() override
    {
        if (pipeline_ == VK_NULL_HANDLE) return;
        klvk::DeviceContext& context = GetDeviceContext();
        context.WaitIdle();
        const VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroyPipelineNE(device, pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
    }

private:
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::array<float, 2> center_{-0.5f, 0.f};
    std::array<float, 2> julia_{-0.8f, 0.156f};
    float scale_ = 1.25f;
    float palette_phase_ = 0.f;
    int iterations_ = 150;
    bool julia_enabled_ = false;
};

void Main()
{
    FractalApp app;
    app.Run();
}

}  // namespace

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
