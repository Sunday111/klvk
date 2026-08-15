#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <imgui.h>
#include <vk_mem_alloc.h>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/vulkan/vulkan_object.hpp"
#include "klvk/window.hpp"

namespace
{

struct Target
{
    vk::Image image = nullptr;
    VmaAllocation allocation = nullptr;
    vk::ImageView view = nullptr;
};

struct PushConstants
{
    std::array<float, 4> data{};
};

class PostProcessingApp : public klvk::Application
{
    static constexpr vk::Format kTargetFormat = vk::Format::eR8G8B8A8Unorm;

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Post-processing effect");
        auto& context = GetDeviceContext();
        const vk::Device device = context.GetDevice();

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
        sampler_ = klvk::VulkanObject<vk::Sampler>{
            device,
            klvk::VulkanValue(device.createSampler(sampler_info), "vkCreateSampler")};

        const vk::PushConstantRange scene_range =
            vk::PushConstantRange{}.setStageFlags(vk::ShaderStageFlagBits::eFragment).setSize(sizeof(PushConstants));
        scene_layout_ = klvk::PipelineLayout{context, {}, std::span{&scene_range, 1}};
        const auto set_layout = descriptor_sets_.GetLayoutView();
        blur_layout_ = klvk::PipelineLayout{context, std::span{&set_layout, 1}, std::span{&scene_range, 1}};
        scene_pipeline_ = klvk::VulkanObject<vk::Pipeline>{
            device,
            CreatePipeline(context, "scene.frag.slang", scene_layout_, kTargetFormat)};
        blur_pipeline_ = klvk::VulkanObject<vk::Pipeline>{
            device,
            CreatePipeline(context, "blur.frag.slang", blur_layout_, GetSwapchainFormat())};
    }

    vk::Pipeline CreatePipeline(
        klvk::DeviceContext& context,
        const char* fragment_name,
        const klvk::PipelineLayout& layout,
        vk::Format format)
    {
        const std::filesystem::path shader_dir = GetShaderDir() / "post_processing";
        return klvk::GraphicsPipelineBuilder(context)
            .Layout(layout)
            .VertexShaderFile(shader_dir / "fullscreen.vert.slang")
            .FragmentShaderFile(shader_dir / fragment_name)
            .ColorFormat(format)
            .Build();
    }

    void EnsureTargets(edt::Vec2<u32> size)
    {
        if (size == size_) return;
        auto& context = GetDeviceContext();
        if (targets_[0].image) context.WaitIdle();
        DestroyTargets();
        size_ = size;
        const vk::ImageCreateInfo image_info =
            vk::ImageCreateInfo{}
                .setImageType(vk::ImageType::e2D)
                .setFormat(kTargetFormat)
                .setExtent(vk::Extent3D{size.x(), size.y(), 1})
                .setMipLevels(1)
                .setArrayLayers(1)
                .setSamples(vk::SampleCountFlagBits::e1)
                .setTiling(vk::ImageTiling::eOptimal)
                .setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
                .setSharingMode(vk::SharingMode::eExclusive)
                .setInitialLayout(vk::ImageLayout::eUndefined);
        const VkImageCreateInfo& raw_image_info = image_info;
        const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        for (size_t i = 0; i != targets_.size(); ++i)
        {
            auto& target = targets_[i];
            VkImage raw_image = nullptr;
            klvk::VulkanCheck(
                static_cast<vk::Result>(vmaCreateImage(
                    context.GetAllocator(),
                    &raw_image_info,
                    &allocation_info,
                    &raw_image,
                    &target.allocation,
                    nullptr)),
                "vmaCreateImage(post-processing)");
            target.image = raw_image;
            const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                    .setLevelCount(1)
                                                                    .setLayerCount(1);
            const vk::ImageViewCreateInfo view_info = vk::ImageViewCreateInfo{}
                                                          .setImage(target.image)
                                                          .setViewType(vk::ImageViewType::e2D)
                                                          .setFormat(kTargetFormat)
                                                          .setSubresourceRange(subresource_range);
            target.view = klvk::VulkanValue(context.GetDevice().createImageView(view_info), "vkCreateImageView");
            descriptor_sets_.WriteImage(i, 0, target.view, sampler_);
        }
    }

    void BeforeSwapchainRender(vk::CommandBuffer command_buffer) override
    {
        EnsureTargets(GetWindow().GetFramebufferSize());
        const Target& target = targets_[GetFrameInFlightIndex()];
        const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                .setLevelCount(1)
                                                                .setLayerCount(1);
        vk::ImageMemoryBarrier2 barrier = vk::ImageMemoryBarrier2{}
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
                                                           .setImageView(target.view)
                                                           .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                           .setLoadOp(vk::AttachmentLoadOp::eClear)
                                                           .setStoreOp(vk::AttachmentStoreOp::eStore);
        const vk::RenderingInfo rendering = vk::RenderingInfo{}
                                                .setRenderArea(vk::Rect2D{{}, {size_.x(), size_.y()}})
                                                .setLayerCount(1)
                                                .setColorAttachments(attachment);
        command_buffer.beginRendering(rendering);
        SetViewport(command_buffer);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, scene_pipeline_);
        const PushConstants constants{.data = {GetTimeSeconds(), 0.f, 0.f, 0.f}};
        command_buffer.pushConstants(
            scene_layout_.GetHandle(),
            vk::ShaderStageFlagBits::eFragment,
            0,
            sizeof(constants),
            &constants);
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

    void SetViewport(vk::CommandBuffer command_buffer)
    {
        const vk::Viewport viewport = vk::Viewport{}
                                          .setY(static_cast<float>(size_.y()))
                                          .setWidth(static_cast<float>(size_.x()))
                                          .setHeight(-static_cast<float>(size_.y()))
                                          .setMinDepth(0.f)
                                          .setMaxDepth(1.f);
        const vk::Rect2D scissor = vk::Rect2D{}.setExtent(vk::Extent2D{size_.x(), size_.y()});
        command_buffer.setViewport(0, std::span{&viewport, 1});
        command_buffer.setScissor(0, std::span{&scissor, 1});
    }

    void Tick() override
    {
        klvk::Application::Tick();
        ImGui::SliderInt("Blur radius", &radius_, 0, 8);
        ImGui::SliderFloat("Blur spread", &spread_, 0.5f, 30.f);
        ImGui::SliderFloat("Blur mix", &mix_, 0.f, 1.f);
        const vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        const vk::DescriptorSet set = descriptor_sets_.Get(GetFrameInFlightIndex());
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, blur_pipeline_);
        command_buffer
            .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, blur_layout_.GetHandle(), 0, std::span{&set, 1}, {});
        const PushConstants constants{.data = {static_cast<float>(radius_), spread_, mix_, 0.f}};
        command_buffer.pushConstants(
            blur_layout_.GetHandle(),
            vk::ShaderStageFlagBits::eFragment,
            0,
            sizeof(constants),
            &constants);
        command_buffer.draw(6, 1, 0, 0);
    }

    void DestroyTargets()
    {
        for (auto& target : targets_)
        {
            if (target.view) GetDeviceContext().GetDevice().destroyImageView(target.view);
            if (target.image)
            {
                const VkImage raw_image = target.image;
                vmaDestroyImage(GetDeviceContext().GetAllocator(), raw_image, target.allocation);
            }
            target = {};
        }
    }

public:
    ~PostProcessingApp() override
    {
        // The offscreen targets are raw VMA allocations with no RAII wrapper; the
        // sampler, pipelines, layouts and descriptor sets are VulkanObject and
        // DescriptorSets members that clean up themselves. Application::Run has
        // already waited for the device to go idle.
        DestroyTargets();
    }

private:
    klvk::DescriptorSets descriptor_sets_;
    klvk::VulkanObject<vk::Sampler> sampler_;
    klvk::PipelineLayout scene_layout_;
    klvk::PipelineLayout blur_layout_;
    klvk::VulkanObject<vk::Pipeline> scene_pipeline_;
    klvk::VulkanObject<vk::Pipeline> blur_pipeline_;
    std::array<Target, kFramesInFlight> targets_{};
    edt::Vec2<u32> size_{};
    int radius_ = 4;
    float spread_ = 12.f;
    float mix_ = 1.f;
};

void Main(int argc, char** argv)
{
    PostProcessingApp app;
    app.RunWithArguments(argc, argv);
}
}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
