#include <imgui.h>

#include <edt/math/math.hpp>

#include "klvk/application.hpp"
#include "klvk/camera/camera_3d.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/mesh/procedural_mesh_generator.hpp"
#include "klvk/ui/simple_type_widget.hpp"
#include "klvk/ui/transform_widget.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/window.hpp"

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
        const auto mesh = klvk::ProceduralMeshGenerator::GenerateCubeMesh();
        index_count_ = static_cast<u32>(mesh.indices.size());
        vertex_buffer_ = klvk::GpuBuffer(
            context,
            vk::BufferUsageFlagBits::eVertexBuffer,
            mesh.vertices.size() * sizeof(mesh.vertices.front()),
            true);
        vertex_buffer_.Write(std::as_bytes(std::span{mesh.vertices}));
        index_buffer_ = klvk::GpuBuffer(
            context,
            vk::BufferUsageFlagBits::eIndexBuffer,
            mesh.indices.size() * sizeof(mesh.indices.front()),
            true);
        index_buffer_.Write(std::as_bytes(std::span{mesh.indices}));

        const vk::PushConstantRange push_constant_range =
            vk::PushConstantRange{}.setStageFlags(vk::ShaderStageFlagBits::eVertex).setSize(sizeof(PushConstants));
        pipeline_layout_ = klvk::PipelineLayout{context, {}, std::span{&push_constant_range, 1}};

        const std::filesystem::path shader_dir = GetShaderDir() / "just_color_3d";
        pipeline_ = klvk::GraphicsPipelineBuilder(*this)
                        .Layout(pipeline_layout_)
                        .VertexShaderFile(shader_dir / "just_color_3d.vert.slang")
                        .FragmentShaderFile(shader_dir / "just_color_3d.frag.slang")
                        .Topology(mesh.topology)
                        .CullMode(vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise)
                        .VertexBinding(0, sizeof(edt::Vec3f), vk::VertexInputRate::eVertex)
                        .VertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0)
                        .Build();
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

        vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        const vk::Buffer vertex_buffer = vertex_buffer_.GetHandle();
        constexpr vk::DeviceSize offset = 0;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
        command_buffer.bindVertexBuffers(0, std::span{&vertex_buffer, 1}, std::span{&offset, 1});
        command_buffer.bindIndexBuffer(index_buffer_.GetHandle(), 0, vk::IndexType::eUint32);
        command_buffer.pushConstants(
            pipeline_layout_.GetHandle(),
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(push_constants),
            &push_constants);
        command_buffer.drawIndexed(index_count_, 1, 0, 0, 0);

        if (ImGui::Begin("Settings"))
        {
            camera_.Widget();
            ImGui::Separator();
            klvk::SimpleTypeWidget("move_speed", move_speed_);
            if (ImGui::CollapsingHeader("cube")) klvk::TransformWidget(cube_transform_);
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
    klvk::PipelineLayout pipeline_layout_;
    vk::UniquePipeline pipeline_;
    u32 index_count_ = 0;
    float move_speed_ = 5.f;
    edt::Transform cube_transform_{.translation = {6, 6, 0}};
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
