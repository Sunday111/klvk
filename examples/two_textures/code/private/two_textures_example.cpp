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

        const std::filesystem::path shader_dir = GetShaderDir() / "two_textures_2d";
        pipeline_ = klvk::GraphicsPipelineBuilder(*this)
                        .Layout(pipeline_layout_)
                        .VertexShaderFile(shader_dir / "two_textures_2d.vert")
                        .FragmentShaderFile(shader_dir / "two_textures_2d.frag")
                        .AlphaBlend()
                        .Build();
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
