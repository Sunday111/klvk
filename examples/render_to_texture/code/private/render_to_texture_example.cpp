#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vk_mem_alloc.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

struct ColorPushConstants
{
    std::array<edt::Vec4f, 3> transform_columns{};
    edt::Vec4f color{};
};

class RenderToTextureApp : public klvk::Application
{
    static constexpr VkFormat kOffscreenFormat = VK_FORMAT_R8G8B8A8_UNORM;

    struct OffscreenTarget
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Render to texture");

        klvk::DeviceContext& context = GetDeviceContext();
        VkDevice device = context.GetDevice();

        const VkDescriptorSetLayoutBinding binding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        descriptor_set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &binding,
            });
        const VkDescriptorPoolSize pool_size{
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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
        const auto descriptor_sets = klvk::Vulkan::AllocateDescriptorSets(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool_,
                .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data(),
            });
        for (size_t index = 0; index != descriptor_sets_.size(); ++index)
            descriptor_sets_[index] = descriptor_sets[index];
        sampler_ = klvk::Vulkan::CreateSampler(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            });

        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(ColorPushConstants),
        };
        color_pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &push_constant_range,
            });
        texture_pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &descriptor_set_layout_,
            });
        color_pipeline_ =
            CreatePipeline(context, "color.vert", "color.frag", color_pipeline_layout_, kOffscreenFormat);
        texture_pipeline_ = CreatePipeline(
            context,
            "textured_quad.vert",
            "textured_quad.frag",
            texture_pipeline_layout_,
            GetSwapchainFormat());
    }

    [[nodiscard]] VkPipeline CreatePipeline(
        klvk::DeviceContext& context,
        const char* vertex_name,
        const char* fragment_name,
        VkPipelineLayout layout,
        VkFormat color_format)
    {
        VkDevice device = context.GetDevice();
        auto load_shader = [&](const char* name)
        {
            return context.CreateShaderModuleFromSource(GetShaderDir() / "render_to_texture" / name);
        };
        VkShaderModule vertex_shader = load_shader(vertex_name);
        VkShaderModule fragment_shader = load_shader(fragment_name);
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
            .layout = layout,
        };
        return klvk::Vulkan::CreateGraphicsPipelines(device, VK_NULL_HANDLE, std::span{&pipeline_info, 1}).front();
    }

    void EnsureOffscreenTargets(edt::Vec2<uint32_t> size)
    {
        if (size == target_size_) return;

        klvk::DeviceContext& context = GetDeviceContext();
        if (targets_.front().image != VK_NULL_HANDLE) context.WaitIdle();
        DestroyOffscreenTargets();
        target_size_ = size;

        const VkImageCreateInfo image_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = kOffscreenFormat,
            .extent = {size.x(), size.y(), 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        for (size_t index = 0; index != targets_.size(); ++index)
        {
            OffscreenTarget& target = targets_[index];
            klvk::CheckVkResult(
                vmaCreateImage(
                    context.GetAllocator(),
                    &image_info,
                    &allocation_info,
                    &target.image,
                    &target.allocation,
                    nullptr),
                "vmaCreateImage(offscreen)");
            target.view = klvk::Vulkan::CreateImageView(
                context.GetDevice(),
                {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .image = target.image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = kOffscreenFormat,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
                });

            const VkDescriptorImageInfo image_descriptor{
                .sampler = sampler_,
                .imageView = target.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            const VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_sets_[index],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_descriptor,
            };
            klvk::Vulkan::UpdateDescriptorSets(context.GetDevice(), std::span{&write, 1});
        }
    }

    void BeforeSwapchainRender(VkCommandBuffer command_buffer) override
    {
        EnsureOffscreenTargets(GetWindow().GetFramebufferSize());
        const OffscreenTarget& target = targets_[GetFrameInFlightIndex()];

        VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = target.image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        };
        VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);

        const VkRenderingAttachmentInfo attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };
        const VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = {target_size_.x(), target_size_.y()}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment,
        };
        klvk::Vulkan::CmdBeginRendering(command_buffer, rendering_info);
        const VkViewport viewport{
            .y = static_cast<float>(target_size_.y()),
            .width = static_cast<float>(target_size_.x()),
            .height = -static_cast<float>(target_size_.y()),
            .minDepth = 0.f,
            .maxDepth = 1.f,
        };
        const VkRect2D scissor{.extent = {target_size_.x(), target_size_.y()}};
        klvk::Vulkan::CmdSetViewport(command_buffer, 0, std::span{&viewport, 1});
        klvk::Vulkan::CmdSetScissor(command_buffer, 0, std::span{&scissor, 1});

        edt::Mat3f transform = edt::Math::ScaleMatrix(edt::Vec2f{} + 0.4f);
        transform = edt::Math::RotationMatrix2d(GetTimeSeconds()).MatMul(transform);
        ColorPushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };
        for (size_t column = 0; column != 3; ++column)
        {
            const edt::Vec3f value = transform.GetColumn(column);
            push_constants.transform_columns[column] = {value.x(), value.y(), value.z(), 0.f};
        }
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, color_pipeline_);
        klvk::Vulkan::CmdPushConstants(
            command_buffer,
            color_pipeline_layout_,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            push_constants);
        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
        klvk::Vulkan::CmdEndRendering(command_buffer);

        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
    }

    void Tick() override
    {
        klvk::Application::Tick();
        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        const VkDescriptorSet descriptor_set = descriptor_sets_[GetFrameInFlightIndex()];
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            texture_pipeline_layout_,
            0,
            std::span{&descriptor_set, 1});
        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
    }

    void DestroyOffscreenTargets()
    {
        for (OffscreenTarget& target : targets_)
        {
            if (target.view)
            {
                klvk::Vulkan::DestroyImageViewNE(GetDeviceContext().GetDevice(), target.view);
                target.view = VK_NULL_HANDLE;
            }
            if (target.image)
            {
                vmaDestroyImage(GetDeviceContext().GetAllocator(), target.image, target.allocation);
                target.image = VK_NULL_HANDLE;
                target.allocation = VK_NULL_HANDLE;
            }
        }
    }

public:
    ~RenderToTextureApp() override
    {
        if (color_pipeline_ == VK_NULL_HANDLE) return;
        klvk::DeviceContext& context = GetDeviceContext();
        context.WaitIdle();
        DestroyOffscreenTargets();
        VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroySamplerNE(device, sampler_);
        klvk::Vulkan::DestroyPipelineNE(device, color_pipeline_);
        klvk::Vulkan::DestroyPipelineNE(device, texture_pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, color_pipeline_layout_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, texture_pipeline_layout_);
        klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
        klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, descriptor_set_layout_);
    }

private:
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> descriptor_sets_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipelineLayout color_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout texture_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline color_pipeline_ = VK_NULL_HANDLE;
    VkPipeline texture_pipeline_ = VK_NULL_HANDLE;
    std::array<OffscreenTarget, kFramesInFlight> targets_{};
    edt::Vec2<uint32_t> target_size_{};
};

void Main()
{
    RenderToTextureApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
