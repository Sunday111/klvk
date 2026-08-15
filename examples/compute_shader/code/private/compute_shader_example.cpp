#include <fmt/format.h>
#include <imgui.h>

#include <array>
#include <edt/math/math.hpp>
#include <optional>
#include <string>

#include "edt/functional/on_scope_leave.hpp"
#include "edt/math/rotator.hpp"
#include "klvk/application.hpp"
#include "klvk/camera/camera_3d.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/ui/simple_type_widget.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/window.hpp"

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
    edt::Rotator initial_rotation{};
    edt::Rotator rotation_per_second{};
    edt::Rotator rotation{};
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
        listener_subscription_ = GetEventManager().AddEventListener(*listener_);

        auto& context = GetDeviceContext();
        descriptor_sets_ = klvk::DescriptorSets::Builder(context)
                               .Binding(
                                   0,
                                   vk::DescriptorType::eStorageBuffer,
                                   vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex)
                               .Build();

        // The particle state persists across frames (klgl keeps one SSBO), so a
        // single buffer serves every frame; barriers order the accesses.
        const std::vector particles = MakeParticles();
        const vk::DeviceSize bytes = particles.size() * sizeof(Particle);
        buffer_ = klvk::GpuBuffer(context, vk::BufferUsageFlagBits::eStorageBuffer, bytes, true);
        buffer_.Write(std::as_bytes(std::span{particles}));
        descriptor_sets_.WriteBuffer(0, 0, buffer_.GetHandle(), bytes);

        const std::array ranges{
            vk::PushConstantRange{}
                .setStageFlags(vk::ShaderStageFlagBits::eCompute)
                .setSize(sizeof(SimulationPushConstants)),
            vk::PushConstantRange{}
                .setStageFlags(vk::ShaderStageFlagBits::eVertex)
                .setSize(sizeof(GraphicsPushConstants)),
        };
        const auto set_layout = descriptor_sets_.GetLayoutView();
        pipeline_layout_ = klvk::PipelineLayout{context, std::span{&set_layout, 1}, ranges};
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
        {
            for (u32 y = 0; y != side && index != kParticleCount; ++y)
            {
                for (u32 z = 0; z != side && index != kParticleCount; ++z)
                {
                    const Vec3f position = Vec3<u32>{x, y, z}.Cast<float>() * delta - 1.f;
                    particles[index++].position = Vec4f(position, 1.f);
                }
            }
        }
        return particles;
    }

    klvk::ShaderModule Load(klvk::DeviceContext& context, const char* name)
    {
        return context.LoadShaderModule(GetShaderDir() / "compute_shader" / name);
    }

    vk::UniquePipeline CreateGraphicsPipeline(
        klvk::DeviceContext& context,
        const klvk::ShaderModule& vertex,
        const klvk::ShaderModule& fragment)
    {
        klvk::ShaderStages stages{vk::ShaderStageFlagBits::eVertex, vertex};
        stages.Append(klvk::ShaderStages{vk::ShaderStageFlagBits::eFragment, fragment});
        return CreateGraphicsPipeline(context, stages);
    }

    vk::UniquePipeline CreateGraphicsPipeline(klvk::DeviceContext& context, const klvk::ShaderStages& stages)
    {
        // Straight-alpha color blend but with the destination alpha left untouched
        // (dstAlpha = ZERO), so this needs an explicit attachment rather than the
        // AlphaBlend() preset.
        const vk::PipelineColorBlendAttachmentState attachment =
            vk::PipelineColorBlendAttachmentState{}
                .setBlendEnable(true)
                .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
                .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                .setColorBlendOp(vk::BlendOp::eAdd)
                .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
                .setAlphaBlendOp(vk::BlendOp::eAdd)
                .setColorWriteMask(
                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                    vk::ColorComponentFlagBits::eA);
        return klvk::GraphicsPipelineBuilder(context)
            .Layout(pipeline_layout_)
            .Stages(stages)
            .Topology(vk::PrimitiveTopology::ePointList)
            .Blend(attachment)
            .ColorFormat(GetSwapchainFormat())
            .Build();
    }

    void CreatePipelines(klvk::DeviceContext& context)
    {
        const vk::Device device = context.GetDevice();
        const klvk::ShaderModule compute = Load(context, "particles.comp.slang");
        const klvk::ShaderModule bodies_vertex = Load(context, "bodies.vert.slang");
        const klvk::ShaderModule fragment = Load(context, "particles.frag.slang");
        const klvk::ShaderStages compute_stage{vk::ShaderStageFlagBits::eCompute, compute};
        (void)pipeline_layout_.Validate(compute_stage);
        const vk::ComputePipelineCreateInfo compute_info = vk::ComputePipelineCreateInfo{}
                                                               .setStage(compute_stage.GetCreateInfos().front())
                                                               .setLayout(pipeline_layout_.GetHandle());
        const std::array compute_infos{compute_info};
        auto outcome = device.createComputePipelinesUnique(nullptr, compute_infos);
        compute_pipeline_ = std::move(outcome.value.front());
        klvk::VulkanCheck(outcome.result);
        bodies_pipeline_ = CreateGraphicsPipeline(context, bodies_vertex, fragment);

        klvk::Shader::shaders_dir_ = GetShaderDir();
        particles_shader_ = std::make_unique<klvk::Shader>(context, "compute_shader/particles");
        color_function_define_ = particles_shader_->GetDefine("COLOR_FUNCTION");
        RebuildParticlesPipeline();
    }

    // Specialization constant changes take effect only when the pipeline is rebuilt.
    void RebuildParticlesPipeline()
    {
        auto& context = GetDeviceContext();

        // The old pipeline may still be referenced by an in-flight frame, so wait
        // before the move-assign below destroys it.
        if (particles_pipeline_) context.WaitIdle();

        // The .comp stage belongs to the simulation pipeline, not this one.
        const auto stages =
            particles_shader_->MakeStages(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
        particles_pipeline_ = CreateGraphicsPipeline(context, stages);
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
        {
            positions[i] = Vec4f(
                edt::Math::TransformPos(bodies_[i].rotation.ToMatrix(), Vec3f{bodies_[i].orbit_radius, 0.f, 0.f}),
                1.f);
        }
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

        const vk::DescriptorSet set = descriptor_sets_.Get(0);
        const vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        std::array<Vec4f, 2> bodies{};
        if (time_steps_per_frame_ != 0)
        {
            const vk::BufferMemoryBarrier2 barrier =
                vk::BufferMemoryBarrier2{}
                    .setSrcStageMask(vk::PipelineStageFlagBits2::eVertexShader)
                    .setSrcAccessMask(vk::AccessFlagBits2::eShaderStorageRead)
                    .setDstStageMask(vk::PipelineStageFlagBits2::eComputeShader)
                    .setDstAccessMask(
                        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite)
                    .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                    .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                    .setBuffer(buffer_.GetHandle())
                    .setSize(vk::WholeSize);
            command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setBufferMemoryBarriers(barrier));
        }
        command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute_pipeline_.get());
        command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            pipeline_layout_.GetHandle(),
            0,
            std::span{&set, 1},
            {});
        for (int step = 0; step != time_steps_per_frame_; ++step)
        {
            bodies = UpdateBodies();
            const SimulationPushConstants simulation{
                .body_a = bodies[0],
                .body_b = bodies[1],
                .delta_time = time_step_,
                .particle_count = kParticleCount};
            command_buffer.pushConstants(
                pipeline_layout_.GetHandle(),
                vk::ShaderStageFlagBits::eCompute,
                0,
                sizeof(simulation),
                &simulation);
            command_buffer.dispatch((kParticleCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1);
            const vk::PipelineStageFlags2 destination_stage = step + 1 == time_steps_per_frame_
                                                                  ? vk::PipelineStageFlagBits2::eVertexShader
                                                                  : vk::PipelineStageFlagBits2::eComputeShader;
            const vk::AccessFlags2 destination_access =
                step + 1 == time_steps_per_frame_
                    ? vk::AccessFlagBits2::eShaderStorageRead
                    : vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;
            const vk::BufferMemoryBarrier2 barrier = vk::BufferMemoryBarrier2{}
                                                         .setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
                                                         .setSrcAccessMask(vk::AccessFlagBits2::eShaderStorageWrite)
                                                         .setDstStageMask(destination_stage)
                                                         .setDstAccessMask(destination_access)
                                                         .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                                                         .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                                                         .setBuffer(buffer_.GetHandle())
                                                         .setSize(vk::WholeSize);
            const vk::DependencyInfo dependency = vk::DependencyInfo{}.setBufferMemoryBarriers(barrier);
            command_buffer.pipelineBarrier2(dependency);
        }
        if (time_steps_per_frame_ == 0) bodies = UpdateBodies();

        GraphicsPushConstants graphics = MakeGraphicsConstants(bodies);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, particles_pipeline_.get());
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
            sizeof(graphics),
            &graphics);
        command_buffer.draw(kParticleCount, 1, 0, 0);
        graphics.color = {1.f, 0.f, 0.f, 1.f};
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, bodies_pipeline_.get());
        command_buffer.pushConstants(
            pipeline_layout_.GetHandle(),
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(graphics),
            &graphics);
        command_buffer.draw(2, 1, 0, 0);
        RenderGui();
    }

    void RenderGui()
    {
        ImGui::Begin("Settings");
        camera_.Widget();
        ImGui::SliderFloat("Camera speed", &camera_speed_, 0.1f, 20.f);
        const std::string framerate = fmt::format("Framerate: {:.1f}", GetFramerate());
        ImGui::TextUnformatted(framerate.c_str());
        ImGui::SliderFloat("Time step", &time_step_, 0.f, 0.0001f, "%.6f");
        ImGui::SliderInt("Time steps per frame", &time_steps_per_frame_, 0, 40);
        ImGui::SliderFloat("Particle alpha", &particle_alpha_, 0.0001f, 1.f, "%.4f");

        if (ImGui::CollapsingHeader("Bodies"))
        {
            auto rotator_widget = [](std::string_view title, edt::Rotator& rotator)
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
        {
            camera_.SetEye(camera_.GetEye() + delta * camera_speed_ * GetLastFrameDurationSeconds());
        }
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
    klvk::PipelineLayout pipeline_layout_;
    vk::UniquePipeline compute_pipeline_;
    vk::UniquePipeline particles_pipeline_;
    vk::UniquePipeline bodies_pipeline_;
    klvk::GpuBuffer buffer_;
    std::unique_ptr<klvk::events::IEventListener> listener_;
    klvk::events::EventSubscription listener_subscription_;
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
