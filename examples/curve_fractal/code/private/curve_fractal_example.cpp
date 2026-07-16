#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <imgui.h>
#include <vk_mem_alloc.h>

#include <EverydayTools/Math/Math.hpp>
#include <bit>
#include <condition_variable>
#include <mutex>
#include <random>
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
#include "klvk/vulkan/vk_object.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace
{
using namespace edt::lazy_matrix_aliases;  // NOLINT

[[nodiscard]] constexpr Vec3f RgbToHsv(Vec3f input)
{
    const float minimum = std::min({input[0], input[1], input[2]});
    const float maximum = std::max({input[0], input[1], input[2]});
    const float delta = maximum - minimum;
    Vec3f output{};
    output[2] = maximum;
    if (delta < 0.00001f) return output;
    if (maximum <= 0.f) return {NAN, 0.f, maximum};
    output[1] = delta / maximum;
    if (input[0] >= maximum)
        output[0] = (input[1] - input[2]) / delta;
    else if (input[1] >= maximum)
        output[0] = 2.f + (input[2] - input[0]) / delta;
    else
        output[0] = 4.f + (input[0] - input[1]) / delta;
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
    return {hue * 360.f, std::lerp(a[1], b[1], t), std::lerp(a[2], b[2], t)};
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
                if (positions_[j] > position)
                {
                    left = j - 1;
                    right = j;
                    break;
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
    static constexpr VkFormat kOffscreenFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    static constexpr float kCurveThickness = 1.f;
    static constexpr float kSegmentPixelLength = 8.f;

    using Vertex = klvk::CurveRenderer2d::Vertex;

    // The world-to-view transform the producer threads tessellate against. The
    // render thread republishes it every frame; producers read the latest snapshot.
    // While the camera is static (the usual case) this is exact; during a pan or
    // zoom, curves already queued were baked against a slightly older transform.
    struct SharedTransform
    {
        Mat3f world_to_view{};
        Vec2f viewport_size{};
    };

    struct OffscreenTarget
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        bool initialized = false;
    };

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(kFramebufferResolution.x(), kFramebufferResolution.y());
        GetWindow().SetTitle("Curve Fractal");
        listener_ = klvk::events::EventListenerMethodCallbacks<&CurveFractalApp::OnMouseScroll>::CreatePtr(this);
        GetEventManager().AddEventListener(*listener_);

        CreateDisplayResources();
        CreateOffscreenTarget();
        renderers_.reserve(kMaxCurvesPerFrame);
        for (size_t i = 0; i != kMaxCurvesPerFrame; ++i)
            renderers_.emplace_back(std::make_unique<klvk::CurveRenderer2d>(*this, kOffscreenFormat));
        draw_batch_.resize(kMaxCurvesPerFrame);

        // Seed the transform the producers tessellate against before they start.
        const auto initial_viewport = klvk::Viewport::FromWindowSize(GetWindow().GetSize());
        transforms_.Update(camera_, initial_viewport, klvk::AspectRatioPolicy::ShrinkToFit);
        shared_transform_ = {transforms_.world_to_view, initial_viewport.size.Cast<float>()};

        constexpr Vec2f eye{};
        constexpr float sample_extent = 3.f;
        constexpr edt::FloatRange2Df world_range =
            edt::FloatRange2Df::FromMinMax(eye - sample_extent, eye + sample_extent);
        const Vec2f thread_tile = world_range.Extent() / 2.f;
        for (size_t x = 0; x != 4; ++x)
            for (size_t y = 0; y != 4; ++y)
            {
                const Vec2f tile_min = Vec2<size_t>{x, y}.Cast<float>() * thread_tile + world_range.Min();
                const auto range = edt::FloatRange2Df::FromMinMax(tile_min, tile_min + thread_tile);
                producers_.emplace_back([this, range](std::stop_token stop) { ProducerThread(stop, range); });
            }
    }

    void ProducerThread(std::stop_token stop, const edt::FloatRange2Df world_range)
    {
        constexpr size_t max_iterations = 2000;
        const int color_seed = std::bit_cast<int>(std::random_device()());
        Palette palette_settings{10};
        palette_settings.Randomize(color_seed);
        const std::vector<Vec3f> palette = palette_settings.Compute(max_iterations + 1);
        std::mt19937_64 random(static_cast<unsigned>(color_seed));
        std::uniform_real_distribution<float> x_distribution(world_range.Min().x(), world_range.Max().x());
        std::uniform_real_distribution<float> y_distribution(world_range.Min().y(), world_range.Max().y());
        std::vector<klvk::CurveRenderer2d::ControlPoint> points;
        std::vector<Vertex> vertices;

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

            // Tessellate here on the producer thread (the expensive part), off the
            // render thread. Uses the latest transform the render thread published.
            SharedTransform transform;
            {
                std::scoped_lock transform_lock(transform_mutex_);
                transform = shared_transform_;
            }
            klvk::CurveRenderer2d::BuildVertices(
                points,
                kCurveThickness,
                kSegmentPixelLength,
                transform.viewport_size,
                transform.world_to_view,
                vertices);
            if (vertices.empty()) continue;

            std::unique_lock lock(queue_mutex_);
            queue_not_full_.wait(lock, stop, [&] { return produced_curves_.size() < kMaxCurves; });
            if (stop.stop_requested()) break;
            produced_curves_.push_back(std::move(vertices));
        }
    }

    void CreateDisplayResources()
    {
        auto& context = GetDeviceContext();
        const VkDevice device = context.GetDevice();
        descriptor_sets_ = klvk::DescriptorSets::Builder(context)
                               .Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .Build();
        sampler_ = klvk::VkObject<VkSampler>{
            device,
            klvk::Vulkan::CreateSampler(
                device,
                {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                 .magFilter = VK_FILTER_LINEAR,
                 .minFilter = VK_FILTER_LINEAR,
                 .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                 .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                 .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                 .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER})};
        const VkDescriptorSetLayout set_layout = descriptor_sets_.GetLayout();
        pipeline_layout_ = klvk::VkObject<VkPipelineLayout>{
            device,
            klvk::Vulkan::CreatePipelineLayout(
                device,
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                 .setLayoutCount = 1,
                 .pSetLayouts = &set_layout})};
        pipeline_ = klvk::VkObject<VkPipeline>{device, CreateDisplayPipeline(context)};
    }

    [[nodiscard]] VkPipeline CreateDisplayPipeline(klvk::DeviceContext&)
    {
        const std::filesystem::path shader_dir = GetShaderDir() / "curve_fractal";
        return klvk::GraphicsPipelineBuilder(*this)
            .Layout(pipeline_layout_)
            .VertexShaderFile(shader_dir / "textured_quad.vert")
            .FragmentShaderFile(shader_dir / "textured_quad.frag")
            .Build();
    }

    void CreateOffscreenTarget()
    {
        auto& context = GetDeviceContext();
        const VkImageCreateInfo image_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = kOffscreenFormat,
            .extent = {kFramebufferResolution.x(), kFramebufferResolution.y(), 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        klvk::CheckVkResult(
            vmaCreateImage(
                context.GetAllocator(),
                &image_info,
                &allocation_info,
                &target_.image,
                &target_.allocation,
                nullptr),
            "vmaCreateImage(curve fractal accumulation)");
        target_.view = klvk::Vulkan::CreateImageView(
            context.GetDevice(),
            {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
             .image = target_.image,
             .viewType = VK_IMAGE_VIEW_TYPE_2D,
             .format = kOffscreenFormat,
             .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}});
        descriptor_sets_.WriteImage(0, 0, target_.view, sampler_);
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

    void BeforeSwapchainRender(VkCommandBuffer command_buffer) override
    {
        const VkImageLayout old_layout =
            target_.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = target_.initialized ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = target_.initialized ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = old_layout,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = target_.image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier};
        klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
        const VkRenderingAttachmentInfo attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target_.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = target_.initialized ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.f, 0.f, 0.f, 0.f}}}};
        const VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = {kFramebufferResolution.x(), kFramebufferResolution.y()}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment};
        klvk::Vulkan::CmdBeginRendering(command_buffer, rendering_info);
        const VkViewport offscreen_viewport{
            .y = static_cast<float>(kFramebufferResolution.y()),
            .width = static_cast<float>(kFramebufferResolution.x()),
            .height = -static_cast<float>(kFramebufferResolution.y()),
            .minDepth = 0.f,
            .maxDepth = 1.f};
        const VkRect2D scissor{.extent = {kFramebufferResolution.x(), kFramebufferResolution.y()}};
        klvk::Vulkan::CmdSetViewport(command_buffer, 0, std::span{&offscreen_viewport, 1});
        klvk::Vulkan::CmdSetScissor(command_buffer, 0, std::span{&scissor, 1});

        const auto viewport = klvk::Viewport::FromWindowSize(GetWindow().GetSize());
        transforms_.Update(camera_, viewport, klvk::AspectRatioPolicy::ShrinkToFit);
        // Publish this frame's transform for the producer threads to tessellate
        // against, then just upload and draw the vertices they already produced.
        {
            std::scoped_lock transform_lock(transform_mutex_);
            shared_transform_ = {transforms_.world_to_view, viewport.size.Cast<float>()};
        }
        const size_t curve_count = DrainProducedCurves();
        for (size_t i = 0; i != curve_count; ++i) renderers_[i]->DrawVertices(draw_batch_[i]);
        klvk::Vulkan::CmdEndRendering(command_buffer);

        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        klvk::Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
        target_.initialized = true;
    }

    void Tick() override
    {
        klvk::Application::Tick();
        const VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        const VkDescriptorSet descriptor_set = descriptor_sets_.Get(0);
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0,
            std::span{&descriptor_set, 1});
        klvk::Vulkan::CmdDraw(command_buffer, 6, 1, 0, 0);
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

        // The offscreen accumulation image is a raw VMA allocation, so it and its
        // view still need manual teardown; the sampler, pipeline, layout and
        // descriptor sets are VkObject / DescriptorSets members that clean up
        // themselves. Application::Run already waited for the device to go idle.
        if (target_.image)
        {
            auto& context = GetDeviceContext();
            klvk::Vulkan::DestroyImageViewNE(context.GetDevice(), target_.view);
            vmaDestroyImage(context.GetAllocator(), target_.image, target_.allocation);
        }
    }

private:
    std::unique_ptr<klvk::events::IEventListener> listener_;
    klvk::Camera2d camera_{};
    klvk::RenderTransforms2d transforms_{};
    float move_speed_ = 0.5f;
    float zoom_power_ = 0.f;
    std::vector<std::jthread> producers_;
    std::mutex queue_mutex_;
    std::condition_variable_any queue_not_full_;
    std::vector<std::vector<Vertex>> produced_curves_;
    std::vector<std::vector<Vertex>> draw_batch_;
    std::vector<std::unique_ptr<klvk::CurveRenderer2d>> renderers_;
    std::mutex transform_mutex_;
    SharedTransform shared_transform_;
    OffscreenTarget target_{};
    klvk::DescriptorSets descriptor_sets_;
    klvk::VkObject<VkSampler> sampler_;
    klvk::VkObject<VkPipelineLayout> pipeline_layout_;
    klvk::VkObject<VkPipeline> pipeline_;
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
