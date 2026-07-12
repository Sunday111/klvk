#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/texture/procedural_texture_generator.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

// Matches the push constant block in textured_quad_2d.vert.
struct PushConstants
{
    edt::Vec4f color{1.f, 0.f, 0.f, 1.f};
    edt::Vec2f scale{0.5f, 0.5f};
    edt::Vec2f translation{0.f, 0.f};
};

class TexturedQuadApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();

        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Textured Quad");

        klvk::DeviceContext& context = GetDeviceContext();
        VkDevice device = context.GetDevice();

        // Generate circle mask texture
        {
            constexpr auto size = edt::Vec2<size_t>{} + 128;
            const auto pixels = klvk::ProceduralTextureGenerator::CircleMask(size, 2);
            texture_ = klvk::Texture::CreateR8(context, size.Cast<uint32_t>(), std::span{pixels});
        }

        // Descriptor set that binds the texture to the fragment shader
        {
            const VkDescriptorSetLayoutBinding binding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            };
            set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = 1,
                    .pBindings = &binding,
                });

            const VkDescriptorPoolSize pool_size{
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
            };
            descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                    .maxSets = 1,
                    .poolSizeCount = 1,
                    .pPoolSizes = &pool_size,
                });

            descriptor_set_ = klvk::Vulkan::AllocateDescriptorSets(
                                  device,
                                  {
                                      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                      .descriptorPool = descriptor_pool_,
                                      .descriptorSetCount = 1,
                                      .pSetLayouts = &set_layout_,
                                  })
                                  .front();

            const VkDescriptorImageInfo image_info{
                .sampler = texture_->GetSampler(),
                .imageView = texture_->GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            const VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set_,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_info,
            };
            klvk::Vulkan::UpdateDescriptorSets(device, std::span{&write, 1});
        }

        // Pipeline
        {
            const VkPushConstantRange push_constant_range{
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(PushConstants),
            };
            pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    .setLayoutCount = 1,
                    .pSetLayouts = &set_layout_,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &push_constant_range,
                });

            auto load_shader = [&](const char* name)
            {
                return context.CreateShaderModuleFromSource(GetShaderDir() / "textured_quad_2d" / name);
            };

            VkShaderModule vertex_shader = load_shader("textured_quad_2d.vert");
            VkShaderModule fragment_shader = load_shader("textured_quad_2d.frag");
            auto destroy_modules = klvk::OnScopeLeave(
                [&]
                {
                    klvk::Vulkan::DestroyShaderModuleNE(device, vertex_shader);
                    klvk::Vulkan::DestroyShaderModuleNE(device, fragment_shader);
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

            const VkFormat color_format = GetSwapchainFormat();
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
            pipeline_ =
                klvk::Vulkan::CreateGraphicsPipelines(device, VK_NULL_HANDLE, std::span{&pipeline_info, 1}).front();
        }
    }

    void Tick() override
    {
        klvk::Application::Tick();

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0,
            std::span{&descriptor_set_, 1});

        const PushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };
        klvk::Vulkan::CmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, push_constants);

        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
    }

public:
    ~TexturedQuadApp() override
    {
        // Initialize either succeeded entirely or threw before creating the pipeline.
        if (pipeline_ == VK_NULL_HANDLE) return;

        klvk::DeviceContext& context = GetDeviceContext();
        context.WaitIdle();
        VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroyPipelineNE(device, pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
        klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
        klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, set_layout_);
    }

private:
    std::unique_ptr<klvk::Texture> texture_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

void Main()
{
    TexturedQuadApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
