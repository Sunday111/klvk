#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "diagnostic_events.hpp"
#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/events/event_listener_interface.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/timing/timer_manager.hpp"

namespace klvk
{

struct DiagnosticCaptureBatch
{
    std::vector<size_t> capture_indices;
    std::vector<std::filesystem::path> paths;
    std::optional<u64> checkpoint_frame;
    bool include_ui = true;

    [[nodiscard]] bool Empty() const noexcept { return capture_indices.empty(); }
};

class DiagnosticReplayScheduler
{
public:
    using InputCallback = std::function<void(const DiagnosticInputEvent&)>;

    DiagnosticReplayScheduler(
        const DiagnosticRunConfig& config,
        events::EventManager& event_manager,
        InputCallback input_callback);
    ~DiagnosticReplayScheduler();

    DiagnosticReplayScheduler(const DiagnosticReplayScheduler&) = delete;
    DiagnosticReplayScheduler(DiagnosticReplayScheduler&&) = delete;
    DiagnosticReplayScheduler& operator=(const DiagnosticReplayScheduler&) = delete;
    DiagnosticReplayScheduler& operator=(DiagnosticReplayScheduler&&) = delete;

    void Advance(u64 frame, TimerDuration elapsed);
    void AdvanceInput(u64 frame, TimerDuration elapsed);

    [[nodiscard]] bool HasCaptureDue(bool include_ui) const noexcept;
    [[nodiscard]] DiagnosticCaptureBatch GetCaptureBatch(bool include_ui) const;
    void MarkCaptured(const DiagnosticCaptureBatch& batch);

    [[nodiscard]] bool AnswersDialogs() const noexcept { return !dialogs_.empty(); }
    [[nodiscard]] std::optional<std::filesystem::path> TakeDialogAnswer();

    void EnsureComplete() const;

private:
    struct Capture
    {
        DiagnosticCaptureConfig config;
        std::optional<u64> checkpoint_frame;
        bool queued = false;
        bool recorded = false;
    };

    void OnCaptureDue(const events::DiagnosticCaptureDue& event);
    void ScheduleCapture(size_t capture_index, bool quit_after_last_capture);
    void ScheduleInput(const DiagnosticInputConfig& input);
    void ScheduleQuit(const DiagnosticExitConfig& exit);

    std::vector<Capture> captures_;
    std::vector<DiagnosticDialogConfig> dialogs_;
    std::vector<size_t> queued_without_ui_;
    std::vector<size_t> queued_with_ui_;
    TimerManager timers_;
    TimerManager input_timers_;
    events::EventManager& event_manager_;
    InputCallback input_callback_;
    std::unique_ptr<events::IEventListener> event_listener_;
    events::EventSubscription event_subscription_;
    size_t next_dialog_ = 0;
    size_t triggered_capture_count_ = 0;
    size_t input_count_ = 0;
    size_t applied_input_count_ = 0;
};

}  // namespace klvk
