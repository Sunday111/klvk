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

// Matches the push constant block in two_textures_2d.vert.
struct PushConstants
{
    edt::Vec4f color{};
    edt::Vec2f scale{0.5f, 0.5f};
    edt::Vec2f translation{};
};

class TwoTexturesApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();

        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Two textures");

        GenerateTextures();
        PrepareDescriptors();
        PreparePipeline();
    }

    void GenerateTextures()
    {
        klvk::DeviceContext& context = GetDeviceContext();

        // Generate triangle mask texture and mirror it
        constexpr auto texture_size = edt::Vec2<size_t>{} + 128;
        auto pixels = klvk::ProceduralTextureGenerator::TriangleMask(texture_size, 2);
        right_triangle_texture_ = klvk::Texture::CreateR8(context, texture_size.Cast<uint32_t>(), std::span{pixels});

        klvk::ProceduralTextureGenerator::MirrorX(texture_size, pixels);
        left_triangle_texture_ = klvk::Texture::CreateR8(context, texture_size.Cast<uint32_t>(), std::span{pixels});

        // Generate circle mask texture
        pixels = klvk::ProceduralTextureGenerator::CircleMask(texture_size, 2);
        circle_texture_ = klvk::Texture::CreateR8(context, texture_size.Cast<uint32_t>(), std::span{pixels});
    }

    // One descriptor set per texture pair, both written once here:
    // the circle with the right triangle and the circle with the left one.
    void PrepareDescriptors()
    {
        VkDevice device = GetDeviceContext().GetDevice();

        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

        const VkDescriptorPoolSize pool_size{
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 4,
        };
        descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 2,
                .poolSizeCount = 1,
                .pPoolSizes = &pool_size,
            });

        const std::array<VkDescriptorSetLayout, 2> layouts{set_layout_, set_layout_};
        const std::vector<VkDescriptorSet> sets = klvk::Vulkan::AllocateDescriptorSets(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool_,
                .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data(),
            });
        right_set_ = sets[0];
        left_set_ = sets[1];

        auto make_image_info = [](const klvk::Texture& texture)
        {
            return VkDescriptorImageInfo{
                .sampler = texture.GetSampler(),
                .imageView = texture.GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
        };
        const std::array image_infos{
            make_image_info(*circle_texture_),
            make_image_info(*right_triangle_texture_),
            make_image_info(*left_triangle_texture_),
        };

        auto make_write = [&](VkDescriptorSet set, uint32_t binding, const VkDescriptorImageInfo& image_info)
        {
            return VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = set,
                .dstBinding = binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_info,
            };
        };
        const std::array writes{
            make_write(right_set_, 0, image_infos[0]),
            make_write(right_set_, 1, image_infos[1]),
            make_write(left_set_, 0, image_infos[0]),
            make_write(left_set_, 1, image_infos[2]),
        };
        klvk::Vulkan::UpdateDescriptorSets(device, writes);
    }

    void PreparePipeline()
    {
        klvk::DeviceContext& context = GetDeviceContext();
        VkDevice device = context.GetDevice();

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
            return context.CreateShaderModuleFromSource(GetShaderDir() / "two_textures_2d" / name);
        };

        VkShaderModule vertex_shader = load_shader("two_textures_2d.vert");
        VkShaderModule fragment_shader = load_shader("two_textures_2d.frag");
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
        pipeline_ = klvk::Vulkan::CreateGraphicsPipelines(device, VK_NULL_HANDLE, std::span{&pipeline_info, 1}).front();
    }

    void Tick() override
    {
        klvk::Application::Tick();

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        PushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };

        auto draw = [&](VkDescriptorSet set, const edt::Vec2f& translation)
        {
            push_constants.translation = translation;
            klvk::Vulkan::CmdBindDescriptorSets(
                command_buffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_layout_,
                0,
                std::span{&set, 1});
            klvk::Vulkan::CmdPushConstants(
                command_buffer,
                pipeline_layout_,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                push_constants);
            klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
        };

        draw(right_set_, {0.5f, 0.f});
        draw(left_set_, {-0.5f, 0.f});
    }

public:
    ~TwoTexturesApp() override
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
    std::unique_ptr<klvk::Texture> circle_texture_;
    std::unique_ptr<klvk::Texture> right_triangle_texture_;
    std::unique_ptr<klvk::Texture> left_triangle_texture_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet right_set_ = VK_NULL_HANDLE;
    VkDescriptorSet left_set_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

void Main()
{
    TwoTexturesApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
