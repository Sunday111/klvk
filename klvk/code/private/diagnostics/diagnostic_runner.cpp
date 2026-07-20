#include "diagnostic_runner.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <set>
#include <type_traits>

#include "klvk/error_handling.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{
namespace
{

bool IsCaptureFormat(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB ||
           format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
}

size_t CheckedPixelCount(VkExtent2D extent)
{
    const u64 pixel_count = static_cast<u64>(extent.width) * extent.height;
    ErrorHandling::Ensure(
        pixel_count <= std::numeric_limits<VkDeviceSize>::max() / 4,
        "Diagnostic framebuffer is too large for a Vulkan readback buffer");
    ErrorHandling::Ensure(
        pixel_count <= std::numeric_limits<size_t>::max() / 4,
        "Diagnostic framebuffer is too large for this process address space");
    ErrorHandling::Ensure(
        pixel_count <= static_cast<u64>(std::numeric_limits<std::streamsize>::max()) / 3,
        "Diagnostic framebuffer is too large for PPM output");
    return static_cast<size_t>(pixel_count);
}

}  // namespace

DiagnosticRunner::DiagnosticRunner(
    const DiagnosticRunConfig& config,
    const std::filesystem::path& executable_directory,
    size_t frames_in_flight,
    events::EventManager& event_manager)
    : pending_(frames_in_flight),
      event_manager_(event_manager)
{
    std::set<std::filesystem::path> resolved_paths;
    captures_.reserve(config.captures.size());
    queued_without_ui_.reserve(config.captures.size());
    queued_with_ui_.reserve(config.captures.size());
    for (auto capture : config.captures)
    {
        if (capture.path.is_relative()) capture.path = executable_directory / capture.path;
        capture.path = capture.path.lexically_normal();
        ErrorHandling::Ensure(
            capture.path.extension() == ".ppm",
            "Diagnostic capture '{}' must use the .ppm extension",
            capture.path.string());
        ErrorHandling::Ensure(
            resolved_paths.insert(capture.path).second,
            "Multiple diagnostic captures resolve to output path '{}'",
            capture.path.string());
        captures_.push_back({.config = std::move(capture)});
    }
    for (size_t capture_index = 0; capture_index != captures_.size(); ++capture_index)
    {
        ScheduleCapture(capture_index, config.exit.after_last_capture);
    }
    ScheduleQuit(config.exit);

    event_listener_ = events::EventListenerMethodCallbacks<&DiagnosticRunner::OnCaptureDue>::CreatePtr(this);
    event_manager_.AddEventListener(*event_listener_);
}

DiagnosticRunner::~DiagnosticRunner()
{
    timers_.Clear();
    if (event_listener_) event_manager_.RemoveListener(event_listener_.get());
}

void DiagnosticRunner::ScheduleCapture(size_t capture_index, bool quit_after_last_capture)
{
    ErrorHandling::Ensure(capture_index < captures_.size(), "Invalid diagnostic capture index");
    const auto callback = [this, capture_index, quit_after_last_capture](const TimerEvent&)
    {
        event_manager_.Emit(events::DiagnosticCaptureDue{.capture_index = capture_index});
        if (quit_after_last_capture && triggered_capture_count_ == captures_.size())
        {
            event_manager_.Emit(events::OnApplicationQuitRequested{});
        }
    };
    const DiagnosticCaptureConfig& capture = captures_[capture_index].config;
    if (capture.frame.has_value())
    {
        [[maybe_unused]] const TimerHandle timer = timers_.ScheduleAtFrame(*capture.frame, callback);
    }
    else
    {
        ErrorHandling::Ensure(capture.time_seconds.has_value(), "Diagnostic capture has no trigger");
        [[maybe_unused]] const TimerHandle timer = timers_.ScheduleAt(TimerDuration{*capture.time_seconds}, callback);
    }
}

void DiagnosticRunner::ScheduleQuit(const DiagnosticExitConfig& exit)
{
    const auto callback = [this](const TimerEvent&)
    {
        event_manager_.Emit(events::OnApplicationQuitRequested{});
    };
    if (exit.frame.has_value())
    {
        [[maybe_unused]] const TimerHandle timer = timers_.ScheduleAtFrame(*exit.frame, callback);
    }
    else if (exit.time_seconds.has_value())
    {
        [[maybe_unused]] const TimerHandle timer = timers_.ScheduleAt(TimerDuration{*exit.time_seconds}, callback);
    }
}

void DiagnosticRunner::OnCaptureDue(const events::DiagnosticCaptureDue& event)
{
    ErrorHandling::Ensure(event.capture_index < captures_.size(), "Invalid due diagnostic capture index");
    Capture& capture = captures_[event.capture_index];
    ErrorHandling::Ensure(!capture.queued && !capture.recorded, "Diagnostic capture was triggered more than once");
    auto& queue = capture.config.include_ui ? queued_with_ui_ : queued_without_ui_;
    queue.push_back(event.capture_index);
    capture.queued = true;
    ++triggered_capture_count_;
}

void DiagnosticRunner::Advance(u64 frame, double time_seconds)
{
    [[maybe_unused]] const u64 callback_count =
        timers_.Advance(TimerDuration{time_seconds}, frame, std::numeric_limits<u64>::max());
}

bool DiagnosticRunner::HasQueuedCaptures(bool include_ui) const noexcept
{
    const auto& queue = include_ui ? queued_with_ui_ : queued_without_ui_;
    return !queue.empty();
}

bool DiagnosticRunner::RecordQueuedCaptures(
    DeviceContext& context,
    VkCommandBuffer command_buffer,
    size_t frame_in_flight,
    bool include_ui,
    VkImage image,
    VkFormat format,
    VkExtent2D extent,
    VkImageLayout final_layout)
{
    static_assert(std::is_nothrow_move_constructible_v<PendingCapture>);

    auto& queue = include_ui ? queued_with_ui_ : queued_without_ui_;
    if (queue.empty()) return false;

    std::vector<std::filesystem::path> paths;
    paths.reserve(queue.size());
    for (size_t capture_index : queue)
    {
        ErrorHandling::Ensure(capture_index < captures_.size(), "Invalid queued diagnostic capture index");
        Capture& capture = captures_[capture_index];
        ErrorHandling::Ensure(
            capture.queued && !capture.recorded && capture.config.include_ui == include_ui,
            "Diagnostic capture queue is corrupt");
        paths.push_back(capture.config.path);
    }

    ErrorHandling::Ensure(frame_in_flight < pending_.size(), "Invalid diagnostic frame-in-flight index");
    ErrorHandling::Ensure(extent.width != 0 && extent.height != 0, "Cannot capture an empty framebuffer");
    ErrorHandling::Ensure(
        IsCaptureFormat(format),
        "Diagnostic PPM capture does not support Vulkan format {}",
        static_cast<int>(format));
    const size_t pixel_count = CheckedPixelCount(extent);
    const VkDeviceSize byte_size = static_cast<VkDeviceSize>(pixel_count) * 4;

    VkPipelineStageFlags2 final_stage_mask = 0;
    VkAccessFlags2 final_access_mask = 0;
    if (final_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        final_stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        final_access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else
    {
        ErrorHandling::Ensure(
            final_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            "Unsupported diagnostic capture final image layout");
        final_stage_mask = VK_PIPELINE_STAGE_2_NONE;
        final_access_mask = VK_ACCESS_2_NONE;
    }

    auto& pending_frame = pending_[frame_in_flight];
    ErrorHandling::Ensure(
        pending_frame.size() < pending_frame.max_size(),
        "Too many pending diagnostic captures for one frame");
    pending_frame.reserve(pending_frame.size() + 1);

    PendingCapture pending{
        .buffer = GpuBuffer(context, VK_BUFFER_USAGE_TRANSFER_DST_BIT, byte_size, GpuBufferHostAccess::Random),
        .format = format,
        .extent = extent,
        .paths = std::move(paths)};

    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier};
    Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
    const std::array regions{VkBufferImageCopy{
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {.width = extent.width, .height = extent.height, .depth = 1}}};
    Vulkan::CmdCopyImageToBuffer(
        command_buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        pending.buffer.GetHandle(),
        regions);

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = final_layout;
    barrier.dstStageMask = final_stage_mask;
    barrier.dstAccessMask = final_access_mask;
    Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
    pending_frame.push_back(std::move(pending));

    for (size_t capture_index : queue)
    {
        Capture& capture = captures_[capture_index];
        capture.queued = false;
        capture.recorded = true;
    }
    queue.clear();
    return true;
}

void DiagnosticRunner::WritePpm(PendingCapture& capture)
{
    const bool bgra = capture.format == VK_FORMAT_B8G8R8A8_UNORM || capture.format == VK_FORMAT_B8G8R8A8_SRGB;
    ErrorHandling::Ensure(IsCaptureFormat(capture.format), "Pending diagnostic capture has an invalid format");
    const size_t pixel_count = CheckedPixelCount(capture.extent);
    std::vector<std::byte> source(pixel_count * 4);
    capture.buffer.Read(source);
    std::vector<char> rgb(pixel_count * 3);
    for (size_t pixel = 0; pixel != pixel_count; ++pixel)
    {
        const auto channel = [&](size_t index)
        {
            return static_cast<char>(source[pixel * 4 + index]);
        };
        rgb[pixel * 3] = channel(bgra ? 2 : 0);
        rgb[pixel * 3 + 1] = channel(1);
        rgb[pixel * 3 + 2] = channel(bgra ? 0 : 2);
    }

    for (const std::filesystem::path& path : capture.paths)
    {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::filesystem::path temporary = path;
        static std::atomic<u64> temporary_sequence = 0;
        temporary +=
            fmt::format(".tmp.{}.{}", os::GetProcessId(), temporary_sequence.fetch_add(1, std::memory_order_relaxed));
        bool installed = false;
        auto remove_temporary = OnScopeLeave(
            [&]
            {
                if (installed) return;
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            });
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            ErrorHandling::Ensure(stream.is_open(), "Failed to open diagnostic capture '{}'", temporary.string());
            stream << fmt::format("P6\n{} {}\n255\n", capture.extent.width, capture.extent.height);
            stream.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
            stream.flush();
            ErrorHandling::Ensure(stream.good(), "Failed to flush diagnostic capture '{}'", temporary.string());
            stream.close();
            ErrorHandling::Ensure(!stream.fail(), "Failed to close diagnostic capture '{}'", temporary.string());
        }
        Filesystem::InstallFileAtomically(temporary, path);
        installed = true;
        fmt::println(
            "klvk: captured {}x{} framebuffer to {}",
            capture.extent.width,
            capture.extent.height,
            path.string());
    }
}

void DiagnosticRunner::ProcessCompletedFrame(size_t frame_in_flight)
{
    ErrorHandling::Ensure(frame_in_flight < pending_.size(), "Invalid diagnostic frame-in-flight index");
    for (PendingCapture& capture : pending_[frame_in_flight]) WritePpm(capture);
    pending_[frame_in_flight].clear();
}

void DiagnosticRunner::ProcessAllCompleted()
{
    for (size_t frame = 0; frame != pending_.size(); ++frame) ProcessCompletedFrame(frame);
}

void DiagnosticRunner::EnsureComplete() const
{
    const auto missing =
        static_cast<size_t>(std::ranges::count_if(captures_, [](const Capture& capture) { return !capture.recorded; }));
    ErrorHandling::Ensure(
        missing == 0,
        "Diagnostic run ended before {} requested capture{} could be recorded",
        missing,
        missing == 1 ? "" : "s");
}

}  // namespace klvk
