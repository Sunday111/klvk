#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"

#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"

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
        CheckVkResult(
            vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &set_layout_),
            "vkCreateDescriptorSetLayout");
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
        CheckVkResult(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool_), "vkCreateDescriptorPool");

        std::array<VkDescriptorSetLayout, Application::kFramesInFlight> layouts{};
        layouts.fill(set_layout_);
        const VkDescriptorSetAllocateInfo allocate_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptor_pool_,
            .descriptorSetCount = frames,
            .pSetLayouts = layouts.data(),
        };
        CheckVkResult(
            vkAllocateDescriptorSets(device, &allocate_info, descriptor_sets_.data()),
            "vkAllocateDescriptorSets");
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
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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
        CheckVkResult(
            vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_),
            "vkCreatePipelineLayout");
    }

    {
        auto load_shader = [&](const char* name)
        {
            std::string spirv;
            Filesystem::ReadFile(app.GetShaderDir() / name, spirv);
            return context.CreateShaderModule(spirv, name);
        };

        VkShaderModule vertex_shader = load_shader("klvk/instanced_sprite.vert.spv");
        VkShaderModule fragment_shader = load_shader("klvk/instanced_sprite.frag.spv");
        auto destroy_modules = OnScopeLeave(
            [&]
            {
                vkDestroyShaderModule(device, vertex_shader, nullptr);
                vkDestroyShaderModule(device, fragment_shader, nullptr);
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
        CheckVkResult(
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_),
            "vkCreateGraphicsPipelines");
    }
}

InstancedSpriteRenderer2d::~InstancedSpriteRenderer2d()
{
    DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
    VkDevice device = context.GetDevice();
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (descriptor_pool_) vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
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
    vkUpdateDescriptorSets(app_->GetDeviceContext().GetDevice(), 1, &write, 0, nullptr);
}

void InstancedSpriteRenderer2d::Render(const Mat3f& world_to_view)
{
    if (instances_.empty()) return;

    const size_t frame_index = app_->GetFrameInFlightIndex();
    VkCommandBuffer command_buffer = app_->GetCurrentCommandBuffer();

    EnsureFrameBufferCapacity(frame_index, instances_.size() * sizeof(Instance));
    instance_buffers_[frame_index].Write(std::as_bytes(std::span{instances_}));

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_,
        0,
        1,
        &descriptor_sets_[frame_index],
        0,
        nullptr);

    // The shader constructs the mat3 from columns.
    PushConstants push_constants{};
    for (size_t column = 0; column != 3; ++column)
    {
        const Vec3f matrix_column = world_to_view.GetColumn(column);
        push_constants.columns[column] = Vec4f{matrix_column.x(), matrix_column.y(), matrix_column.z(), 0.f};
    }
    vkCmdPushConstants(
        command_buffer,
        pipeline_layout_,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(PushConstants),
        &push_constants);

    vkCmdDraw(command_buffer, 6, static_cast<uint32_t>(instances_.size()), 0, 0);
}

}  // namespace klvk
