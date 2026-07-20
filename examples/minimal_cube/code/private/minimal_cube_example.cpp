#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/camera/camera_3d.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/math/transform.hpp"
#include "klvk/mesh/procedural_mesh_generator.hpp"
#include "klvk/ui/simple_type_widget.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vk_object.hpp"
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
        index_count_ = static_cast<u32>(mesh.indices.size());
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
        pipeline_layout_ = klvk::VkObject<VkPipelineLayout>{
            device,
            klvk::Vulkan::CreatePipelineLayout(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &push_constant_range,
                })};

        const std::filesystem::path shader_dir = GetShaderDir() / "just_color_3d";
        pipeline_ = klvk::VkObject<VkPipeline>{
            device,
            klvk::GraphicsPipelineBuilder(*this)
                .Layout(pipeline_layout_)
                .VertexShaderFile(shader_dir / "just_color_3d.vert")
                .FragmentShaderFile(shader_dir / "just_color_3d.frag")
                .Topology(mesh.topology)
                .CullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE)
                .VertexBinding(0, sizeof(edt::Vec3f), VK_VERTEX_INPUT_RATE_VERTEX)
                .VertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .Build()};
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
            camera_.SetRotation({.yaw = yaw + delta.x(), .pitch = pitch + delta.y(), .roll = roll});
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

private:
    std::unique_ptr<klvk::events::IEventListener> event_listener_;
    klvk::GpuBuffer vertex_buffer_;
    klvk::GpuBuffer index_buffer_;
    klvk::VkObject<VkPipelineLayout> pipeline_layout_;
    klvk::VkObject<VkPipeline> pipeline_;
    u32 index_count_ = 0;
    float move_speed_ = 5.f;
    klvk::Transform cube_transform_{.translation = {6, 6, 0}};
    klvk::Camera3d camera_{Vec3f{3, 3, 4}, {.yaw = 45, .pitch = 45}};
};

void Main(int argc, char** argv)
{
    CubeApp app;
    app.RunWithArguments(argc, argv);
}

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
