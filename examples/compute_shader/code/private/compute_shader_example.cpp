#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
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
    Vec4f attractor_a{};
    Vec4f attractor_b{};
    float delta_time = 0.f;
};

class ComputeShaderApp : public klvk::Application
{
    static constexpr uint32_t kParticleCount = 65'536;
    static constexpr uint32_t kWorkgroupSize = 64;

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Compute shader particles");

        klvk::DeviceContext& context = GetDeviceContext();
        const VkDevice device = context.GetDevice();
        const VkDescriptorSetLayoutBinding binding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
        };
        descriptor_set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &binding,
            });
        const VkDescriptorPoolSize pool_size{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = kFramesInFlight,
        };
        descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = kFramesInFlight,
                .poolSizeCount = 1,
                .pPoolSizes = &pool_size,
            });
        const std::array layouts{descriptor_set_layout_, descriptor_set_layout_};
        const auto sets = klvk::Vulkan::AllocateDescriptorSets(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool_,
                .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data(),
            });

        const VkDeviceSize buffer_size = sizeof(Particle) * kParticleCount;
        const std::vector particles = MakeParticles();
        for (size_t index = 0; index != kFramesInFlight; ++index)
        {
            descriptor_sets_[index] = sets[index];
            particle_buffers_[index] = klvk::GpuBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, buffer_size, true);
            particle_buffers_[index].Write(std::as_bytes(std::span{particles}));
            const VkDescriptorBufferInfo buffer_info{
                .buffer = particle_buffers_[index].GetHandle(),
                .range = buffer_size,
            };
            const VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_sets_[index],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffer_info,
            };
            klvk::Vulkan::UpdateDescriptorSets(device, std::span{&write, 1});
        }

        const VkPushConstantRange push_range{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(SimulationPushConstants),
        };
        pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &descriptor_set_layout_,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &push_range,
            });
        CreatePipelines(context);
    }

    static std::vector<Particle> MakeParticles()
    {
        std::vector<Particle> particles(kParticleCount);
        constexpr uint32_t side = 256;
        for (uint32_t index = 0; index != kParticleCount; ++index)
        {
            const float x = static_cast<float>(index % side) / static_cast<float>(side - 1) * 1.6f - 0.8f;
            const float y = static_cast<float>(index / side) / static_cast<float>(side - 1) * 1.6f - 0.8f;
            particles[index].position = {x, y, 0.f, 1.f};
            particles[index].velocity = {-y * 0.04f, x * 0.04f, 0.f, 0.f};
        }
        return particles;
    }

    VkShaderModule LoadShader(klvk::DeviceContext& context, const char* name)
    {
        std::string spirv;
        klvk::Filesystem::ReadFile(GetShaderDir() / "compute_shader" / name, spirv);
        return context.CreateShaderModule(spirv, name);
    }

    void CreatePipelines(klvk::DeviceContext& context)
    {
        const VkDevice device = context.GetDevice();
        const VkShaderModule compute_shader = LoadShader(context, "particles.comp.spv");
        const VkShaderModule vertex_shader = LoadShader(context, "particles.vert.spv");
        const VkShaderModule fragment_shader = LoadShader(context, "particles.frag.spv");
        auto destroy_shaders = klvk::OnScopeLeave(
            [&]
            {
                klvk::Vulkan::DestroyShaderModuleNE(device, compute_shader);
                klvk::Vulkan::DestroyShaderModuleNE(device, vertex_shader);
                klvk::Vulkan::DestroyShaderModuleNE(device, fragment_shader);
            });
        const VkComputePipelineCreateInfo compute_info{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage =
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                    .module = compute_shader,
                    .pName = "main",
                },
            .layout = pipeline_layout_,
        };
        compute_pipeline_ = klvk::Vulkan::CreateComputePipelines(device, {}, std::span{&compute_info, 1}).front();

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
            .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
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
        const VkPipelineColorBlendAttachmentState blend_attachment{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };
        const VkPipelineColorBlendStateCreateInfo blend{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &blend_attachment,
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
        const VkGraphicsPipelineCreateInfo graphics_info{
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
        graphics_pipeline_ = klvk::Vulkan::CreateGraphicsPipelines(device, {}, std::span{&graphics_info, 1}).front();
    }

    void Tick() override
    {
        klvk::Application::Tick();
        const size_t frame_index = GetFrameInFlightIndex();
        const VkDescriptorSet set = descriptor_sets_[frame_index];
        const VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        const float time = GetTimeSeconds();
        const SimulationPushConstants simulation{
            .attractor_a = {std::cos(time) * 0.35f, std::sin(time) * 0.35f, 0.f, 0.f},
            .attractor_b = {-std::cos(time * 0.7f) * 0.35f, -std::sin(time * 0.7f) * 0.35f, 0.f, 0.f},
            .delta_time = std::min(GetLastFrameDurationSeconds() * 2.f, 0.05f),
        };
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline_layout_,
            0,
            std::span{&set, 1});
        klvk::Vulkan::CmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, simulation);
        klvk::Vulkan::CmdDispatch(command_buffer, kParticleCount / kWorkgroupSize, 1, 1);

        const VkBufferMemoryBarrier2 buffer_barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = particle_buffers_[frame_index].GetHandle(),
            .size = VK_WHOLE_SIZE,
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &buffer_barrier,
        };
        klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);

        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0,
            std::span{&set, 1});
        klvk::Vulkan::CmdDraw(command_buffer, kParticleCount, 1, 0, 0);
    }

public:
    ~ComputeShaderApp() override
    {
        if (compute_pipeline_ == VK_NULL_HANDLE) return;
        klvk::DeviceContext& context = GetDeviceContext();
        context.WaitIdle();
        const VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroyPipelineNE(device, compute_pipeline_);
        klvk::Vulkan::DestroyPipelineNE(device, graphics_pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
        klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
        klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, descriptor_set_layout_);
    }

private:
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline compute_pipeline_ = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> descriptor_sets_{};
    std::array<klvk::GpuBuffer, kFramesInFlight> particle_buffers_{};
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
