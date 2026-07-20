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
#include "klvk/vulkan/vk_object.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace
{

struct Target
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct PushConstants
{
    std::array<float, 4> data{};
};

class PostProcessingApp : public klvk::Application
{
    static constexpr VkFormat kTargetFormat = VK_FORMAT_R8G8B8A8_UNORM;

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Post-processing effect");
        auto& context = GetDeviceContext();
        const VkDevice device = context.GetDevice();

        descriptor_sets_ = klvk::DescriptorSets::Builder(context)
                               .Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .Build(kFramesInFlight);
        sampler_ = klvk::VkObject<VkSampler>{
            device,
            klvk::Vulkan::CreateSampler(
                device,
                {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                 .magFilter = VK_FILTER_LINEAR,
                 .minFilter = VK_FILTER_LINEAR,
                 .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                 .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                 .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                 .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE})};

        const VkPushConstantRange scene_range{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .size = sizeof(PushConstants)};
        scene_layout_ = klvk::VkObject<VkPipelineLayout>{
            device,
            klvk::Vulkan::CreatePipelineLayout(
                device,
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                 .pushConstantRangeCount = 1,
                 .pPushConstantRanges = &scene_range})};
        const VkDescriptorSetLayout set_layout = descriptor_sets_.GetLayout();
        blur_layout_ = klvk::VkObject<VkPipelineLayout>{
            device,
            klvk::Vulkan::CreatePipelineLayout(
                device,
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                 .setLayoutCount = 1,
                 .pSetLayouts = &set_layout,
                 .pushConstantRangeCount = 1,
                 .pPushConstantRanges = &scene_range})};
        scene_pipeline_ =
            klvk::VkObject<VkPipeline>{device, CreatePipeline(context, "scene.frag", scene_layout_, kTargetFormat)};
        blur_pipeline_ = klvk::VkObject<VkPipeline>{
            device,
            CreatePipeline(context, "blur.frag", blur_layout_, GetSwapchainFormat())};
    }

    VkPipeline
    CreatePipeline(klvk::DeviceContext& context, const char* fragment_name, VkPipelineLayout layout, VkFormat format)
    {
        const std::filesystem::path shader_dir = GetShaderDir() / "post_processing";
        return klvk::GraphicsPipelineBuilder(context)
            .Layout(layout)
            .VertexShaderFile(shader_dir / "fullscreen.vert")
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
        const VkImageCreateInfo image_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = kTargetFormat,
            .extent = {.width = size.x(), .height = size.y(), .depth = 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        for (size_t i = 0; i != targets_.size(); ++i)
        {
            auto& target = targets_[i];
            klvk::CheckVkResult(
                vmaCreateImage(
                    context.GetAllocator(),
                    &image_info,
                    &allocation_info,
                    &target.image,
                    &target.allocation,
                    nullptr),
                "vmaCreateImage(post-processing)");
            target.view = klvk::Vulkan::CreateImageView(
                context.GetDevice(),
                {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                 .image = target.image,
                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .format = kTargetFormat,
                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}});
            descriptor_sets_.WriteImage(i, 0, target.view, sampler_);
        }
    }

    void BeforeSwapchainRender(VkCommandBuffer command_buffer) override
    {
        EnsureTargets(GetWindow().GetFramebufferSize());
        const Target& target = targets_[GetFrameInFlightIndex()];
        VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = target.image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
        VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier};
        klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
        const VkRenderingAttachmentInfo attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        const VkRenderingInfo rendering{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = {.width = size_.x(), .height = size_.y()}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment};
        klvk::Vulkan::CmdBeginRendering(command_buffer, rendering);
        SetViewport(command_buffer);
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_pipeline_);
        const PushConstants constants{.data = {GetTimeSeconds(), 0.f, 0.f, 0.f}};
        klvk::Vulkan::CmdPushConstants(command_buffer, scene_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, constants);
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

    void SetViewport(VkCommandBuffer command_buffer)
    {
        const VkViewport viewport{
            .y = static_cast<float>(size_.y()),
            .width = static_cast<float>(size_.x()),
            .height = -static_cast<float>(size_.y()),
            .minDepth = 0.f,
            .maxDepth = 1.f};
        const VkRect2D scissor{.extent = {.width = size_.x(), .height = size_.y()}};
        klvk::Vulkan::CmdSetViewport(command_buffer, 0, std::span{&viewport, 1});
        klvk::Vulkan::CmdSetScissor(command_buffer, 0, std::span{&scissor, 1});
    }

    void Tick() override
    {
        klvk::Application::Tick();
        ImGui::SliderInt("Blur radius", &radius_, 0, 8);
        ImGui::SliderFloat("Blur spread", &spread_, 0.5f, 30.f);
        ImGui::SliderFloat("Blur mix", &mix_, 0.f, 1.f);
        const VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        const VkDescriptorSet set = descriptor_sets_.Get(GetFrameInFlightIndex());
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blur_pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            blur_layout_,
            0,
            std::span{&set, 1});
        const PushConstants constants{.data = {static_cast<float>(radius_), spread_, mix_, 0.f}};
        klvk::Vulkan::CmdPushConstants(command_buffer, blur_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, constants);
        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
    }

    void DestroyTargets()
    {
        for (auto& target : targets_)
        {
            if (target.view) klvk::Vulkan::DestroyImageViewNE(GetDeviceContext().GetDevice(), target.view);
            if (target.image) vmaDestroyImage(GetDeviceContext().GetAllocator(), target.image, target.allocation);
            target = {};
        }
    }

public:
    ~PostProcessingApp() override
    {
        // The offscreen targets are raw VMA allocations with no RAII wrapper; the
        // sampler, pipelines, layouts and descriptor sets are VkObject /
        // DescriptorSets members that clean up themselves. Application::Run has
        // already waited for the device to go idle.
        DestroyTargets();
    }

private:
    klvk::DescriptorSets descriptor_sets_;
    klvk::VkObject<VkSampler> sampler_;
    klvk::VkObject<VkPipelineLayout> scene_layout_;
    klvk::VkObject<VkPipelineLayout> blur_layout_;
    klvk::VkObject<VkPipeline> scene_pipeline_;
    klvk::VkObject<VkPipeline> blur_pipeline_;
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
