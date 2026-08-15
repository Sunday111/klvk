#include "simple_cpu_renderer.hpp"

#include <vk_mem_alloc.h>

#include <edt/math/math.hpp>

#include "../fractal_settings.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"

SimpleCpuRenderer::SimpleCpuRenderer(klvk::Application& app, size_t max_iterations_)
    : app_(&app),
      max_iterations(max_iterations_),
      fullscreen_shader_(app.GetDeviceContext(), "fractal_example/fullscreen"),
      textured_quad_shader_(app.GetDeviceContext(), "fractal_example/textured_quad")
{
    klvk::DeviceContext& context = app.GetDeviceContext();
    vk::Device device = context.GetDevice();

    const vk::SamplerCreateInfo sampler_info = vk::SamplerCreateInfo{}
                                                   .setMagFilter(vk::Filter::eNearest)
                                                   .setMinFilter(vk::Filter::eNearest)
                                                   .setMipmapMode(vk::SamplerMipmapMode::eNearest)
                                                   .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                   .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                   .setAddressModeW(vk::SamplerAddressMode::eClampToEdge);
    sampler_ = klvk::VulkanValue(device.createSampler(sampler_info), "vkCreateSampler");

    const vk::DescriptorSetLayoutBinding binding = vk::DescriptorSetLayoutBinding{}
                                                       .setBinding(0)
                                                       .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                                                       .setDescriptorCount(1)
                                                       .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    set_layout_description_.bindings = {binding};
    const vk::DescriptorSetLayoutCreateInfo layout_info = vk::DescriptorSetLayoutCreateInfo{}.setBindings(binding);
    set_layout_ = klvk::VulkanValue(device.createDescriptorSetLayout(layout_info), "vkCreateDescriptorSetLayout");

    const vk::DescriptorPoolSize pool_size =
        vk::DescriptorPoolSize{}.setType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(1);
    const vk::DescriptorPoolCreateInfo pool_info = vk::DescriptorPoolCreateInfo{}.setMaxSets(1).setPoolSizes(pool_size);
    descriptor_pool_ = klvk::VulkanValue(device.createDescriptorPool(pool_info), "vkCreateDescriptorPool");
    const vk::DescriptorSetAllocateInfo allocate_info =
        vk::DescriptorSetAllocateInfo{}.setDescriptorPool(descriptor_pool_).setSetLayouts(set_layout_);
    descriptor_set_ =
        klvk::VulkanValue(device.allocateDescriptorSets(allocate_info), "vkAllocateDescriptorSets").front();

    const klvk::DescriptorSetLayoutView set_layout_view{
        .handle = set_layout_,
        .description = &set_layout_description_,
    };
    pipeline_layout_ = klvk::PipelineLayout{context, std::span{&set_layout_view, 1}};
    auto stages = fullscreen_shader_.MakeStages();
    stages.Append(textured_quad_shader_.MakeStages());
    pipeline_ = CreateFullscreenPipeline(*app_, pipeline_layout_, stages);
}

SimpleCpuRenderer::~SimpleCpuRenderer() noexcept
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    context.WaitIdle();
    vk::Device device = context.GetDevice();
    DestroyImage();
    device.destroyPipeline(pipeline_);
    device.destroyDescriptorPool(descriptor_pool_);
    device.destroyDescriptorSetLayout(set_layout_);
    device.destroySampler(sampler_);
}

void SimpleCpuRenderer::DestroyImage()
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    if (image_view_) context.GetDevice().destroyImageView(image_view_);
    if (image_)
    {
        const VkImage raw_image = image_;
        vmaDestroyImage(context.GetAllocator(), raw_image, image_allocation_);
    }
    image_view_ = nullptr;
    image_ = nullptr;
    image_allocation_ = nullptr;
    image_initialized_ = false;
}

void SimpleCpuRenderer::ApplySettings(const FractalSettings& settings)
{
    klvk::DeviceContext& context = app_->GetDeviceContext();
    vk::Device device = context.GetDevice();

    if (auto s = settings.viewport.size.Cast<size_t>(); !image_ || image_size_ != s)
    {
        context.WaitIdle();
        DestroyImage();
        image_size_ = s;

        const vk::ImageCreateInfo image_info =
            vk::ImageCreateInfo{}
                .setImageType(vk::ImageType::e2D)
                .setFormat(vk::Format::eR8G8B8A8Unorm)
                .setExtent(vk::Extent3D{static_cast<u32>(s.x()), static_cast<u32>(s.y()), 1})
                .setMipLevels(1)
                .setArrayLayers(1)
                .setSamples(vk::SampleCountFlagBits::e1)
                .setTiling(vk::ImageTiling::eOptimal)
                .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
                .setSharingMode(vk::SharingMode::eExclusive)
                .setInitialLayout(vk::ImageLayout::eUndefined);
        const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO};
        const VkImageCreateInfo& raw_image_info = image_info;
        VkImage raw_image = nullptr;
        klvk::VulkanCheck(
            static_cast<vk::Result>(vmaCreateImage(
                context.GetAllocator(),
                &raw_image_info,
                &allocation_info,
                &raw_image,
                &image_allocation_,
                nullptr)),
            "vmaCreateImage");
        image_ = raw_image;

        const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                .setLevelCount(1)
                                                                .setLayerCount(1);
        const vk::ImageViewCreateInfo view_info = vk::ImageViewCreateInfo{}
                                                      .setImage(image_)
                                                      .setViewType(vk::ImageViewType::e2D)
                                                      .setFormat(vk::Format::eR8G8B8A8Unorm)
                                                      .setSubresourceRange(subresource_range);
        image_view_ = klvk::VulkanValue(device.createImageView(view_info), "vkCreateImageView");

        for (auto& buffer : staging_buffers_)
        {
            buffer = klvk::GpuBuffer(
                context,
                vk::BufferUsageFlagBits::eTransferSrc,
                s.x() * s.y() * sizeof(edt::Vec4u8),
                true);
        }

        const vk::DescriptorImageInfo descriptor_image_info =
            vk::DescriptorImageInfo{}
                .setSampler(sampler_)
                .setImageView(image_view_)
                .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        const vk::WriteDescriptorSet write = vk::WriteDescriptorSet{}
                                                 .setDstSet(descriptor_set_)
                                                 .setDstBinding(0)
                                                 .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                                                 .setImageInfo(descriptor_image_info);
        device.updateDescriptorSets(std::span{&write, 1}, {});
    }

    pallette.resize(max_iterations + 1);
    settings.ComputeColors(pallette.size(), [&](size_t index, const edt::Vec3f& color) { pallette[index] = color; });
}

void SimpleCpuRenderer::PrepareFrame(vk::CommandBuffer command_buffer, const FractalSettings& settings)
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
            pixel = edt::Vec4u8{(edt::Math::Clamp(color, 0.f, 1.f) * 255.f).Cast<u8>(), 255};
        }
    }

    klvk::GpuBuffer& staging = staging_buffers_[app_->GetFrameInFlightIndex()];
    staging.Write(std::as_bytes(std::span{image_buffer_}));

    const vk::ImageSubresourceRange subresource_range =
        vk::ImageSubresourceRange{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLevelCount(1).setLayerCount(1);
    vk::ImageMemoryBarrier2 barrier =
        vk::ImageMemoryBarrier2{}
            .setSrcStageMask(vk::PipelineStageFlagBits2::eFragmentShader)
            .setSrcAccessMask(vk::AccessFlagBits2::eNone)
            .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)
            .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
            .setOldLayout(image_initialized_ ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
            .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
            .setImage(image_)
            .setSubresourceRange(subresource_range);
    const vk::DependencyInfo dependency = vk::DependencyInfo{}.setImageMemoryBarriers(barrier);
    command_buffer.pipelineBarrier2(dependency);

    const vk::ImageSubresourceLayers image_subresource =
        vk::ImageSubresourceLayers{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLayerCount(1);
    const vk::BufferImageCopy region = vk::BufferImageCopy{}
                                           .setImageSubresource(image_subresource)
                                           .setImageExtent(vk::Extent3D{static_cast<u32>(w), static_cast<u32>(h), 1});
    command_buffer
        .copyBufferToImage(staging.GetHandle(), image_, vk::ImageLayout::eTransferDstOptimal, std::span{&region, 1});

    barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    command_buffer.pipelineBarrier2(dependency);

    image_initialized_ = true;
}

void SimpleCpuRenderer::Render(vk::CommandBuffer command_buffer, const FractalSettings& settings)
{
    if (!image_initialized_) return;

    CmdSetGlStyleViewport(command_buffer, settings.viewport, app_->GetWindow().GetSize());
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipeline_layout_.GetHandle(),
        0,
        std::span{&descriptor_set_, 1},
        {});
    command_buffer.draw(6, 1, 0, 0);
}
