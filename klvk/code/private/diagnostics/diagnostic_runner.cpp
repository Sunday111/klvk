#include "diagnostic_runner.hpp"

namespace klvk
{

DiagnosticRunner::DiagnosticRunner(
    const DiagnosticRunConfig& config,
    size_t frames_in_flight,
    events::EventManager& event_manager,
    Window& window)
    : input_player_(window),
      replay_(config, event_manager, [this](const DiagnosticInputEvent& input) { input_player_.Apply(input); }),
      video_(config),
      readback_(frames_in_flight, config.checkpoints)
{
}

DiagnosticRunner::~DiagnosticRunner() = default;

void DiagnosticRunner::Advance(u64 frame, TimerDuration elapsed)
{
    replay_.Advance(frame, elapsed);
}

void DiagnosticRunner::AdvanceInput(u64 frame, TimerDuration elapsed)
{
    replay_.AdvanceInput(frame, elapsed);
}

bool DiagnosticRunner::NeedsReadback(bool include_ui) const noexcept
{
    return replay_.HasCaptureDue(include_ui) || video_.NeedsFrame(include_ui);
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
    const DiagnosticCaptureBatch captures = replay_.GetCaptureBatch(include_ui);
    const std::optional<u64> video_frame = video_.ReserveFrame(include_ui);
    const bool recorded = readback_.Record(
        context,
        command_buffer,
        frame_in_flight,
        image,
        format,
        extent,
        final_layout,
        captures.paths,
        captures.checkpoint_frame,
        video_frame);
    if (recorded) replay_.MarkCaptured(captures);
    return recorded;
}

const std::vector<DiagnosticCheckpoint>& DiagnosticRunner::GetCheckpoints() const noexcept
{
    return readback_.GetCheckpoints();
}

std::optional<DiagnosticCheckpoint> DiagnosticRunner::GetFirstDivergence() const noexcept
{
    return readback_.GetFirstDivergence();
}

void DiagnosticRunner::ProcessCompletedFrame(size_t frame_in_flight)
{
    readback_.ProcessCompletedFrame(frame_in_flight, video_);
}

void DiagnosticRunner::ProcessAllCompleted()
{
    readback_.ProcessAllCompleted(video_);
    video_.Finish();
}

void DiagnosticRunner::EnsureComplete() const
{
    replay_.EnsureComplete();
    readback_.EnsureComplete();
}

bool DiagnosticRunner::AnswersDialogs() const noexcept
{
    return replay_.AnswersDialogs();
}

std::optional<std::filesystem::path> DiagnosticRunner::TakeDialogAnswer()
{
    return replay_.TakeDialogAnswer();
}

}  // namespace klvk
