#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/events/event_listener_interface.hpp"
#include "klvk/events/keyboard_events.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/integral_aliases.hpp"

namespace klvk
{

namespace events
{
class EventManager;
}

// Records real input into a diagnostic configuration that replays through
// --klvk-diagnostics. It listens to the same four window entry points the replay
// path writes to, so recording and replay share one vocabulary by construction.
//
// Events are pinned to the one-based frame they arrived on rather than to a
// timestamp: a frame trigger reproduces the original input-to-frame association
// whatever clock the replay runs at, while a wall-clock timestamp recorded at a
// variable frame rate would land on a different frame under a fixed step.
class DiagnosticInputRecorder
{
public:
    DiagnosticInputRecorder(std::filesystem::path path, events::EventManager& event_manager);
    DiagnosticInputRecorder(const DiagnosticInputRecorder&) = delete;
    DiagnosticInputRecorder(DiagnosticInputRecorder&&) = delete;
    ~DiagnosticInputRecorder();

    DiagnosticInputRecorder& operator=(const DiagnosticInputRecorder&) = delete;
    DiagnosticInputRecorder& operator=(DiagnosticInputRecorder&&) = delete;

    // Input arriving from now on belongs to this one-based frame.
    void BeginFrame(u64 frame) noexcept;

    // Writes the recording. framebuffer_size is enforced on replay, so it must be
    // the size the events were produced against. application is carried through
    // unchanged so a replayed run sees the configuration the recorded one did.
    void Write(
        edt::Vec2<u32> framebuffer_size,
        u64 fixed_step_ns,
        const nlohmann::json& application,
        const std::filesystem::path& executable_directory) const;

    [[nodiscard]] size_t GetRecordedEventCount() const noexcept { return input_.size(); }

private:
    void OnMouseMove(const events::OnMouseMove& event);
    void OnMouseButton(const events::OnMouseButton& event);
    void OnMouseScroll(const events::OnMouseScroll& event);
    void OnKey(const events::OnKey& event);

    void Append(DiagnosticInputEvent event);

    std::filesystem::path path_;
    events::EventManager& event_manager_;
    std::unique_ptr<events::IEventListener> event_listener_;
    std::vector<DiagnosticInputConfig> input_;
    std::optional<edt::Vec2f> last_recorded_position_;
    u64 current_frame_ = 1;
};

}  // namespace klvk
