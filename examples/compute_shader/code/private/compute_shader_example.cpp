#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>
#include <optional>

#include "klvk/application.hpp"
#include "klvk/camera/camera_3d.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/math/rotator.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/ui/simple_type_widget.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vk_object.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace
{
using namespace edt::lazy_matrix_aliases;  // NOLINT

struct Particle
{
    Vec4f position{};
    Vec4f velocity{};
};

struct SimulationPushConstants
{
    Vec4f body_a{};
    Vec4f body_b{};
    float delta_time = 0.f;
    u32 particle_count = 0;
    std::array<u32, 2> padding{};
};

struct GraphicsPushConstants
{
    Mat4f mvp{};
    Vec4f color{};
    Vec4f body_a{};
    Vec4f body_b{};
};

static_assert(sizeof(GraphicsPushConstants) == 112);

struct Body
{
    Vec3f orbit_center{};
    float orbit_radius = 5.f;
    klvk::Rotator initial_rotation{};
    klvk::Rotator rotation_per_second{};
    klvk::Rotator rotation{};
};

class ComputeShaderApp : public klvk::Application
{
    static constexpr u32 kParticleCount = 1'000'000;
    static constexpr u32 kWorkgroupSize = 256;

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        // Same title as the klgl version of this example.
        GetWindow().SetTitle("Painter 2d");
        SetTargetFramerate(60.f);
        listener_ = klvk::events::EventListenerMethodCallbacks<&ComputeShaderApp::OnMouseMove>::CreatePtr(this);
        GetEventManager().AddEventListener(*listener_);

        auto& context = GetDeviceContext();
        const VkDevice device = context.GetDevice();
        descriptor_sets_ =
            klvk::DescriptorSets::Builder(context)
                .Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT)
                .Build();

        // The particle state persists across frames (klgl keeps one SSBO), so a
        // single buffer serves every frame; barriers order the accesses.
        const std::vector particles = MakeParticles();
        const VkDeviceSize bytes = particles.size() * sizeof(Particle);
        buffer_ = klvk::GpuBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, bytes, true);
        buffer_.Write(std::as_bytes(std::span{particles}));
        descriptor_sets_.WriteBuffer(0, 0, buffer_.GetHandle(), bytes);

        const std::array ranges{
            VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = sizeof(SimulationPushConstants)},
            VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .size = sizeof(GraphicsPushConstants)},
        };
        const VkDescriptorSetLayout set_layout = descriptor_sets_.GetLayout();
        pipeline_layout_ = klvk::VkObject<VkPipelineLayout>{
            device,
            klvk::Vulkan::CreatePipelineLayout(
                device,
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                 .setLayoutCount = 1,
                 .pSetLayouts = &set_layout,
                 .pushConstantRangeCount = static_cast<u32>(ranges.size()),
                 .pPushConstantRanges = ranges.data()})};
        CreatePipelines(context);

        for (Body& body : bodies_) body.rotation = body.initial_rotation;
    }

    static std::vector<Particle> MakeParticles()
    {
        std::vector<Particle> particles(kParticleCount);
        const u32 side = static_cast<u32>(std::round(std::cbrt(static_cast<float>(kParticleCount))));
        const Vec3f delta = Vec3f{} + 2.f / static_cast<float>(side);
        u32 index = 0;
        for (u32 x = 0; x != side && index != kParticleCount; ++x)
            for (u32 y = 0; y != side && index != kParticleCount; ++y)
                for (u32 z = 0; z != side && index != kParticleCount; ++z)
                {
                    const Vec3f position = Vec3<u32>{x, y, z}.Cast<float>() * delta - 1.f;
                    particles[index++].position = Vec4f(position, 1.f);
                }
        return particles;
    }

    VkShaderModule Load(klvk::DeviceContext& context, const char* name)
    {
        return context.CreateShaderModuleFromSource(GetShaderDir() / "compute_shader" / name);
    }

    VkPipeline CreateGraphicsPipeline(klvk::DeviceContext& context, VkShaderModule vertex, VkShaderModule fragment)
    {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertex,
                .pName = "main"},
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragment,
                .pName = "main"},
        };
        return CreateGraphicsPipeline(context, stages);
    }

    VkPipeline CreateGraphicsPipeline(
        klvk::DeviceContext& context,
        std::span<const VkPipelineShaderStageCreateInfo> stages)
    {
        // Straight-alpha color blend but with the destination alpha left untouched
        // (dstAlpha = ZERO), so this needs an explicit attachment rather than the
        // AlphaBlend() preset.
        const VkPipelineColorBlendAttachmentState attachment{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT};
        return klvk::GraphicsPipelineBuilder(context)
            .Layout(pipeline_layout_)
            .Stages(stages)
            .Topology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
            .Blend(attachment)
            .ColorFormat(GetSwapchainFormat())
            .Build();
    }

    void CreatePipelines(klvk::DeviceContext& context)
    {
        const VkDevice device = context.GetDevice();
        const VkShaderModule compute = Load(context, "particles.comp");
        const VkShaderModule particles_vertex = Load(context, "particles.vert");
        const VkShaderModule bodies_vertex = Load(context, "bodies.vert");
        const VkShaderModule fragment = Load(context, "particles.frag");
        auto cleanup = klvk::OnScopeLeave(
            [&]
            {
                klvk::Vulkan::DestroyShaderModuleNE(device, compute);
                klvk::Vulkan::DestroyShaderModuleNE(device, particles_vertex);
                klvk::Vulkan::DestroyShaderModuleNE(device, bodies_vertex);
                klvk::Vulkan::DestroyShaderModuleNE(device, fragment);
            });
        const VkComputePipelineCreateInfo compute_info{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage =
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                 .module = compute,
                 .pName = "main"},
            .layout = pipeline_layout_};
        compute_pipeline_ = klvk::VkObject<VkPipeline>{
            device,
            klvk::Vulkan::CreateComputePipelines(device, {}, std::span{&compute_info, 1}).front()};
        bodies_pipeline_ = klvk::VkObject<VkPipeline>{device, CreateGraphicsPipeline(context, bodies_vertex, fragment)};

        klvk::Shader::shaders_dir_ = GetShaderDir();
        particles_shader_ = std::make_unique<klvk::Shader>(context, "compute_shader/particles");
        color_function_define_ = particles_shader_->GetDefine("COLOR_FUNCTION");
        RebuildParticlesPipeline();
    }

    // klgl recompiles the particle shader when COLOR_FUNCTION changes; the
    // specialization constants in klvk::Shader require a pipeline rebuild the same way.
    void RebuildParticlesPipeline()
    {
        auto& context = GetDeviceContext();

        // The old pipeline may still be referenced by an in-flight frame, so wait
        // before the move-assign below destroys it.
        if (particles_pipeline_.IsValid()) context.WaitIdle();

        // The .comp stage belongs to the simulation pipeline, not this one.
        const auto stages =
            particles_shader_->MakeShaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        particles_pipeline_ = klvk::VkObject<VkPipeline>{context.GetDevice(), CreateGraphicsPipeline(context, stages)};
        particles_pipeline_shader_version_ = particles_shader_->GetVersion();
    }

    std::array<Vec4f, 2> UpdateBodies()
    {
        for (Body& body : bodies_)
        {
            body.rotation += body.rotation_per_second * time_step_;
            body.rotation.yaw = std::fmod(body.rotation.yaw, 360.f);
            body.rotation.pitch = std::fmod(body.rotation.pitch, 360.f);
            body.rotation.roll = std::fmod(body.rotation.roll, 360.f);
        }
        std::array<Vec4f, 2> positions{};
        for (size_t i = 0; i != bodies_.size(); ++i)
            positions[i] = Vec4f(
                edt::Math::TransformPos(bodies_[i].rotation.ToMatrix(), Vec3f{bodies_[i].orbit_radius, 0.f, 0.f}),
                1.f);
        return positions;
    }

    GraphicsPushConstants MakeGraphicsConstants(const std::array<Vec4f, 2>& bodies) const
    {
        // Camera matrices are stored transposed (column-major of the true matrix),
        // so multiplying them in reverse order yields the column-major projection *
        // view product that the shader's mat4 reads directly - same idiom as klgl.
        return GraphicsPushConstants{
            .mvp = camera_.GetViewMatrix().MatMul(camera_.GetProjectionMatrix(GetWindow().GetAspect())),
            .color = {1.f, 1.f, 1.f, particle_alpha_},
            .body_a = bodies[0],
            .body_b = bodies[1]};
    }

    void Tick() override
    {
        klvk::Application::Tick();
        HandleInput();
        if (particles_pipeline_shader_version_ != particles_shader_->GetVersion())
        {
            RebuildParticlesPipeline();
        }

        const VkDescriptorSet set = descriptor_sets_.Get(0);
        const VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        std::array<Vec4f, 2> bodies{};
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline_layout_,
            0,
            std::span{&set, 1});
        for (int step = 0; step != time_steps_per_frame_; ++step)
        {
            bodies = UpdateBodies();
            const SimulationPushConstants simulation{
                .body_a = bodies[0],
                .body_b = bodies[1],
                .delta_time = time_step_,
                .particle_count = kParticleCount};
            klvk::Vulkan::CmdPushConstants(
                command_buffer,
                pipeline_layout_,
                VK_SHADER_STAGE_COMPUTE_BIT,
                0,
                simulation);
            klvk::Vulkan::CmdDispatch(command_buffer, (kParticleCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1);
            const VkBufferMemoryBarrier2 barrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = step + 1 == time_steps_per_frame_ ? VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                                                                  : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = step + 1 == time_steps_per_frame_
                                     ? VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                                     : VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = buffer_.GetHandle(),
                .size = VK_WHOLE_SIZE};
            const VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &barrier};
            klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
        }
        if (time_steps_per_frame_ == 0) bodies = UpdateBodies();

        GraphicsPushConstants graphics = MakeGraphicsConstants(bodies);
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particles_pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0,
            std::span{&set, 1});
        klvk::Vulkan::CmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, graphics);
        klvk::Vulkan::CmdDraw(command_buffer, kParticleCount, 1, 0, 0);
        graphics.color = {1.f, 0.f, 0.f, 1.f};
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bodies_pipeline_);
        klvk::Vulkan::CmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, graphics);
        klvk::Vulkan::CmdDraw(command_buffer, 2, 1, 0, 0);
        RenderGui();
    }

    void RenderGui()
    {
        ImGui::Begin("Settings");
        camera_.Widget();
        ImGui::SliderFloat("Camera speed", &camera_speed_, 0.1f, 20.f);
        ImGui::Text("Framerate: %.1f", static_cast<double>(GetFramerate()));
        ImGui::SliderFloat("Time step", &time_step_, 0.f, 0.0001f, "%.6f");
        ImGui::SliderInt("Time steps per frame", &time_steps_per_frame_, 0, 40);
        ImGui::SliderFloat("Particle alpha", &particle_alpha_, 0.0001f, 1.f, "%.4f");

        if (ImGui::CollapsingHeader("Bodies"))
        {
            auto rotator_widget = [](std::string_view title, klvk::Rotator& rotator)
            {
                if (ImGui::CollapsingHeader(title.data()))
                {
                    klvk::SimpleTypeWidget("yaw", rotator.yaw);
                    klvk::SimpleTypeWidget("pitch", rotator.pitch);
                    klvk::SimpleTypeWidget("roll", rotator.roll);
                }
            };

            for (Body& body : bodies_)
            {
                ImGui::PushID(&body);
                if (ImGui::CollapsingHeader("Body"))
                {
                    klvk::SimpleTypeWidget("Orbit center", body.orbit_center);
                    klvk::SimpleTypeWidget("Orbit radius", body.orbit_radius);
                    rotator_widget("Initial rotation", body.initial_rotation);
                    rotator_widget("Rotation per second", body.rotation_per_second);
                    rotator_widget("Current rotation", body.rotation);
                }
                ImGui::PopID();
            }
        }

        if (ImGui::CollapsingHeader("Shader"))
        {
            int color_function = particles_shader_->GetDefineValue<i32>(color_function_define_);
            if (ImGui::SliderInt("COLOR_FUNCTION", &color_function, 0, 2))
            {
                // Applied at the start of the next frame: this frame's command
                // buffer still references the current pipeline.
                particles_shader_->SetDefineValue(color_function_define_, color_function);
            }
        }
        ImGui::End();
    }

    void HandleInput()
    {
        if (ImGui::GetIO().WantCaptureKeyboard) return;
        int right = 0, forward = 0, up = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) ++forward;
        if (ImGui::IsKeyDown(ImGuiKey_S)) --forward;
        if (ImGui::IsKeyDown(ImGuiKey_D)) ++right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) --right;
        if (ImGui::IsKeyDown(ImGuiKey_E)) ++up;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) --up;
        Vec3f delta = camera_.GetForwardAxis() * static_cast<float>(forward) +
                      camera_.GetRightAxis() * static_cast<float>(right) + camera_.GetUpAxis() * static_cast<float>(up);
        if (delta.SquaredLength() > 0.f)
            camera_.SetEye(camera_.GetEye() + delta * camera_speed_ * GetLastFrameDurationSeconds());
    }

    void OnMouseMove(const klvk::events::OnMouseMove& event)
    {
        if (!GetWindow().IsFocused() || !GetWindow().IsInInputMode() || ImGui::GetIO().WantCaptureMouse) return;
        const Vec2f delta = (event.current - event.previous) * 0.01f;
        const auto rotation = camera_.GetRotation();
        camera_.SetRotation(
            {.yaw = rotation.yaw + delta.x(), .pitch = rotation.pitch + delta.y(), .roll = rotation.roll});
    }

private:
    klvk::DescriptorSets descriptor_sets_;
    klvk::VkObject<VkPipelineLayout> pipeline_layout_;
    klvk::VkObject<VkPipeline> compute_pipeline_;
    klvk::VkObject<VkPipeline> particles_pipeline_;
    klvk::VkObject<VkPipeline> bodies_pipeline_;
    klvk::GpuBuffer buffer_;
    std::unique_ptr<klvk::events::IEventListener> listener_;
    klvk::Camera3d camera_{Vec3f{0.f, 15.f, 0.f}, {.yaw = -90.f}};
    std::array<Body, 2> bodies_{
        Body{
            .orbit_center{0, 0, 0},
            .orbit_radius = 5.f,
            .initial_rotation = {.pitch = 0.f},
            .rotation_per_second = {.yaw = 500.f, .pitch = 600.f, .roll = 700.f}},
        Body{
            .orbit_center{0, 0, 0},
            .orbit_radius = 5.f,
            .initial_rotation = {.pitch = 180.f},
            .rotation_per_second = {.yaw = 500.f, .pitch = 600.f, .roll = 700.f}},
    };
    int time_steps_per_frame_ = 30;
    float camera_speed_ = 5.f;
    float time_step_ = 0.f;
    float particle_alpha_ = 0.1f;
    std::unique_ptr<klvk::Shader> particles_shader_;
    klvk::DefineHandle color_function_define_;
    size_t particles_pipeline_shader_version_ = 0;
};

void Main(int argc, char** argv)
{
    ComputeShaderApp app;
    app.RunWithArguments(argc, argv);
}
}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
