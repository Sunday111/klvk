#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <imgui.h>
#include <vk_mem_alloc.h>

#include <bit>
#include <condition_variable>
#include <edt/math/math.hpp>
#include <edt/threading/thread_name.hpp>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include "klvk/application.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/rendering/curve_renderer_2d.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/window.hpp"

namespace
{
using namespace edt::lazy_matrix_aliases;  // NOLINT

[[nodiscard]] constexpr Vec3f RgbToHsv(Vec3f input)
{
    const float minimum = input.Min();
    const float maximum = input.Max();
    const float delta = maximum - minimum;
    Vec3f output{};
    output[2] = maximum;
    if (delta < 0.00001f) return output;
    if (maximum <= 0.f) return {NAN, 0.f, maximum};
    output[1] = delta / maximum;
    if (input[0] >= maximum)
    {
        output[0] = (input[1] - input[2]) / delta;
    }
    else if (input[1] >= maximum)
    {
        output[0] = 2.f + (input[2] - input[0]) / delta;
    }
    else
    {
        output[0] = 4.f + (input[0] - input[1]) / delta;
    }
    output[0] *= 60.f;
    if (output[0] < 0.f) output[0] += 360.f;
    return output;
}

[[nodiscard]] constexpr Vec3f HsvToRgb(Vec3f input)
{
    if (input[1] <= 0.f) return Vec3f{} + input[2];
    float hue = input[0] >= 360.f ? 0.f : input[0];
    hue /= 60.f;
    const auto sector = static_cast<u8>(hue);
    const float fraction = hue - static_cast<float>(sector);
    const float p = input[2] * (1.f - input[1]);
    const float q = input[2] * (1.f - input[1] * fraction);
    const float t = input[2] * (1.f - input[1] * (1.f - fraction));
    switch (sector)
    {
    case 0:
        return {input[2], t, p};
    case 1:
        return {q, input[2], p};
    case 2:
        return {p, input[2], t};
    case 3:
        return {p, q, input[2]};
    case 4:
        return {t, p, input[2]};
    default:
        return {input[2], p, q};
    }
}

[[nodiscard]] Vec3f LerpHsv(Vec3f a, Vec3f b, float t)
{
    float x = a[0] / 360.f;
    const float y = b[0] / 360.f;
    float delta = std::fmod(y - x + 1.f, 1.f);
    if (delta > 0.5f) delta -= 1.f;
    float hue = std::fmod(x + t * delta, 1.f);
    if (hue < 0.f) hue += 1.f;
    Vec3f result = edt::Math::Lerp(a, b, t);
    result[0] = hue * 360.f;
    return result;
}

class Palette
{
public:
    explicit Palette(size_t size) : colors_(size), positions_(size)
    {
        const float delta = 1.f / static_cast<float>(size - 1);
        for (size_t i = 1; i + 1 < size; ++i) positions_[i] = static_cast<float>(i) * delta;
        positions_.back() = 1.f;
    }

    void Randomize(int seed)
    {
        std::mt19937 random(static_cast<unsigned>(seed));
        std::uniform_real_distribution<float> distribution(0.f, 1.f);
        for (Vec3f& color : colors_) color = color.Transform([&](float) { return distribution(random); });
    }

    [[nodiscard]] std::vector<Vec3f> Compute(size_t size) const
    {
        std::vector<Vec3f> result(size);
        for (size_t i = 0; i != size; ++i)
        {
            const float position = static_cast<float>(i) / static_cast<float>(size - 1);
            size_t left = 0;
            size_t right = colors_.size() - 1;
            for (size_t j = 1; j != colors_.size(); ++j)
            {
                if (positions_[j] > position)
                {
                    left = j - 1;
                    right = j;
                    break;
                }
            }
            const float t = (position - positions_[left]) / (positions_[right] - positions_[left]);
            result[i] = HsvToRgb(LerpHsv(RgbToHsv(colors_[left]), RgbToHsv(colors_[right]), t));
        }
        return result;
    }

private:
    std::vector<Vec3f> colors_;
    std::vector<float> positions_;
};

class CurveFractalApp : public klvk::Application
{
    static constexpr Vec2<u32> kFramebufferResolution{3840, 2160};
    static constexpr size_t kMaxCurves = 10'000;
    static constexpr size_t kMaxCurvesPerFrame = 100;
    static constexpr vk::Format kOffscreenFormat = vk::Format::eR32G32B32A32Sfloat;
    static constexpr float kCurveThickness = 1.f;
    static constexpr float kSegmentPixelLength = 8.f;

    using ControlPoint = klvk::CurveRenderer2d::ControlPoint;

    struct OffscreenTarget
    {
        vk::Image image = nullptr;
        VmaAllocation allocation = nullptr;
        vk::UniqueImageView view;
        bool initialized = false;
    };

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(kFramebufferResolution.x(), kFramebufferResolution.y());
        GetWindow().SetTitle("Curve Fractal");
        listener_ = klvk::events::EventListenerMethodCallbacks<&CurveFractalApp::OnMouseScroll>::CreatePtr(this);
        listener_subscription_ = GetEventManager().AddEventListener(*listener_);

        CreateDisplayResources();
        CreateOffscreenTarget();
        renderers_.reserve(kMaxCurvesPerFrame);
        for (size_t i = 0; i != kMaxCurvesPerFrame; ++i)
        {
            renderers_.emplace_back(
                std::make_unique<klvk::CurveRenderer2d>(
                    *this,
                    kOffscreenFormat,
                    klvk::CurveRenderer2d::CompositeMode::Accumulate));
        }
        draw_batch_.resize(kMaxCurvesPerFrame);

        const auto initial_viewport = klvk::Viewport::FromWindowSize(GetWindow().GetSize());
        transforms_.Update(camera_, initial_viewport, klvk::AspectRatioPolicy::ShrinkToFit);

        constexpr Vec2f eye{};
        constexpr float sample_extent = 3.f;
        constexpr edt::FloatRange2Df world_range =
            edt::FloatRange2Df::FromMinMax(eye - sample_extent, eye + sample_extent);
        const Vec2f thread_tile = world_range.Extent() / 2.f;
        for (size_t x = 0; x != 4; ++x)
        {
            for (size_t y = 0; y != 4; ++y)
            {
                const Vec2f tile_min = Vec2<size_t>{x, y}.Cast<float>() * thread_tile + world_range.Min();
                const auto range = edt::FloatRange2Df::FromMinMax(tile_min, tile_min + thread_tile);
                const size_t producer_index = producers_.size();
                producers_.emplace_back([this, range, producer_index](const std::stop_token& stop)
                                        { ProducerThread(stop, range, producer_index); });
            }
        }
    }

    void ProducerThread(const std::stop_token& stop, const edt::FloatRange2Df world_range, size_t producer_index)
    {
        edt::SetCurrentThreadName("klvk_curve_" + std::to_string(producer_index));
        constexpr size_t max_iterations = 2000;
        const int color_seed = std::bit_cast<int>(std::random_device()());
        Palette palette_settings{10};
        palette_settings.Randomize(color_seed);
        const std::vector<Vec3f> palette = palette_settings.Compute(max_iterations + 1);
        std::mt19937_64 random(static_cast<unsigned>(color_seed));
        std::uniform_real_distribution<float> x_distribution(world_range.Min().x(), world_range.Max().x());
        std::uniform_real_distribution<float> y_distribution(world_range.Min().y(), world_range.Max().y());
        std::vector<ControlPoint> points;

        while (!stop.stop_requested())
        {
            Vec2f z{x_distribution(random), y_distribution(random)};
            points.clear();
            points.push_back({.position = z});
            size_t iteration = 0;
            while (iteration != max_iterations)
            {
                const Vec2f next = edt::Math::ComplexPower(z, Vec2f{6.f, 0.f}) + Vec2f{0.529f, 0.508f};
                points.push_back({.position = {z.y(), -z.x()}});
                if (next.SquaredLength() > world_range.Extent().SquaredLength()) break;
                z = next;
                ++iteration;
            }

            const size_t num_points = points.size();
            if (num_points <= 2) continue;
            for (size_t i = 0; i != num_points; ++i)
            {
                points[i].color = Vec4f(palette[i], 1.f);
                points[i].color.w() = static_cast<float>(i) / static_cast<float>(num_points) * 0.2f;
            }

            std::unique_lock lock(queue_mutex_);
            queue_not_full_.wait(lock, stop, [&] { return produced_curves_.size() < kMaxCurves; });
            if (stop.stop_requested()) break;
            produced_curves_.push_back(std::move(points));
        }
    }

    void CreateDisplayResources()
    {
        auto& context = GetDeviceContext();
        const vk::Device device = context.GetDevice();
        descriptor_sets_ =
            klvk::DescriptorSets::Builder(context)
                .Binding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
                .Build();
        const vk::SamplerCreateInfo sampler_info = vk::SamplerCreateInfo{}
                                                       .setMagFilter(vk::Filter::eLinear)
                                                       .setMinFilter(vk::Filter::eLinear)
                                                       .setMipmapMode(vk::SamplerMipmapMode::eNearest)
                                                       .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                                       .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                                       .setAddressModeW(vk::SamplerAddressMode::eClampToBorder);
        sampler_ = device.createSamplerUnique(sampler_info);
        const auto set_layout = descriptor_sets_.GetLayoutView();
        pipeline_layout_ = klvk::PipelineLayout{context, std::span{&set_layout, 1}};
        pipeline_ = CreateDisplayPipeline(context);
    }

    [[nodiscard]] vk::UniquePipeline CreateDisplayPipeline(klvk::DeviceContext&)
    {
        const std::filesystem::path shader_dir = GetShaderDir() / "curve_fractal";
        return klvk::GraphicsPipelineBuilder(*this)
            .Layout(pipeline_layout_)
            .VertexShaderFile(shader_dir / "textured_quad.vert.slang")
            .FragmentShaderFile(shader_dir / "textured_quad.frag.slang")
            .Build();
    }

    void CreateOffscreenTarget()
    {
        auto& context = GetDeviceContext();
        const vk::ImageCreateInfo image_info =
            vk::ImageCreateInfo{}
                .setImageType(vk::ImageType::e2D)
                .setFormat(kOffscreenFormat)
                .setExtent(vk::Extent3D{kFramebufferResolution.x(), kFramebufferResolution.y(), 1})
                .setMipLevels(1)
                .setArrayLayers(1)
                .setSamples(vk::SampleCountFlagBits::e1)
                .setTiling(vk::ImageTiling::eOptimal)
                .setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
                .setSharingMode(vk::SharingMode::eExclusive)
                .setInitialLayout(vk::ImageLayout::eUndefined);
        VmaAllocationCreateInfo allocation_info{};
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        const VkImageCreateInfo& raw_image_info = image_info;
        VkImage raw_image = nullptr;
        klvk::VulkanCheck(
            static_cast<vk::Result>(vmaCreateImage(
                context.GetAllocator(),
                &raw_image_info,
                &allocation_info,
                &raw_image,
                &target_.allocation,
                nullptr)));
        target_.image = raw_image;
        const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                .setLevelCount(1)
                                                                .setLayerCount(1);
        const vk::ImageViewCreateInfo view_info = vk::ImageViewCreateInfo{}
                                                      .setImage(target_.image)
                                                      .setViewType(vk::ImageViewType::e2D)
                                                      .setFormat(kOffscreenFormat)
                                                      .setSubresourceRange(subresource_range);
        target_.view = context.GetDevice().createImageViewUnique(view_info);
        descriptor_sets_.WriteImage(0, 0, target_.view.get(), sampler_.get());
    }

    size_t DrainProducedCurves()
    {
        std::scoped_lock lock(queue_mutex_);
        const size_t count = std::min(kMaxCurvesPerFrame, produced_curves_.size());
        for (size_t i = 0; i != count; ++i)
        {
            draw_batch_[i] = std::move(produced_curves_.back());
            produced_curves_.pop_back();
        }
        if (count != 0) queue_not_full_.notify_all();
        return count;
    }

    void BeforeSwapchainRender(vk::CommandBuffer command_buffer) override
    {
        const vk::ImageLayout old_layout =
            target_.initialized ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined;
        const vk::ImageSubresourceRange subresource_range = vk::ImageSubresourceRange{}
                                                                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                .setLevelCount(1)
                                                                .setLayerCount(1);
        vk::ImageMemoryBarrier2 barrier =
            vk::ImageMemoryBarrier2{}
                .setSrcStageMask(
                    target_.initialized ? vk::PipelineStageFlagBits2::eFragmentShader
                                        : vk::PipelineStageFlagBits2::eNone)
                .setSrcAccessMask(
                    target_.initialized ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlagBits2::eNone)
                .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                .setOldLayout(old_layout)
                .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setImage(target_.image)
                .setSubresourceRange(subresource_range);
        const vk::DependencyInfo dependency = vk::DependencyInfo{}.setImageMemoryBarriers(barrier);
        command_buffer.pipelineBarrier2(dependency);
        const vk::RenderingAttachmentInfo attachment =
            vk::RenderingAttachmentInfo{}
                .setImageView(target_.view.get())
                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setLoadOp(target_.initialized ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear)
                .setStoreOp(vk::AttachmentStoreOp::eStore);
        const vk::RenderingInfo rendering_info =
            vk::RenderingInfo{}
                .setRenderArea(vk::Rect2D{{}, {kFramebufferResolution.x(), kFramebufferResolution.y()}})
                .setLayerCount(1)
                .setColorAttachments(attachment);
        command_buffer.beginRendering(rendering_info);
        const vk::Viewport offscreen_viewport = vk::Viewport{}
                                                    .setY(static_cast<float>(kFramebufferResolution.y()))
                                                    .setWidth(static_cast<float>(kFramebufferResolution.x()))
                                                    .setHeight(-static_cast<float>(kFramebufferResolution.y()))
                                                    .setMinDepth(0.f)
                                                    .setMaxDepth(1.f);
        const vk::Rect2D scissor =
            vk::Rect2D{}.setExtent(vk::Extent2D{kFramebufferResolution.x(), kFramebufferResolution.y()});
        command_buffer.setViewport(0, std::span{&offscreen_viewport, 1});
        command_buffer.setScissor(0, std::span{&scissor, 1});

        // The offscreen pass renders at kFramebufferResolution, so the curve
        // renderer's viewport_size (which its coverage math keys pixel-space
        // distance on) and the camera aspect must both use that target size, not
        // the window size — they differ whenever the window is smaller than the
        // fixed offscreen target (e.g. a diagnostic capture forces a small size).
        const auto viewport = klvk::Viewport::FromWindowSize(kFramebufferResolution);
        transforms_.Update(camera_, viewport, klvk::AspectRatioPolicy::ShrinkToFit);
        // thickness and segment length are authored in display pixels; scale them
        // to the offscreen target so a curve keeps the same on-screen weight (and
        // the same accumulated density) as when it was drawn at the window size.
        const float target_scale =
            static_cast<float>(kFramebufferResolution.y()) / static_cast<float>(GetWindow().GetSize().y());
        const size_t curve_count = DrainProducedCurves();
        for (size_t i = 0; i != curve_count; ++i)
        {
            renderers_[i]->Draw(
                draw_batch_[i],
                viewport.size.Cast<float>(),
                transforms_.world_to_view,
                kCurveThickness * target_scale,
                kSegmentPixelLength * target_scale);
        }
        command_buffer.endRendering();

        barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
        barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        command_buffer.pipelineBarrier2(dependency);
        target_.initialized = true;
    }

    void Tick() override
    {
        klvk::Application::Tick();
        const vk::CommandBuffer command_buffer = GetCurrentCommandBuffer();
        const vk::DescriptorSet descriptor_set = descriptor_sets_.Get(0);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
        command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipeline_layout_.GetHandle(),
            0,
            std::span{&descriptor_set, 1},
            {});
        command_buffer.draw(6, 1, 0, 0);
        HandleInput();
    }

    void HandleInput()
    {
        if (ImGui::GetIO().WantCaptureKeyboard) return;
        int right = 0;
        int up = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) ++up;
        if (ImGui::IsKeyDown(ImGuiKey_S)) --up;
        if (ImGui::IsKeyDown(ImGuiKey_D)) ++right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) --right;
        if (std::abs(right) + std::abs(up) == 0) return;
        Vec2f delta{};
        delta += static_cast<float>(right) * Vec2f::AxisX();
        delta += static_cast<float>(up) * Vec2f::AxisY();
        camera_.eye = camera_.eye + delta * move_speed_ * GetLastFrameDurationSeconds() / camera_.zoom;
    }

    void OnMouseScroll(const klvk::events::OnMouseScroll& event)
    {
        if (ImGui::GetIO().WantCaptureMouse) return;
        zoom_power_ += event.value.y();
        camera_.zoom = std::max(std::pow(1.1f, zoom_power_), 0.1f);
    }

public:
    ~CurveFractalApp() override
    {
        for (std::jthread& producer : producers_) producer.request_stop();
        queue_not_full_.notify_all();
        producers_.clear();
        renderers_.clear();

        // The offscreen accumulation image is VMA-owned, so reset its view before
        // destroying the image. Application::Run already waited for the device to go idle.
        if (target_.image)
        {
            auto& context = GetDeviceContext();
            target_.view.reset();
            const VkImage raw_image = target_.image;
            vmaDestroyImage(context.GetAllocator(), raw_image, target_.allocation);
        }
    }

private:
    std::unique_ptr<klvk::events::IEventListener> listener_;
    klvk::events::EventSubscription listener_subscription_;
    klvk::Camera2d camera_{};
    klvk::RenderTransforms2d transforms_{};
    float move_speed_ = 0.5f;
    float zoom_power_ = 0.f;
    std::vector<std::jthread> producers_;
    std::mutex queue_mutex_;
    std::condition_variable_any queue_not_full_;
    std::vector<std::vector<ControlPoint>> produced_curves_;
    std::vector<std::vector<ControlPoint>> draw_batch_;
    std::vector<std::unique_ptr<klvk::CurveRenderer2d>> renderers_;
    OffscreenTarget target_{};
    klvk::DescriptorSets descriptor_sets_;
    vk::UniqueSampler sampler_;
    klvk::PipelineLayout pipeline_layout_;
    vk::UniquePipeline pipeline_;
};

void Main(int argc, char** argv)
{
    CurveFractalApp app;
    app.RunWithArguments(argc, argv);
}
}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
