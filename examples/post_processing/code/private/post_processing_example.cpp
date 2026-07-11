#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <imgui.h>
#include <vk_mem_alloc.h>

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

        const VkDescriptorSetLayoutBinding binding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
            device,
            {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding});
        const VkDescriptorPoolSize pool_size{
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = kFramesInFlight,
        };
        descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
            device,
            {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
             .maxSets = kFramesInFlight,
             .poolSizeCount = 1,
             .pPoolSizes = &pool_size});
        const std::array layouts{set_layout_, set_layout_};
        const auto sets = klvk::Vulkan::AllocateDescriptorSets(
            device,
            {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
             .descriptorPool = descriptor_pool_,
             .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
             .pSetLayouts = layouts.data()});
        for (size_t i = 0; i != sets_.size(); ++i) sets_[i] = sets[i];
        sampler_ = klvk::Vulkan::CreateSampler(
            device,
            {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
             .magFilter = VK_FILTER_LINEAR,
             .minFilter = VK_FILTER_LINEAR,
             .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
             .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
             .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
             .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE});

        const VkPushConstantRange scene_range{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .size = sizeof(PushConstants)};
        scene_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
             .pushConstantRangeCount = 1,
             .pPushConstantRanges = &scene_range});
        blur_layout_ = klvk::Vulkan::CreatePipelineLayout(
            device,
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
             .setLayoutCount = 1,
             .pSetLayouts = &set_layout_,
             .pushConstantRangeCount = 1,
             .pPushConstantRanges = &scene_range});
        scene_pipeline_ = CreatePipeline(context, "scene.frag.spv", scene_layout_, kTargetFormat);
        blur_pipeline_ = CreatePipeline(context, "blur.frag.spv", blur_layout_, GetSwapchainFormat());
    }

    VkPipeline
    CreatePipeline(klvk::DeviceContext& context, const char* fragment_name, VkPipelineLayout layout, VkFormat format)
    {
        auto load = [&](const char* name)
        {
            std::string data;
            klvk::Filesystem::ReadFile(GetShaderDir() / "post_processing" / name, data);
            return context.CreateShaderModule(data, name);
        };
        const VkDevice device = context.GetDevice();
        const VkShaderModule vs = load("fullscreen.vert.spv");
        const VkShaderModule fs = load(fragment_name);
        auto cleanup = klvk::OnScopeLeave(
            [&]
            {
                klvk::Vulkan::DestroyShaderModuleNE(device, vs);
                klvk::Vulkan::DestroyShaderModuleNE(device, fs);
            });
        const std::array stages{
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vs,
                .pName = "main"},
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fs,
                .pName = "main"},
        };
        const VkPipelineVertexInputStateCreateInfo vertex{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        const VkPipelineViewportStateCreateInfo viewport{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1};
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .lineWidth = 1.f};
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
        const VkPipelineColorBlendAttachmentState color_attachment{
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT};
        const VkPipelineColorBlendStateCreateInfo blend{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &color_attachment};
        const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
            .pDynamicStates = dynamic_states.data()};
        const VkPipelineRenderingCreateInfo rendering{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &format};
        const VkGraphicsPipelineCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering,
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertex,
            .pInputAssemblyState = &assembly,
            .pViewportState = &viewport,
            .pRasterizationState = &raster,
            .pMultisampleState = &multisample,
            .pColorBlendState = &blend,
            .pDynamicState = &dynamic,
            .layout = layout};
        return klvk::Vulkan::CreateGraphicsPipelines(device, {}, std::span{&info, 1}).front();
    }

    void EnsureTargets(edt::Vec2<uint32_t> size)
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
            .extent = {size.x(), size.y(), 1},
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
            const VkDescriptorImageInfo image{
                .sampler = sampler_,
                .imageView = target.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            const VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = sets_[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image};
            klvk::Vulkan::UpdateDescriptorSets(context.GetDevice(), std::span{&write, 1});
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
            .renderArea = {.extent = {size_.x(), size_.y()}},
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
        const VkRect2D scissor{.extent = {size_.x(), size_.y()}};
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
        const VkDescriptorSet set = sets_[GetFrameInFlightIndex()];
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
        if (!scene_pipeline_) return;
        auto& context = GetDeviceContext();
        context.WaitIdle();
        DestroyTargets();
        const VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroyPipelineNE(device, scene_pipeline_);
        klvk::Vulkan::DestroyPipelineNE(device, blur_pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, scene_layout_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, blur_layout_);
        klvk::Vulkan::DestroySamplerNE(device, sampler_);
        klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
        klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, set_layout_);
    }

private:
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipelineLayout scene_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout blur_layout_ = VK_NULL_HANDLE;
    VkPipeline scene_pipeline_ = VK_NULL_HANDLE;
    VkPipeline blur_pipeline_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> sets_{};
    std::array<Target, kFramesInFlight> targets_{};
    edt::Vec2<uint32_t> size_{};
    int radius_ = 4;
    float spread_ = 12.f;
    float mix_ = 1.f;
};

void Main()
{
    PostProcessingApp app;
    app.Run();
}
}  // namespace

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
