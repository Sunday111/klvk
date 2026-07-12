#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"

#include <algorithm>

#include "klvk/filesystem/filesystem.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

namespace
{

// The world-to-view matrix as three vec4 columns, matching the push constant block layout.
struct PushConstants
{
    std::array<edt::Vec4f, 3> columns;
};

}  // namespace

InstancedSpriteRenderer2d::InstancedSpriteRenderer2d(Application& app, const Texture& texture) : app_(&app)
{
    DeviceContext& context = app.GetDeviceContext();
    VkDevice device = context.GetDevice();

    {
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            },
        };
        const VkDescriptorSetLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        set_layout_ = Vulkan::CreateDescriptorSetLayout(device, layout_info);
    }

    {
        constexpr auto frames = static_cast<uint32_t>(Application::kFramesInFlight);
        const std::array<VkDescriptorPoolSize, 2> pool_sizes{
            VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = frames},
            VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = frames},
        };
        const VkDescriptorPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = frames,
            .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data(),
        };
        descriptor_pool_ = Vulkan::CreateDescriptorPool(device, pool_info);

        std::array<VkDescriptorSetLayout, Application::kFramesInFlight> layouts{};
        layouts.fill(set_layout_);
        const VkDescriptorSetAllocateInfo allocate_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptor_pool_,
            .descriptorSetCount = frames,
            .pSetLayouts = layouts.data(),
        };
        const std::vector<VkDescriptorSet> allocated_sets = Vulkan::AllocateDescriptorSets(device, allocate_info);
        std::ranges::copy(allocated_sets, descriptor_sets_.begin());
    }

    for (VkDescriptorSet set : descriptor_sets_)
    {
        const VkDescriptorImageInfo image_info{
            .sampler = texture.GetSampler(),
            .imageView = texture.GetView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
        };
        Vulkan::UpdateDescriptorSetsNE(device, std::span{&write, 1});
    }

    {
        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(PushConstants),
        };
        const VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &set_layout_,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range,
        };
        pipeline_layout_ = Vulkan::CreatePipelineLayout(device, layout_info);
    }

    {
        auto load_shader = [&](const char* name)
        {
            return context.CreateShaderModuleFromSource(app.GetShaderDir() / name);
        };

        VkShaderModule vertex_shader = load_shader("klvk/instanced_sprite.vert");
        VkShaderModule fragment_shader = load_shader("klvk/instanced_sprite.frag");
        auto destroy_modules = OnScopeLeave(
            [&]
            {
                Vulkan::DestroyShaderModuleNE(device, vertex_shader);
                Vulkan::DestroyShaderModuleNE(device, fragment_shader);
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
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
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

        const VkFormat color_format = app.GetSwapchainFormat();
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
        const std::vector<VkPipeline> pipelines =
            Vulkan::CreateGraphicsPipelines(device, VK_NULL_HANDLE, std::span{&pipeline_info, 1});
        pipeline_ = pipelines.front();
    }
}

InstancedSpriteRenderer2d::~InstancedSpriteRenderer2d()
{
    DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
    VkDevice device = context.GetDevice();
    if (pipeline_) Vulkan::DestroyPipelineNE(device, pipeline_);
    if (pipeline_layout_) Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
    if (descriptor_pool_) Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
    if (set_layout_) Vulkan::DestroyDescriptorSetLayoutNE(device, set_layout_);
}

void InstancedSpriteRenderer2d::EnsureFrameBufferCapacity(size_t frame_index, size_t bytes)
{
    GpuBuffer& buffer = instance_buffers_[frame_index];
    if (buffer.IsValid() && buffer.GetSize() >= bytes) return;

    size_t new_size = 1024;
    while (new_size < bytes) new_size *= 2;

    // The application waited on this frame slot's fence in PreTick, so the GPU
    // is done with the old buffer and it can be destroyed right away.
    buffer = GpuBuffer(app_->GetDeviceContext(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, new_size, true);

    const VkDescriptorBufferInfo buffer_info{
        .buffer = buffer.GetHandle(),
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_sets_[frame_index],
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &buffer_info,
    };
    Vulkan::UpdateDescriptorSetsNE(app_->GetDeviceContext().GetDevice(), std::span{&write, 1});
}

void InstancedSpriteRenderer2d::Render(const Mat3f& world_to_view)
{
    if (instances_.empty()) return;

    const size_t frame_index = app_->GetFrameInFlightIndex();
    VkCommandBuffer command_buffer = app_->GetCurrentCommandBuffer();

    EnsureFrameBufferCapacity(frame_index, instances_.size() * sizeof(Instance));
    instance_buffers_[frame_index].Write(std::as_bytes(std::span{instances_}));

    Vulkan::CmdBindPipelineNE(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    Vulkan::CmdBindDescriptorSetsNE(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_,
        0,
        std::span{&descriptor_sets_[frame_index], 1});

    // The shader constructs the mat3 from columns.
    PushConstants push_constants{};
    for (size_t column = 0; column != 3; ++column)
    {
        const Vec3f matrix_column = world_to_view.GetColumn(column);
        push_constants.columns[column] = Vec4f{matrix_column.x(), matrix_column.y(), matrix_column.z(), 0.f};
    }
    Vulkan::CmdPushConstantsNE(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, push_constants);

    Vulkan::CmdDrawNE(command_buffer, 6, static_cast<uint32_t>(instances_.size()), 0, 0);
}

}  // namespace klvk
