#include "diagnostic_replay_scheduler.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "klvk/error_handling.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/event_listener_method.hpp"

namespace klvk
{

DiagnosticReplayScheduler::DiagnosticReplayScheduler(
    const DiagnosticRunConfig& config,
    events::EventManager& event_manager,
    InputCallback input_callback)
    : dialogs_(config.dialogs),
      event_manager_(event_manager),
      input_callback_(std::move(input_callback)),
      input_count_(config.input.size())
{
    captures_.reserve(config.captures.size());
    queued_without_ui_.reserve(config.captures.size());
    queued_with_ui_.reserve(config.captures.size());
    for (const DiagnosticCaptureConfig& capture : config.captures) captures_.push_back({.config = capture});
    if (config.checkpoints.has_value())
    {
        ErrorHandling::Ensure(config.exit.frame.has_value(), "Diagnostic checkpoints were not validated");
        const u64 every = config.checkpoints->every_frames;
        for (u64 frame = every; frame <= *config.exit.frame; frame += every)
        {
            captures_.push_back(
                {.config = {.frame = frame, .include_ui = config.checkpoints->include_ui}, .checkpoint_frame = frame});
            if (frame > std::numeric_limits<u64>::max() - every) break;
        }
    }

    event_listener_ = events::EventListenerMethodCallbacks<&DiagnosticReplayScheduler::OnCaptureDue>::CreatePtr(this);
    event_subscription_ = event_manager_.AddEventListener(*event_listener_);

    for (size_t capture_index = 0; capture_index != captures_.size(); ++capture_index)
    {
        ScheduleCapture(capture_index, config.exit.after_last_capture);
    }
    for (const DiagnosticInputConfig& input : config.input) ScheduleInput(input);
    ScheduleQuit(config.exit);
}

DiagnosticReplayScheduler::~DiagnosticReplayScheduler()
{
    timers_.Clear();
    input_timers_.Clear();
}

void DiagnosticReplayScheduler::ScheduleInput(const DiagnosticInputConfig& input)
{
    const auto callback = [this, event = input.event](const TimerEvent&)
    {
        input_callback_(event);
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

void DiagnosticReplayScheduler::ScheduleCapture(size_t capture_index, bool quit_after_last_capture)
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

void DiagnosticReplayScheduler::ScheduleQuit(const DiagnosticExitConfig& exit)
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

void DiagnosticReplayScheduler::OnCaptureDue(const events::DiagnosticCaptureDue& event)
{
    ErrorHandling::Ensure(event.capture_index < captures_.size(), "Invalid due diagnostic capture index");
    Capture& capture = captures_[event.capture_index];
    ErrorHandling::Ensure(!capture.queued && !capture.recorded, "Diagnostic capture was triggered more than once");
    auto& queue = capture.config.include_ui ? queued_with_ui_ : queued_without_ui_;
    queue.push_back(event.capture_index);
    capture.queued = true;
    ++triggered_capture_count_;
}

void DiagnosticReplayScheduler::Advance(u64 frame, TimerDuration elapsed)
{
    [[maybe_unused]] const u64 callback_count = timers_.Advance(elapsed, frame, std::numeric_limits<u64>::max());
}

void DiagnosticReplayScheduler::AdvanceInput(u64 frame, TimerDuration elapsed)
{
    [[maybe_unused]] const u64 callback_count = input_timers_.Advance(elapsed, frame, std::numeric_limits<u64>::max());
}

bool DiagnosticReplayScheduler::HasCaptureDue(bool include_ui) const noexcept
{
    return !(include_ui ? queued_with_ui_ : queued_without_ui_).empty();
}

DiagnosticCaptureBatch DiagnosticReplayScheduler::GetCaptureBatch(bool include_ui) const
{
    const auto& queue = include_ui ? queued_with_ui_ : queued_without_ui_;
    DiagnosticCaptureBatch batch{.capture_indices = queue, .include_ui = include_ui};
    batch.paths.reserve(queue.size());
    for (size_t capture_index : queue)
    {
        ErrorHandling::Ensure(capture_index < captures_.size(), "Invalid queued diagnostic capture index");
        const Capture& capture = captures_[capture_index];
        ErrorHandling::Ensure(
            capture.queued && !capture.recorded && capture.config.include_ui == include_ui,
            "Diagnostic capture queue is corrupt");
        if (capture.checkpoint_frame.has_value())
        {
            ErrorHandling::Ensure(!batch.checkpoint_frame.has_value(), "Two diagnostic checkpoints share one frame");
            batch.checkpoint_frame = capture.checkpoint_frame;
        }
        else
        {
            batch.paths.push_back(capture.config.path);
        }
    }
    return batch;
}

void DiagnosticReplayScheduler::MarkCaptured(const DiagnosticCaptureBatch& batch)
{
    auto& queue = batch.include_ui ? queued_with_ui_ : queued_without_ui_;
    ErrorHandling::Ensure(queue == batch.capture_indices, "Diagnostic capture batch no longer matches its queue");
    for (size_t capture_index : queue)
    {
        Capture& capture = captures_[capture_index];
        ErrorHandling::Ensure(capture.queued && !capture.recorded, "Diagnostic capture state is corrupt");
        capture.queued = false;
        capture.recorded = true;
    }
    queue.clear();
}

std::optional<std::filesystem::path> DiagnosticReplayScheduler::TakeDialogAnswer()
{
    ErrorHandling::Ensure(
        next_dialog_ != dialogs_.size(),
        "Diagnostic replay opened {} file dialog{} but the recording answered only {}",
        next_dialog_ + 1,
        next_dialog_ == 0 ? "" : "s",
        dialogs_.size());
    return dialogs_[next_dialog_++].answer;
}

void DiagnosticReplayScheduler::EnsureComplete() const
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
}

}  // namespace klvk
