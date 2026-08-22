#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "diagnostic_framebuffer_readback.hpp"
#include "diagnostic_input_player.hpp"
#include "diagnostic_replay_scheduler.hpp"
#include "diagnostic_video_recorder.hpp"
#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/timing/timer_manager.hpp"

namespace klvk
{

class DeviceContext;
class Window;

class DiagnosticRunner
{
public:
    DiagnosticRunner(
        const DiagnosticRunConfig& config,
        size_t frames_in_flight,
        events::EventManager& event_manager,
        Window& window);
    ~DiagnosticRunner();

    void Advance(u64 frame, TimerDuration elapsed);
    void AdvanceInput(u64 frame, TimerDuration elapsed);
    [[nodiscard]] bool NeedsReadback(bool include_ui) const noexcept;

    bool RecordReadback(
        DeviceContext& context,
        vk::CommandBuffer command_buffer,
        size_t frame_in_flight,
        bool include_ui,
        vk::Image image,
        vk::Format format,
        vk::Extent2D extent,
        vk::ImageLayout final_layout);

    [[nodiscard]] const std::vector<DiagnosticCheckpoint>& GetCheckpoints() const noexcept;
    [[nodiscard]] std::optional<DiagnosticCheckpoint> GetFirstDivergence() const noexcept;

    void ProcessCompletedFrame(size_t frame_in_flight);
    void ProcessAllCompleted();
    void EnsureComplete() const;

    [[nodiscard]] bool AnswersDialogs() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> TakeDialogAnswer();

private:
    DiagnosticInputPlayer input_player_;
    DiagnosticReplayScheduler replay_;
    DiagnosticVideoRecorder video_;
    DiagnosticFramebufferReadback readback_;
};

}  // namespace klvk
