#include "simple_cpu_renderer.hpp"

#include <vk_mem_alloc.h>

#include <EverydayTools/Math/Math.hpp>

#include "../fractal_settings.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

SimpleCpuRenderer::SimpleCpuRenderer(klvk::Application& app, size_t max_iterations_)
    : app_(&app),
      max_iterations(max_iterations_),
      fullscreen_shader_(app.GetDeviceContext(), "fractal_example/fullscreen"),
      textured_quad_shader_(app.GetDeviceContext(), "fractal_example/textured_quad")
{
    klvk::DeviceContext& context = app.GetDeviceContext();
    VkDevice device = context.GetDevice();

    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    sampler_ = klvk::Vulkan::CreateSampler(device, sampler_info);

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

    const VkDescriptorPoolSize pool_size{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1};
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

    pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
        device,
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &set_layout_,
        });
    auto stages = fullscreen_shader_.MakeShaderStages();
    const auto fragment_stages = textured_quad_shader_.MakeShaderStages();
    stages.insert(stages.end(), fragment_stages.begin(), fragment_stages.end());
    pipeline_ = CreateFullscreenPipeline(*app_, pipeline_layout_, stages);
}

SimpleCpuRenderer::~SimpleCpuRenderer() noexcept
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
    VkDevice device = context.GetDevice();
    DestroyImage();
    klvk::Vulkan::DestroyPipelineNE(device, pipeline_);
    klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
    klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
    klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, set_layout_);
    klvk::Vulkan::DestroySamplerNE(device, sampler_);
}

void SimpleCpuRenderer::DestroyImage()
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    if (image_view_) klvk::Vulkan::DestroyImageViewNE(context.GetDevice(), image_view_);
    if (image_) vmaDestroyImage(context.GetAllocator(), image_, image_allocation_);
    image_view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    image_allocation_ = VK_NULL_HANDLE;
    image_initialized_ = false;
}

void SimpleCpuRenderer::ApplySettings(const FractalSettings& settings)
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    VkDevice device = context.GetDevice();

    if (auto s = settings.viewport.size.Cast<size_t>(); !image_ || image_size_ != s)
    {
        context.WaitIdle();
        DestroyImage();
        image_size_ = s;

        const VkImageCreateInfo image_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {.width = static_cast<u32>(s.x()), .height = static_cast<u32>(s.y()), .depth = 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO};
        klvk::CheckVkResult(
            vmaCreateImage(context.GetAllocator(), &image_info, &allocation_info, &image_, &image_allocation_, nullptr),
            "vmaCreateImage");

        image_view_ = klvk::Vulkan::CreateImageView(
            device,
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image_,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
            });

        for (auto& buffer : staging_buffers_)
        {
            buffer =
                klvk::GpuBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, s.x() * s.y() * sizeof(edt::Vec4u8), true);
        }

        const VkDescriptorImageInfo descriptor_image_info{
            .sampler = sampler_,
            .imageView = image_view_,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set_,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &descriptor_image_info,
        };
        klvk::Vulkan::UpdateDescriptorSets(device, std::span{&write, 1});
    }

    pallette.resize(max_iterations + 1);
    settings.ComputeColors(pallette.size(), [&](size_t index, const edt::Vec3f& color) { pallette[index] = color; });
}

void SimpleCpuRenderer::PrepareFrame(VkCommandBuffer command_buffer, const FractalSettings& settings)
{
    if (!image_) return;

    render_transforms_.Update(settings.camera, settings.viewport);

    const auto [w, h] = image_size_.Tuple();
    image_buffer_.resize(w * h);

    for (size_t y = 0; y != h; ++y)
    {
        for (size_t x = 0; x != w; ++x)
        {
            auto& pixel = image_buffer_[y * w + x];
            edt::Vec2<size_t> frag_coord_u{x, y};
            auto frag_coord_f = frag_coord_u.Cast<float>();

            auto world = edt::Math::TransformPos(render_transforms_.screen_to_world, frag_coord_f);

            auto z = world;

            size_t i = 0;
            while (i != max_iterations)
            {
                auto p = edt::Math::ComplexPower(z, settings.fractal_power) + settings.fractal_constant;
                if (p.SquaredLength() > 4) break;
                z = p;
                ++i;
            }

            const edt::Vec3f color = pallette[i];
            pixel = edt::Vec4u8{
                static_cast<u8>(std::clamp(color.x(), 0.f, 1.f) * 255.f),
                static_cast<u8>(std::clamp(color.y(), 0.f, 1.f) * 255.f),
                static_cast<u8>(std::clamp(color.z(), 0.f, 1.f) * 255.f),
                255,
            };
        }
    }

    klvk::GpuBuffer& staging = staging_buffers_[app_->GetFrameInFlightIndex()];
    staging.Write(std::as_bytes(std::span{image_buffer_}));

    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = image_initialized_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);

    const VkBufferImageCopy region{
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {.width = static_cast<u32>(w), .height = static_cast<u32>(h), .depth = 1},
    };
    klvk::Vulkan::CmdCopyBufferToImage(
        command_buffer,
        staging.GetHandle(),
        image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        std::span{&region, 1});

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);

    image_initialized_ = true;
}

void SimpleCpuRenderer::Render(VkCommandBuffer command_buffer, const FractalSettings& settings)
{
    if (!image_initialized_) return;

    CmdSetGlStyleViewport(command_buffer, settings.viewport, app_->GetWindow().GetSize());
    klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    klvk::Vulkan::CmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_,
        0,
        std::span{&descriptor_set_, 1});
    klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
}
