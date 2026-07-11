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

struct Vertex
{
    edt::Vec3f position{};
    edt::Vec3f normal{};
};

struct SceneUniforms
{
    std::array<edt::Vec4f, 4> view_projection_columns{};
    edt::Vec4f view_position{};
    edt::Vec4f light_position{};
    edt::Vec4f light_color{};
    edt::Vec4f object_color{};
    float ambient = 0.1f;
    float specular = 0.5f;
    edt::Vec2f padding{};
};

struct ModelPushConstants
{
    std::array<edt::Vec4f, 4> columns{};
};

static_assert(sizeof(SceneUniforms) == 144);
static_assert(sizeof(ModelPushConstants) == 64);

class SimpleLitCubeApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();
        SetDepthBufferEnabled(true);
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Simple lit cubes");

        event_listener_ = klvk::events::EventListenerMethodCallbacks<&SimpleLitCubeApp::OnMouseMove>::CreatePtr(this);
        GetEventManager().AddEventListener(*event_listener_);

        klvk::DeviceContext& context = GetDeviceContext();
        const auto mesh = klvk::ProceduralMeshGenerator::GenerateCubeMesh();
        std::vector<Vertex> vertices(mesh.vertices.size());
        for (size_t index = 0; index != vertices.size(); ++index)
        {
            vertices[index] = {.position = mesh.vertices[index], .normal = mesh.normals[index]};
        }
        index_count_ = static_cast<uint32_t>(mesh.indices.size());
        vertex_buffer_ = klvk::GpuBuffer(
            context,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertices.size() * sizeof(vertices.front()),
            true);
        vertex_buffer_.Write(std::as_bytes(std::span{vertices}));
        index_buffer_ = klvk::GpuBuffer(
            context,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            mesh.indices.size() * sizeof(mesh.indices.front()),
            true);
        index_buffer_.Write(std::as_bytes(std::span{mesh.indices}));

        CreateDescriptors(context);
        CreatePipeline(context, mesh.topology);

        for (int x = -10; x != 11; ++x)
        {
            for (int y = -10; y != 11; ++y)
            {
                cubes_.push_back(
                    klvk::Transform{
                        .translation = edt::Vec3i{x, y, 0}.Cast<float>(),
                        .scale = {0.3f, 0.3f, 0.3f},
                    }
                        .Matrix());
            }
        }
    }

    void CreateDescriptors(klvk::DeviceContext& context)
    {
        VkDevice device = context.GetDevice();
        const VkDescriptorSetLayoutBinding binding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        descriptor_set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &binding,
            });

        constexpr uint32_t frames = static_cast<uint32_t>(kFramesInFlight);
        const VkDescriptorPoolSize pool_size{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = frames};
        descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = frames,
                .poolSizeCount = 1,
                .pPoolSizes = &pool_size,
            });
        std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
        layouts.fill(descriptor_set_layout_);
        const auto sets = klvk::Vulkan::AllocateDescriptorSets(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool_,
                .descriptorSetCount = frames,
                .pSetLayouts = layouts.data(),
            });

        for (size_t index = 0; index != kFramesInFlight; ++index)
        {
            descriptor_sets_[index] = sets[index];
            uniform_buffers_[index] =
                klvk::GpuBuffer(context, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(SceneUniforms), true);
            const VkDescriptorBufferInfo buffer_info{
                .buffer = uniform_buffers_[index].GetHandle(),
                .range = sizeof(SceneUniforms),
            };
            const VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_sets_[index],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_info,
            };
            klvk::Vulkan::UpdateDescriptorSets(device, std::span{&write, 1});
        }
    }

    void CreatePipeline(klvk::DeviceContext& context, VkPrimitiveTopology topology)
    {
        VkDevice device = context.GetDevice();
        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(ModelPushConstants),
        };
        pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &descriptor_set_layout_,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &push_constant_range,
            });

        auto load_shader = [&](const char* name)
        {
            std::string spirv;
            klvk::Filesystem::ReadFile(GetShaderDir() / "basic_light_3d" / name, spirv);
            return context.CreateShaderModule(spirv, name);
        };
        VkShaderModule vertex_shader = load_shader("basic_light_3d.vert.spv");
        VkShaderModule fragment_shader = load_shader("basic_light_3d.frag.spv");
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
        const VkVertexInputBindingDescription vertex_binding{
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        const std::array attributes{
            VkVertexInputAttributeDescription{
                .location = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, position),
            },
            VkVertexInputAttributeDescription{
                .location = 1,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, normal),
            },
        };
        const VkPipelineVertexInputStateCreateInfo vertex_input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &vertex_binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data(),
        };
        const VkPipelineInputAssemblyStateCreateInfo input_assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = topology,
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
        const VkPipelineDepthStencilStateCreateInfo depth_stencil{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
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
        const VkFormat depth_format = GetDepthFormat();
        const VkPipelineRenderingCreateInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_format,
            .depthAttachmentFormat = depth_format,
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
            .pDepthStencilState = &depth_stencil,
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

        const edt::Mat4f view = camera_.GetViewMatrix().Transposed();
        const edt::Mat4f projection = camera_.GetProjectionMatrix(GetWindow().GetAspect()).Transposed();
        const edt::Mat4f view_projection = projection.MatMul(view);
        SceneUniforms uniforms{
            .view_position = edt::Vec4f{camera_.GetEye().x(), camera_.GetEye().y(), camera_.GetEye().z(), 1.f},
            .light_position = {9.f, 9.f, 3.f, 1.f},
            .light_color = {1.f, 1.f, 1.f, 1.f},
            .object_color = {1.f, 0.f, 0.f, 1.f},
        };
        for (size_t column = 0; column != 4; ++column)
        {
            uniforms.view_projection_columns[column] = view_projection.GetColumn(column);
        }
        const size_t frame_index = GetFrameInFlightIndex();
        uniform_buffers_[frame_index].Write(std::as_bytes(std::span{&uniforms, 1}));

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        const VkBuffer vertex_buffer = vertex_buffer_.GetHandle();
        constexpr VkDeviceSize offset = 0;
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0,
            std::span{&descriptor_sets_[frame_index], 1});
        klvk::Vulkan::CmdBindVertexBuffers(command_buffer, 0, std::span{&vertex_buffer, 1}, std::span{&offset, 1});
        klvk::Vulkan::CmdBindIndexBuffer(command_buffer, index_buffer_.GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        for (const edt::Mat4f& model : cubes_)
        {
            ModelPushConstants model_constants;
            for (size_t column = 0; column != 4; ++column)
            {
                model_constants.columns[column] = model.GetColumn(column);
            }
            klvk::Vulkan::CmdPushConstants(
                command_buffer,
                pipeline_layout_,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                model_constants);
            klvk::Vulkan::CmdDrawIndexed(command_buffer, index_count_, 1, 0, 0, 0);
        }

        if (ImGui::Begin("Settings"))
        {
            camera_.Widget();
            ImGui::Separator();
            klvk::SimpleTypeWidget("move_speed", move_speed_);
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
    ~SimpleLitCubeApp() override
    {
        if (pipeline_ == VK_NULL_HANDLE) return;
        klvk::DeviceContext& context = GetDeviceContext();
        context.WaitIdle();
        VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroyPipelineNE(device, pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
        klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
        klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, descriptor_set_layout_);
    }

private:
    std::unique_ptr<klvk::events::IEventListener> event_listener_;
    klvk::GpuBuffer vertex_buffer_;
    klvk::GpuBuffer index_buffer_;
    std::array<klvk::GpuBuffer, kFramesInFlight> uniform_buffers_;
    std::array<VkDescriptorSet, kFramesInFlight> descriptor_sets_{};
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    uint32_t index_count_ = 0;
    float move_speed_ = 5.f;
    std::vector<edt::Mat4f> cubes_;
    klvk::Camera3d camera_{Vec3f{3, 3, 4}, {.yaw = 45, .pitch = 45}};
};

void Main()
{
    SimpleLitCubeApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
