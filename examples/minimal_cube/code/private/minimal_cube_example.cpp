#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/camera/camera_3d.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/math/transform.hpp"
#include "klvk/mesh/procedural_mesh_generator.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/ui/simple_type_widget.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

using namespace edt::lazy_matrix_aliases;  // NOLINT

struct PushConstants
{
    std::array<edt::Vec4f, 4> transform_columns{};
    edt::Vec4f color{};
};

class CubeApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();

        event_listener_ = klvk::events::EventListenerMethodCallbacks<&CubeApp::OnMouseMove>::CreatePtr(this);
        GetEventManager().AddEventListener(*event_listener_);

        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Cube");

        klvk::DeviceContext& context = GetDeviceContext();
        VkDevice device = context.GetDevice();
        const auto mesh = klvk::ProceduralMeshGenerator::GenerateCubeMesh();
        index_count_ = static_cast<uint32_t>(mesh.indices.size());
        vertex_buffer_ = klvk::GpuBuffer(
            context,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            mesh.vertices.size() * sizeof(mesh.vertices.front()),
            true);
        vertex_buffer_.Write(std::as_bytes(std::span{mesh.vertices}));
        index_buffer_ = klvk::GpuBuffer(
            context,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            mesh.indices.size() * sizeof(mesh.indices.front()),
            true);
        index_buffer_.Write(std::as_bytes(std::span{mesh.indices}));

        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
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
            return context.CreateShaderModuleFromSource(GetShaderDir() / "just_color_3d" / name);
        };
        VkShaderModule vertex_shader = load_shader("just_color_3d.vert");
        VkShaderModule fragment_shader = load_shader("just_color_3d.frag");
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
        const VkVertexInputBindingDescription binding{
            .binding = 0,
            .stride = sizeof(edt::Vec3f),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        const VkVertexInputAttributeDescription attribute{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
        };
        const VkPipelineVertexInputStateCreateInfo vertex_input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = 1,
            .pVertexAttributeDescriptions = &attribute,
        };
        const VkPipelineInputAssemblyStateCreateInfo input_assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = mesh.topology,
        };
        const VkPipelineViewportStateCreateInfo viewport_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };
        const VkPipelineRasterizationStateCreateInfo rasterization{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
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
        HandleInput();

        const edt::Mat4f model = cube_transform_.Matrix();
        const edt::Mat4f view = camera_.GetViewMatrix().Transposed();
        const edt::Mat4f projection = camera_.GetProjectionMatrix(GetWindow().GetAspect()).Transposed();
        const edt::Mat4f transform = projection.MatMul(view.MatMul(model));
        PushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };
        for (size_t column = 0; column != 4; ++column)
        {
            push_constants.transform_columns[column] = transform.GetColumn(column);
        }

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        const VkBuffer vertex_buffer = vertex_buffer_.GetHandle();
        constexpr VkDeviceSize offset = 0;
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdBindVertexBuffers(command_buffer, 0, std::span{&vertex_buffer, 1}, std::span{&offset, 1});
        klvk::Vulkan::CmdBindIndexBuffer(command_buffer, index_buffer_.GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        klvk::Vulkan::CmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, push_constants);
        klvk::Vulkan::CmdDrawIndexed(command_buffer, index_count_, 1, 0, 0, 0);

        if (ImGui::Begin("Settings"))
        {
            camera_.Widget();
            ImGui::Separator();
            klvk::SimpleTypeWidget("move_speed", move_speed_);
            if (ImGui::CollapsingHeader("cube")) cube_transform_.Widget();
        }
        ImGui::End();
    }

    void OnMouseMove(const klvk::events::OnMouseMove& event)
    {
        constexpr float sensitivity = 0.01f;
        if (GetWindow().IsFocused() && GetWindow().IsInInputMode() && !ImGui::GetIO().WantCaptureMouse)
        {
            const auto delta = (event.current - event.previous) * sensitivity;
            const auto [yaw, pitch, roll] = camera_.GetRotation();
            camera_.SetRotation({yaw + delta.x(), pitch + delta.y(), roll});
        }
    }

    void HandleInput()
    {
        if (ImGui::GetIO().WantCaptureKeyboard) return;
        int right = 0;
        int forward = 0;
        int up = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) ++forward;
        if (ImGui::IsKeyDown(ImGuiKey_S)) --forward;
        if (ImGui::IsKeyDown(ImGuiKey_D)) ++right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) --right;
        if (ImGui::IsKeyDown(ImGuiKey_E)) ++up;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) --up;
        if (std::abs(right) + std::abs(forward) + std::abs(up) == 0) return;

        Vec3f delta = static_cast<float>(forward) * camera_.GetForwardAxis();
        delta += static_cast<float>(right) * camera_.GetRightAxis();
        delta += static_cast<float>(up) * camera_.GetUpAxis();
        camera_.SetEye(camera_.GetEye() + delta * move_speed_ * GetLastFrameDurationSeconds());
    }

public:
    ~CubeApp() override
    {
        if (pipeline_ == VK_NULL_HANDLE) return;
        klvk::DeviceContext& context = GetDeviceContext();
        context.WaitIdle();
        klvk::Vulkan::DestroyPipelineNE(context.GetDevice(), pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(context.GetDevice(), pipeline_layout_);
    }

private:
    std::unique_ptr<klvk::events::IEventListener> event_listener_;
    klvk::GpuBuffer vertex_buffer_;
    klvk::GpuBuffer index_buffer_;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    uint32_t index_count_ = 0;
    float move_speed_ = 5.f;
    klvk::Transform cube_transform_{.translation = {6, 6, 0}};
    klvk::Camera3d camera_{Vec3f{3, 3, 4}, {.yaw = 45, .pitch = 45}};
};

void Main()
{
    CubeApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
