#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vk_mem_alloc.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vk_object.hpp"
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

        descriptor_sets_ = klvk::DescriptorSets::Builder(context)
                               .Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .Build(kFramesInFlight);
        sampler_ = klvk::VkObject<VkSampler>{
            device,
            klvk::Vulkan::CreateSampler(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                    .magFilter = VK_FILTER_LINEAR,
                    .minFilter = VK_FILTER_LINEAR,
                    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                })};

        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(ColorPushConstants),
        };
        color_pipeline_layout_ = klvk::VkObject<VkPipelineLayout>{
            device,
            klvk::Vulkan::CreatePipelineLayout(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &push_constant_range,
                })};
        const VkDescriptorSetLayout set_layout = descriptor_sets_.GetLayout();
        texture_pipeline_layout_ = klvk::VkObject<VkPipelineLayout>{
            device,
            klvk::Vulkan::CreatePipelineLayout(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    .setLayoutCount = 1,
                    .pSetLayouts = &set_layout,
                })};
        color_pipeline_ = klvk::VkObject<VkPipeline>{
            device,
            CreatePipeline(context, "color.vert", "color.frag", color_pipeline_layout_, kOffscreenFormat)};
        texture_pipeline_ = klvk::VkObject<VkPipeline>{
            device,
            CreatePipeline(
                context,
                "textured_quad.vert",
                "textured_quad.frag",
                texture_pipeline_layout_,
                GetSwapchainFormat())};
    }

    [[nodiscard]] VkPipeline CreatePipeline(
        klvk::DeviceContext& context,
        const char* vertex_name,
        const char* fragment_name,
        VkPipelineLayout layout,
        VkFormat color_format)
    {
        const std::filesystem::path shader_dir = GetShaderDir() / "render_to_texture";
        return klvk::GraphicsPipelineBuilder(context)
            .Layout(layout)
            .VertexShaderFile(shader_dir / vertex_name)
            .FragmentShaderFile(shader_dir / fragment_name)
            .ColorFormat(color_format)
            .Build();
    }

    void EnsureOffscreenTargets(edt::Vec2<u32> size)
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
            .extent = {.width = size.x(), .height = size.y(), .depth = 1},
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

            descriptor_sets_.WriteImage(index, 0, target.view, sampler_);
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
            .renderArea = {.extent = {.width = target_size_.x(), .height = target_size_.y()}},
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
        const VkRect2D scissor{.extent = {.width = target_size_.x(), .height = target_size_.y()}};
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
        const VkDescriptorSet descriptor_set = descriptor_sets_.Get(GetFrameInFlightIndex());
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
        // The offscreen images are raw VMA allocations with no RAII wrapper; the
        // sampler, pipelines, layouts and descriptor sets are VkObject /
        // DescriptorSets members that clean up themselves. Application::Run has
        // already waited for the device to go idle.
        DestroyOffscreenTargets();
    }

private:
    klvk::DescriptorSets descriptor_sets_;
    klvk::VkObject<VkSampler> sampler_;
    klvk::VkObject<VkPipelineLayout> color_pipeline_layout_;
    klvk::VkObject<VkPipelineLayout> texture_pipeline_layout_;
    klvk::VkObject<VkPipeline> color_pipeline_;
    klvk::VkObject<VkPipeline> texture_pipeline_;
    std::array<OffscreenTarget, kFramesInFlight> targets_{};
    edt::Vec2<u32> target_size_{};
};

void Main(int argc, char** argv)
{
    RenderToTextureApp app;
    app.RunWithArguments(argc, argv);
}

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
