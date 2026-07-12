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
#include "klvk/math/rotator.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/ui/simple_type_widget.hpp"
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
    uint32_t particle_count = 0;
    std::array<uint32_t, 2> padding{};
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
    static constexpr uint32_t kParticleCount = 1'000'000;
    static constexpr uint32_t kWorkgroupSize = 256;

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
        const VkDescriptorSetLayoutBinding binding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT};
        set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
            device,
            {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding});
        const VkDescriptorPoolSize pool_size{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
        descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
            device,
            {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
             .maxSets = 1,
             .poolSizeCount = 1,
             .pPoolSizes = &pool_size});
        set_ = klvk::Vulkan::AllocateDescriptorSets(
                   device,
                   {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = descriptor_pool_,
                    .descriptorSetCount = 1,
                    .pSetLayouts = &set_layout_})
                   .front();

        // The particle state persists across frames (klgl keeps one SSBO), so a
        // single buffer serves every frame; barriers order the accesses.
        const std::vector particles = MakeParticles();
        const VkDeviceSize bytes = particles.size() * sizeof(Particle);
        buffer_ = klvk::GpuBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, bytes, true);
        buffer_.Write(std::as_bytes(std::span{particles}));
        const VkDescriptorBufferInfo buffer{.buffer = buffer_.GetHandle(), .range = bytes};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set_,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffer};
        klvk::Vulkan::UpdateDescriptorSets(device, std::span{&write, 1});
        const std::array ranges{
            VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = sizeof(SimulationPushConstants)},
            VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .size = sizeof(GraphicsPushConstants)},
        };
        pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
             .setLayoutCount = 1,
             .pSetLayouts = &set_layout_,
             .pushConstantRangeCount = static_cast<uint32_t>(ranges.size()),
             .pPushConstantRanges = ranges.data()});
        CreatePipelines(context);

        for (Body& body : bodies_) body.rotation = body.initial_rotation;
    }

    static std::vector<Particle> MakeParticles()
    {
        std::vector<Particle> particles(kParticleCount);
        const uint32_t side = static_cast<uint32_t>(std::round(std::cbrt(static_cast<float>(kParticleCount))));
        const Vec3f delta = Vec3f{} + 2.f / static_cast<float>(side);
        uint32_t index = 0;
        for (uint32_t x = 0; x != side && index != kParticleCount; ++x)
            for (uint32_t y = 0; y != side && index != kParticleCount; ++y)
                for (uint32_t z = 0; z != side && index != kParticleCount; ++z)
                {
                    const Vec3f position = Vec3<uint32_t>{x, y, z}.Cast<float>() * delta - 1.f;
                    particles[index++].position = Vec4f(position, 1.f);
                }
        return particles;
    }

    VkShaderModule Load(klvk::DeviceContext& context, const char* name)
    {
        std::string data;
        klvk::Filesystem::ReadFile(GetShaderDir() / "compute_shader" / name, data);
        return context.CreateShaderModule(data, name);
    }

    VkPipeline CreateGraphicsPipeline(
        klvk::DeviceContext& context,
        VkShaderModule vertex,
        VkShaderModule fragment,
        const VkSpecializationInfo* vertex_specialization = nullptr)
    {
        const std::array stages{
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertex,
                .pName = "main",
                .pSpecializationInfo = vertex_specialization},
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragment,
                .pName = "main"},
        };
        const VkPipelineVertexInputStateCreateInfo vertex_input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
        const VkPipelineViewportStateCreateInfo viewport{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1};
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .lineWidth = 1.f};
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
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
        const VkPipelineColorBlendStateCreateInfo blend{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &attachment};
        const std::array states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<uint32_t>(states.size()),
            .pDynamicStates = states.data()};
        const VkFormat format = GetSwapchainFormat();
        const VkPipelineRenderingCreateInfo rendering{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &format};
        const VkGraphicsPipelineCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering,
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &assembly,
            .pViewportState = &viewport,
            .pRasterizationState = &raster,
            .pMultisampleState = &multisample,
            .pColorBlendState = &blend,
            .pDynamicState = &dynamic,
            .layout = pipeline_layout_};
        return klvk::Vulkan::CreateGraphicsPipelines(context.GetDevice(), {}, std::span{&info, 1}).front();
    }

    void CreatePipelines(klvk::DeviceContext& context)
    {
        const VkDevice device = context.GetDevice();
        const VkShaderModule compute = Load(context, "particles.comp.spv");
        const VkShaderModule particles_vertex = Load(context, "particles.vert.spv");
        const VkShaderModule bodies_vertex = Load(context, "bodies.vert.spv");
        const VkShaderModule fragment = Load(context, "particles.frag.spv");
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
        compute_pipeline_ = klvk::Vulkan::CreateComputePipelines(device, {}, std::span{&compute_info, 1}).front();
        bodies_pipeline_ = CreateGraphicsPipeline(context, bodies_vertex, fragment);
        RebuildParticlesPipeline();
    }

    // klgl recompiles the particle shader when COLOR_FUNCTION changes; the
    // specialization constant requires a pipeline rebuild the same way.
    void RebuildParticlesPipeline()
    {
        auto& context = GetDeviceContext();
        const VkDevice device = context.GetDevice();
        if (particles_pipeline_)
        {
            context.WaitIdle();
            klvk::Vulkan::DestroyPipelineNE(device, particles_pipeline_);
        }

        const VkShaderModule vertex = Load(context, "particles.vert.spv");
        const VkShaderModule fragment = Load(context, "particles.frag.spv");
        auto cleanup = klvk::OnScopeLeave(
            [&]
            {
                klvk::Vulkan::DestroyShaderModuleNE(device, vertex);
                klvk::Vulkan::DestroyShaderModuleNE(device, fragment);
            });

        const int32_t color_function = color_function_;
        const VkSpecializationMapEntry entry{.constantID = 0, .offset = 0, .size = sizeof(color_function)};
        const VkSpecializationInfo specialization{
            .mapEntryCount = 1,
            .pMapEntries = &entry,
            .dataSize = sizeof(color_function),
            .pData = &color_function};
        particles_pipeline_ = CreateGraphicsPipeline(context, vertex, fragment, &specialization);
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
        if (pending_color_function_)
        {
            color_function_ = *pending_color_function_;
            pending_color_function_.reset();
            RebuildParticlesPipeline();
        }

        const VkDescriptorSet set = set_;
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
            int color_function = pending_color_function_.value_or(color_function_);
            if (ImGui::SliderInt("COLOR_FUNCTION", &color_function, 0, 2))
            {
                pending_color_function_ = color_function;
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

public:
    ~ComputeShaderApp() override
    {
        if (!compute_pipeline_) return;
        auto& context = GetDeviceContext();
        context.WaitIdle();
        const VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroyPipelineNE(device, compute_pipeline_);
        klvk::Vulkan::DestroyPipelineNE(device, particles_pipeline_);
        klvk::Vulkan::DestroyPipelineNE(device, bodies_pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
        klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
        klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, set_layout_);
    }

private:
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline compute_pipeline_ = VK_NULL_HANDLE;
    VkPipeline particles_pipeline_ = VK_NULL_HANDLE;
    VkPipeline bodies_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
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
    int color_function_ = 0;
    std::optional<int> pending_color_function_;
};

void Main()
{
    ComputeShaderApp app;
    app.Run();
}
}  // namespace

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
