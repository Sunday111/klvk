#include "input_recorder.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "klvk/error_handling.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/filesystem/filesystem.hpp"

namespace klvk
{

DiagnosticInputRecorder::DiagnosticInputRecorder(std::filesystem::path path, events::EventManager& event_manager)
    : path_(std::move(path)),
      event_manager_(event_manager)
{
    ErrorHandling::Ensure(!path_.empty(), "Diagnostic input recording requires a non-empty path");
    event_listener_ = events::EventListenerMethodCallbacks<
        &DiagnosticInputRecorder::OnMouseMove,
        &DiagnosticInputRecorder::OnMouseButton,
        &DiagnosticInputRecorder::OnMouseScroll,
        &DiagnosticInputRecorder::OnKey>::CreatePtr(this);
    event_manager_.AddEventListener(*event_listener_);
}

DiagnosticInputRecorder::~DiagnosticInputRecorder()
{
    if (event_listener_) event_manager_.RemoveListener(event_listener_.get());
}

void DiagnosticInputRecorder::BeginFrame(u64 frame) noexcept
{
    current_frame_ = frame;
}

void DiagnosticInputRecorder::Append(DiagnosticInputEvent event)
{
    input_.push_back(DiagnosticInputConfig{.frame = current_frame_, .time_ns = std::nullopt, .event = event});
}

void DiagnosticInputRecorder::OnMouseMove(const events::OnMouseMove& event)
{
    // A real session emits a cursor event per platform callback, far more than a
    // replay can apply: input is dispatched once per frame, so only the last
    // position of a frame is observable. Collapsing to that one position keeps
    // the recording faithful and keeps the file small.
    if (last_recorded_position_.has_value() && *last_recorded_position_ == event.current) return;
    last_recorded_position_ = event.current;

    if (!input_.empty() && input_.back().frame == current_frame_ &&
        std::holds_alternative<DiagnosticMouseMoveInput>(input_.back().event))
    {
        input_.back().event = DiagnosticMouseMoveInput{.position = event.current};
        return;
    }
    Append(DiagnosticMouseMoveInput{.position = event.current});
}

void DiagnosticInputRecorder::OnMouseButton(const events::OnMouseButton& event)
{
    Append(DiagnosticMouseButtonInput{.button = event.button, .action = event.action});
}

void DiagnosticInputRecorder::OnMouseScroll(const events::OnMouseScroll& event)
{
    Append(DiagnosticMouseScrollInput{.offset = event.value});
}

void DiagnosticInputRecorder::OnKey(const events::OnKey& event)
{
    Append(DiagnosticKeyInput{.key = event.key, .action = event.action});
}

void DiagnosticInputRecorder::Write(
    edt::Vec2<u32> framebuffer_size,
    u64 fixed_step_ns,
    const nlohmann::json& application,
    const std::filesystem::path& executable_directory) const
{
    DiagnosticRunConfig config;
    // Offscreen needs no display server, so a recording replays in CI unchanged.
    config.presentation = DiagnosticPresentation::Offscreen;
    config.framebuffer_size = framebuffer_size;
    config.clock.fixed_step_ns = fixed_step_ns;
    config.input = input_;
    config.application = application;
    // Replay the whole session, not just up to the last event: what a recording
    // is meant to reproduce often appears in the frames after the final input.
    // The floor keeps the last event's effect rendered even if the session ended
    // on the very frame it arrived.
    const u64 last_input_frame = input_.empty() ? 0 : *input_.back().frame;
    config.exit.frame = std::max(current_frame_, last_input_frame + 1);

    std::filesystem::path path = path_;
    if (path.is_relative()) path = executable_directory / path;
    path = path.lexically_normal();
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());

    const nlohmann::json document = DiagnosticRunConfigToJson(config);
    Filesystem::WriteFile(path, document.dump(2) + "\n");
}

}  // namespace klvk
