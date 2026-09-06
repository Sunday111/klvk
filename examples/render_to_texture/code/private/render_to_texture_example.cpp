#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vk_mem_alloc.h>

#include <edt/math/math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/window.hpp"

struct ColorPushConstants
{
    std::array<edt::Vec4f, 3> transform_columns{};
    edt::Vec4f color{};
};

class RenderToTextureApp : public klvk::Application
{
    static constexpr vk::Format kOffscreenFormat = vk::Format::eR8G8B8A8Unorm;

    struct OffscreenTarget
    {
        vk::Image image = nullptr;
        VmaAllocation allocation = nullptr;
        vk::UniqueImageView view;
    };

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Render to texture");

        klvk::DeviceContext& context = GetDeviceContext();
        vk::Device device = context.GetDevice();

        descriptor_sets_ =
            klvk::DescriptorSets::Builder(context)
                .Binding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
                .Build(kFramesInFlight);
        const vk::SamplerCreateInfo sampler_info = vk::SamplerCreateInfo{}
                                                       .setMagFilter(vk::Filter::eLinear)
                                                       .setMinFilter(vk::Filter::eLinear)
                                                       .setMipmapMode(vk::SamplerMipmapMode::eNearest)
                                                       .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                       .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                       .setAddressModeW(vk::SamplerAddressMode::eClampToEdge);
        sampler_ = device.createSamplerUnique(sampler_info);

        const vk::PushConstantRange push_constant_range =
            vk::PushConstantRange{}.setStageFlags(vk::ShaderStageFlagBits::eVertex).setSize(sizeof(ColorPushConstants));
        color_pipeline_layout_ = klvk::PipelineLayout{context, {}, std::span{&push_constant_range, 1}};
        const auto set_layout = descriptor_sets_.GetLayoutView();
        texture_pipeline_layout_ = klvk::PipelineLayout{context, std::span{&set_layout, 1}};
        color_pipeline_ =
            CreatePipeline(context, "color.vert.slang", "color.frag.slang", color_pipeline_layout_, kOffscreenFormat);
        texture_pipeline_ = CreatePipeline(
            context,
            "textured_quad.vert.slang",
            "textured_quad.frag.slang",
            texture_pipeline_layout_,
            GetSwapchainFormat());
    }

    [[nodiscard]] vk::UniquePipeline CreatePipeline(
        klvk::DeviceContext& context,
        const char* vertex_name,
        const char* fragment_name,
        const klvk::PipelineLayout& layout,
        vk::Format color_format)
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
        if (targets_.front().image != nullptr) context.WaitIdle();
        DestroyOffscreenTargets();
        target_size_ = size;

        const vk::ImageCreateInfo image_info =
            vk::ImageCreateInfo{}
                .setImageType(vk::ImageType::e2D)
                .setFormat(kOffscreenFormat)
                .setExtent(vk::Extent3D{size.x(), size.y(), 1})
                .setMipLevels(1)
                .setArrayLayers(1)
                .setSamples(vk::SampleCountFlagBits::e1)
                .setTiling(vk::ImageTiling::eOptimal)
                .setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
                .setSharingMode(vk::SharingMode::eExclusive)
                .setInitialLayout(vk::ImageLayout::eUndefined);
        const VkImageCreateInfo& raw_image_info = image_info;
        VmaAllocationCreateInfo allocation_info{};
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        for (size_t index = 0; index != targets_.size(); ++index)
        {
            OffscreenTarget& target = targets_[index];
            VkImage raw_image = nullptr;
            klvk::VulkanCheck(
                static_cast<vk::Result>(vmaCreateImage(
                    context.GetAllocator(),
                    &raw_image_info,
                    &allocation_info,
                    &raw_image,
                    &target.allocation,
                    nullptr)));
            target.image = raw_image;
            const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                    .setLevelCount(1)
                                                                    .setLayerCount(1);
            const vk::ImageViewCreateInfo view_info = vk::ImageViewCreateInfo{}
                                                          .setImage(target.image)
                                                          .setViewType(vk::ImageViewType::e2D)
                                                          .setFormat(kOffscreenFormat)
                                                          .setSubresourceRange(subresource_range);
            target.view = context.GetDevice().createImageViewUnique(view_info);

            descriptor_sets_.WriteImage(index, 0, target.view.get(), sampler_.get());
        }
    }

    void BeforeSwapchainRender(vk::CommandBuffer command_buffer) override
    {
        EnsureOffscreenTargets(GetWindow().GetFramebufferSize());
        const OffscreenTarget& target = targets_[GetFrameInFlightIndex()];

        const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                .setLevelCount(1)
                                                                .setLayerCount(1);
        vk::ImageMemoryBarrier2 barrier = vk::ImageMemoryBarrier2{}
                                              .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                                              .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                              .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                              .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                              .setOldLayout(vk::ImageLayout::eUndefined)
                                              .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                              .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                                              .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                                              .setImage(target.image)
                                              .setSubresourceRange(subresource_range);
        const vk::DependencyInfo dependency = vk::DependencyInfo{}.setImageMemoryBarriers(barrier);
        command_buffer.pipelineBarrier2(dependency);

        const vk::RenderingAttachmentInfo attachment = vk::RenderingAttachmentInfo{}
                                                           .setImageView(target.view.get())
                                                           .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                           .setLoadOp(vk::AttachmentLoadOp::eClear)
                                                           .setStoreOp(vk::AttachmentStoreOp::eStore);
        const vk::RenderingInfo rendering_info =
            vk::RenderingInfo{}
                .setRenderArea(vk::Rect2D{{}, {target_size_.x(), target_size_.y()}})
                .setLayerCount(1)
                .setColorAttachments(attachment);
        command_buffer.beginRendering(rendering_info);
        const vk::Viewport viewport = vk::Viewport{}
                                          .setY(static_cast<float>(target_size_.y()))
                                          .setWidth(static_cast<float>(target_size_.x()))
                                          .setHeight(-static_cast<float>(target_size_.y()))
                                          .setMinDepth(0.f)
                                          .setMaxDepth(1.f);
        const vk::Rect2D scissor = vk::Rect2D{}.setExtent(vk::Extent2D{target_size_.x(), target_size_.y()});
        command_buffer.setViewport(0, std::span{&viewport, 1});
        command_buffer.setScissor(0, std::span{&scissor, 1});

        edt::Mat3f transform = edt::Math::ScaleMatrix(edt::Vec2f{} + 0.4f);
        transform = edt::Math::RotationMatrix2d(GetTimeSeconds()).MatMul(transform);
        ColorPushConstants push_constants{
            .color = edt::Math::GetRainbowColorsA(GetTimeSeconds()).Cast<float>() / 255.f,
        };
        for (size_t column = 0; column != 3; ++column)
        {
            const edt::Vec3f value = transform.GetColumn(column);
            push_constants.transform_columns[column] = {value, 0.f};
        }
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, color_pipeline_.get());
        command_buffer.pushConstants(
            color_pipeline_layout_.GetHandle(),
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(push_constants),
            &push_constants);
        command_buffer.draw(6, 1, 0, 0);
        command_buffer.endRendering();

        barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
        barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        command_buffer.pipelineBarrier2(dependency);
    }

    void Tick() override
    {
        klvk::Application::Tick();
        vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        const vk::DescriptorSet descriptor_set = descriptor_sets_.Get(GetFrameInFlightIndex());
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, texture_pipeline_.get());
        command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            texture_pipeline_layout_.GetHandle(),
            0,
            std::span{&descriptor_set, 1},
            {});
        command_buffer.draw(6, 1, 0, 0);
    }

    void DestroyOffscreenTargets()
    {
        for (OffscreenTarget& target : targets_)
        {
            target.view.reset();
            if (target.image)
            {
                const VkImage raw_image = target.image;
                vmaDestroyImage(GetDeviceContext().GetAllocator(), raw_image, target.allocation);
                target.image = nullptr;
                target.allocation = nullptr;
            }
        }
    }

public:
    ~RenderToTextureApp() override
    {
        // The offscreen images are raw VMA allocations with no RAII wrapper; the
        // sampler, pipelines, layouts and descriptor sets are owning members
        // that clean up themselves. Application::Run has
        // already waited for the device to go idle.
        DestroyOffscreenTargets();
    }

private:
    klvk::DescriptorSets descriptor_sets_;
    vk::UniqueSampler sampler_;
    klvk::PipelineLayout color_pipeline_layout_;
    klvk::PipelineLayout texture_pipeline_layout_;
    vk::UniquePipeline color_pipeline_;
    vk::UniquePipeline texture_pipeline_;
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
