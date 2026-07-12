#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/texture/procedural_texture_generator.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
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

            const std::filesystem::path shader_dir = GetShaderDir() / "textured_quad_2d";
            pipeline_ = klvk::GraphicsPipelineBuilder(*this)
                            .Layout(pipeline_layout_)
                            .VertexShaderFile(shader_dir / "textured_quad_2d.vert")
                            .FragmentShaderFile(shader_dir / "textured_quad_2d.frag")
                            .AlphaBlend()
                            .Build();
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
