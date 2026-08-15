#include "diagnostic_runner.hpp"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <type_traits>

#include "edt/functional/on_scope_leave.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/window.hpp"
#include "platform/input_mapping.hpp"

namespace klvk
{
namespace
{

bool IsCaptureFormat(vk::Format format)
{
    return format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb ||
           format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eR8G8B8A8Srgb;
}

size_t CheckedPixelCount(vk::Extent2D extent)
{
    const u64 pixel_count = static_cast<u64>(extent.width) * extent.height;
    ErrorHandling::Ensure(
        pixel_count <= std::numeric_limits<vk::DeviceSize>::max() / 4,
        "Diagnostic framebuffer is too large for a Vulkan readback buffer");
    ErrorHandling::Ensure(
        pixel_count <= std::numeric_limits<size_t>::max() / 4,
        "Diagnostic framebuffer is too large for this process address space");
    ErrorHandling::Ensure(
        pixel_count <= static_cast<u64>(std::numeric_limits<std::streamsize>::max()) / 3,
        "Diagnostic framebuffer is too large for PPM output");
    return static_cast<size_t>(pixel_count);
}

u64 HashPixels(std::span<const std::byte> pixels) noexcept
{
    constexpr u64 kOffsetBasis = 14'695'981'039'346'656'037ULL;
    constexpr u64 kPrime = 1'099'511'628'211ULL;
    u64 hash = kOffsetBasis;
    for (const std::byte value : pixels)
    {
        hash ^= static_cast<u64>(std::to_integer<unsigned char>(value));
        hash *= kPrime;
    }
    return hash;
}

}  // namespace

DiagnosticRunner::DiagnosticRunner(
    const DiagnosticRunConfig& config,
    size_t frames_in_flight,
    events::EventManager& event_manager,
    Window& window)
    : pending_(frames_in_flight),
      event_manager_(event_manager),
      window_(window)
{
    if (config.video.has_value())
    {
        ErrorHandling::Ensure(
            config.framebuffer_size.has_value() && config.clock.fixed_step_ns.has_value(),
            "Diagnostic video configuration was not validated");
        const auto size = *config.framebuffer_size;
        video_encoder_ = std::make_unique<DiagnosticVideoEncoder>(
            config.video->path,
            size.x(),
            size.y(),
            *config.clock.fixed_step_ns,
            config.video->encoding,
            config.video->encoding_device,
            config.video->compression_level,
            config.video->log_ffmpeg);
        video_includes_ui_ = config.video->include_ui;
    }

    captures_.reserve(config.captures.size());
    queued_without_ui_.reserve(config.captures.size());
    queued_with_ui_.reserve(config.captures.size());
    for (const auto& capture : config.captures) captures_.push_back({.config = capture});
    if (config.checkpoints.has_value())
    {
        ErrorHandling::Ensure(config.exit.frame.has_value(), "Diagnostic checkpoints were not validated");
        expected_checkpoints_ = config.checkpoints->expected;
        const u64 every = config.checkpoints->every_frames;
        for (u64 frame = every; frame <= *config.exit.frame; frame += every)
        {
            captures_.push_back(
                {.config = {.frame = frame, .include_ui = config.checkpoints->include_ui}, .checkpoint_frame = frame});
            if (frame > std::numeric_limits<u64>::max() - every) break;
        }
    }
    for (size_t capture_index = 0; capture_index != captures_.size(); ++capture_index)
    {
        ScheduleCapture(capture_index, config.exit.after_last_capture);
    }
    dialogs_ = config.dialogs;
    input_count_ = config.input.size();
    for (const DiagnosticInputConfig& input : config.input) ScheduleInput(input);
    ScheduleQuit(config.exit);

    event_listener_ = events::EventListenerMethodCallbacks<&DiagnosticRunner::OnCaptureDue>::CreatePtr(this);
    event_manager_.AddEventListener(*event_listener_);
}

DiagnosticRunner::~DiagnosticRunner()
{
    timers_.Clear();
    input_timers_.Clear();
    if (event_listener_) event_manager_.RemoveListener(event_listener_.get());
}

void DiagnosticRunner::ScheduleInput(const DiagnosticInputConfig& input)
{
    const auto callback = [this, event = input.event](const TimerEvent&)
    {
        ApplyInput(event);
        ++applied_input_count_;
    };
    if (input.frame.has_value())
    {
        [[maybe_unused]] const TimerHandle timer = input_timers_.ScheduleAtFrame(*input.frame, callback);
    }
    else
    {
        ErrorHandling::Ensure(input.time_ns.has_value(), "Diagnostic input has no trigger");
        [[maybe_unused]] const TimerHandle timer = input_timers_.ScheduleAt(TimerDuration{*input.time_ns}, callback);
    }
}

// ImGui tracks the combined state of each modifier pair separately from the
// individual keys, so a replayed modifier has to update both.
void DiagnosticRunner::ApplyModifier(Key key)
{
    struct Modifier
    {
        Key left;
        Key right;
        ImGuiKey flag;
    };
    static constexpr auto kModifiers = std::to_array<Modifier>({
        {.left = Key::LeftCtrl, .right = Key::RightCtrl, .flag = ImGuiMod_Ctrl},
        {.left = Key::LeftShift, .right = Key::RightShift, .flag = ImGuiMod_Shift},
        {.left = Key::LeftAlt, .right = Key::RightAlt, .flag = ImGuiMod_Alt},
        {.left = Key::LeftSuper, .right = Key::RightSuper, .flag = ImGuiMod_Super},
    });

    const auto found = std::ranges::find_if(
        kModifiers,
        [key](const Modifier& modifier) { return modifier.left == key || modifier.right == key; });
    if (found == std::ranges::end(kModifiers)) return;
    ImGui::GetIO().AddKeyEvent(found->flag, window_.IsKeyPressed(found->left) || window_.IsKeyPressed(found->right));
}

void DiagnosticRunner::ApplyInput(const DiagnosticInputEvent& input)
{
    ImGuiIO& io = ImGui::GetIO();
    std::visit(
        [&](const auto& event)
        {
            using Event = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<Event, DiagnosticMouseMoveInput>)
            {
                window_.OnMouseMove(event.position);
                io.AddMousePosEvent(event.position.x(), event.position.y());
            }
            else if constexpr (std::is_same_v<Event, DiagnosticMouseButtonInput>)
            {
                const bool pressed = event.action == InputAction::Press;
                window_.OnMouseButton(event.button, event.action);
                io.AddMouseButtonEvent(MouseButtonToImGui(event.button), pressed);
            }
            else if constexpr (std::is_same_v<Event, DiagnosticMouseScrollInput>)
            {
                window_.OnMouseScroll(event.offset.x(), event.offset.y());
                io.AddMouseWheelEvent(event.offset.x(), event.offset.y());
            }
            else if constexpr (std::is_same_v<Event, DiagnosticKeyInput>)
            {
                const bool pressed = event.action == InputAction::Press;
                window_.OnKey(event.key, event.action);
                io.AddKeyEvent(static_cast<ImGuiKey>(KeyToImGui(event.key)), pressed);
                ApplyModifier(event.key);
            }
        },
        input);
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
        ErrorHandling::Ensure(capture.time_ns.has_value(), "Diagnostic capture has no trigger");
        [[maybe_unused]] const TimerHandle timer = timers_.ScheduleAt(TimerDuration{*capture.time_ns}, callback);
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
    else if (exit.time_ns.has_value())
    {
        [[maybe_unused]] const TimerHandle timer = timers_.ScheduleAt(TimerDuration{*exit.time_ns}, callback);
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

void DiagnosticRunner::Advance(u64 frame, TimerDuration elapsed)
{
    [[maybe_unused]] const u64 callback_count = timers_.Advance(elapsed, frame, std::numeric_limits<u64>::max());
}

void DiagnosticRunner::AdvanceInput(u64 frame, TimerDuration elapsed)
{
    [[maybe_unused]] const u64 callback_count = input_timers_.Advance(elapsed, frame, std::numeric_limits<u64>::max());
}

bool DiagnosticRunner::NeedsReadback(bool include_ui) const noexcept
{
    const auto& queue = include_ui ? queued_with_ui_ : queued_without_ui_;
    return !queue.empty() || (video_encoder_ != nullptr && video_includes_ui_ == include_ui);
}

bool DiagnosticRunner::RecordReadback(
    DeviceContext& context,
    vk::CommandBuffer command_buffer,
    size_t frame_in_flight,
    bool include_ui,
    vk::Image image,
    vk::Format format,
    vk::Extent2D extent,
    vk::ImageLayout final_layout)
{
    static_assert(std::is_nothrow_move_constructible_v<PendingCapture>);

    auto& queue = include_ui ? queued_with_ui_ : queued_without_ui_;
    const bool record_video = video_encoder_ != nullptr && video_includes_ui_ == include_ui;
    if (queue.empty() && !record_video) return false;

    std::vector<std::filesystem::path> paths;
    paths.reserve(queue.size());
    std::optional<u64> checkpoint_frame;
    for (size_t capture_index : queue)
    {
        ErrorHandling::Ensure(capture_index < captures_.size(), "Invalid queued diagnostic capture index");
        Capture& capture = captures_[capture_index];
        ErrorHandling::Ensure(
            capture.queued && !capture.recorded && capture.config.include_ui == include_ui,
            "Diagnostic capture queue is corrupt");
        if (capture.checkpoint_frame.has_value())
        {
            // Checkpoint frames are distinct multiples, so at most one shares a readback.
            ErrorHandling::Ensure(!checkpoint_frame.has_value(), "Two diagnostic checkpoints share one frame");
            checkpoint_frame = capture.checkpoint_frame;
            continue;
        }
        paths.push_back(capture.config.path);
    }

    ErrorHandling::Ensure(frame_in_flight < pending_.size(), "Invalid diagnostic frame-in-flight index");
    ErrorHandling::Ensure(extent.width != 0 && extent.height != 0, "Cannot capture an empty framebuffer");
    ErrorHandling::Ensure(
        IsCaptureFormat(format),
        "Diagnostic readback does not support Vulkan format {}",
        static_cast<int>(format));
    const size_t pixel_count = CheckedPixelCount(extent);
    const vk::DeviceSize byte_size = static_cast<vk::DeviceSize>(pixel_count) * 4;

    vk::PipelineStageFlags2 final_stage_mask{};
    vk::AccessFlags2 final_access_mask{};
    if (final_layout == vk::ImageLayout::eColorAttachmentOptimal)
    {
        final_stage_mask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        final_access_mask = vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
    }
    else
    {
        ErrorHandling::Ensure(
            final_layout == vk::ImageLayout::ePresentSrcKHR,
            "Unsupported diagnostic capture final image layout");
        final_stage_mask = vk::PipelineStageFlagBits2::eNone;
        final_access_mask = vk::AccessFlagBits2::eNone;
    }

    auto& pending_frame = pending_[frame_in_flight];
    ErrorHandling::Ensure(
        pending_frame.size() < pending_frame.max_size(),
        "Too many pending diagnostic captures for one frame");
    pending_frame.reserve(pending_frame.size() + 1);

    PendingCapture pending{
        .buffer = GpuBuffer(context, vk::BufferUsageFlagBits::eTransferDst, byte_size, GpuBufferHostAccess::Random),
        .format = format,
        .extent = extent,
        .paths = std::move(paths),
        .video_frame = record_video ? std::optional<u64>{video_frame_count_++} : std::nullopt,
        .checkpoint_frame = checkpoint_frame};

    const auto range =
        vk::ImageSubresourceRange{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLevelCount(1).setLayerCount(1);
    auto barrier = vk::ImageMemoryBarrier2{}
                       .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                       .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                       .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)
                       .setDstAccessMask(vk::AccessFlagBits2::eTransferRead)
                       .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                       .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                       .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                       .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                       .setImage(image)
                       .setSubresourceRange(range);
    const auto dependency = [&]
    {
        return vk::DependencyInfo{}.setImageMemoryBarrierCount(1).setPImageMemoryBarriers(&barrier);
    };
    command_buffer.pipelineBarrier2(dependency());
    const auto layers = vk::ImageSubresourceLayers{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLayerCount(1);
    const std::array regions{
        vk::BufferImageCopy{}.setImageSubresource(layers).setImageExtent(vk::Extent3D{extent.width, extent.height, 1})};
    command_buffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, pending.buffer.GetHandle(), regions);

    barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
    barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
    barrier.newLayout = final_layout;
    barrier.dstStageMask = final_stage_mask;
    barrier.dstAccessMask = final_access_mask;
    command_buffer.pipelineBarrier2(dependency());
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

void DiagnosticRunner::ProcessReadback(PendingCapture& capture)
{
    const bool bgra = capture.format == vk::Format::eB8G8R8A8Unorm || capture.format == vk::Format::eB8G8R8A8Srgb;
    ErrorHandling::Ensure(IsCaptureFormat(capture.format), "Pending diagnostic capture has an invalid format");
    const size_t pixel_count = CheckedPixelCount(capture.extent);
    std::vector<std::byte> source(pixel_count * 4);
    capture.buffer.Read(source);
    if (capture.checkpoint_frame.has_value()) RecordCheckpoint(*capture.checkpoint_frame, source);
    if (!capture.paths.empty())
    {
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
            temporary += fmt::format(
                ".tmp.{}.{}",
                os::GetProcessId(),
                temporary_sequence.fetch_add(1, std::memory_order_relaxed));
            bool installed = false;
            auto remove_temporary = edt::OnScopeLeave(
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

    if (capture.video_frame.has_value())
    {
        ErrorHandling::Ensure(video_encoder_ != nullptr, "Pending diagnostic video frame has no encoder");
        video_encoder_->WriteFrame(std::move(source), bgra, *capture.video_frame);
    }
}

void DiagnosticRunner::ProcessCompletedFrame(size_t frame_in_flight)
{
    ErrorHandling::Ensure(frame_in_flight < pending_.size(), "Invalid diagnostic frame-in-flight index");
    for (PendingCapture& capture : pending_[frame_in_flight]) ProcessReadback(capture);
    pending_[frame_in_flight].clear();
}

void DiagnosticRunner::ProcessAllCompleted()
{
    std::vector<PendingCapture> remaining;
    for (auto& pending_frame : pending_)
    {
        std::ranges::move(pending_frame, std::back_inserter(remaining));
        pending_frame.clear();
    }
    std::ranges::stable_sort(
        remaining,
        [](const PendingCapture& lhs, const PendingCapture& rhs)
        {
            if (!lhs.video_frame.has_value()) return false;
            if (!rhs.video_frame.has_value()) return true;
            return *lhs.video_frame < *rhs.video_frame;
        });
    for (PendingCapture& capture : remaining) ProcessReadback(capture);
    if (video_encoder_) video_encoder_->Finish();
}

void DiagnosticRunner::RecordCheckpoint(u64 frame, std::span<const std::byte> pixels)
{
    const DiagnosticCheckpoint checkpoint{.frame = frame, .hash = HashPixels(pixels)};
    checkpoints_.push_back(checkpoint);

    const auto expected = std::ranges::find(expected_checkpoints_, frame, &DiagnosticCheckpoint::frame);
    if (expected == std::ranges::end(expected_checkpoints_)) return;
    if (expected->hash == checkpoint.hash) return;
    // Report the earliest disagreement: later ones are usually consequences of it.
    if (first_divergence_.has_value() && first_divergence_->frame <= frame) return;
    first_divergence_ = checkpoint;
}

std::optional<std::filesystem::path> DiagnosticRunner::TakeDialogAnswer()
{
    ErrorHandling::Ensure(
        next_dialog_ != dialogs_.size(),
        "Diagnostic replay opened {} file dialog{} but the recording answered only {}",
        next_dialog_ + 1,
        next_dialog_ == 0 ? "" : "s",
        dialogs_.size());

    return dialogs_[next_dialog_++].answer;
}

void DiagnosticRunner::EnsureComplete() const
{
    ErrorHandling::Ensure(
        applied_input_count_ == input_count_,
        "Diagnostic run ended before {} scheduled input event{} could be applied",
        input_count_ - applied_input_count_,
        input_count_ - applied_input_count_ == 1 ? "" : "s");
    ErrorHandling::Ensure(
        next_dialog_ == dialogs_.size(),
        "Diagnostic run ended with {} recorded dialog answer{} unused",
        dialogs_.size() - next_dialog_,
        dialogs_.size() - next_dialog_ == 1 ? "" : "s");
    const auto missing =
        static_cast<size_t>(std::ranges::count_if(captures_, [](const Capture& capture) { return !capture.recorded; }));
    ErrorHandling::Ensure(
        missing == 0,
        "Diagnostic run ended before {} requested capture{} could be recorded",
        missing,
        missing == 1 ? "" : "s");

    if (first_divergence_.has_value())
    {
        const auto expected =
            std::ranges::find(expected_checkpoints_, first_divergence_->frame, &DiagnosticCheckpoint::frame);
        ErrorHandling::ThrowWithMessage(
            "Diagnostic replay diverged at frame {}: expected checkpoint hash {}, got {}",
            first_divergence_->frame,
            expected == std::ranges::end(expected_checkpoints_) ? 0 : expected->hash,
            first_divergence_->hash);
    }
}

}  // namespace klvk
