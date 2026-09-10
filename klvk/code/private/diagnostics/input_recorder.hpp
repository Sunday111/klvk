#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "edt/math/matrix.hpp"
#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/events/event_listener_interface.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/keyboard_events.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/integral_aliases.hpp"

namespace klvk
{

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
    void RecordFrameDuration(u64 duration_ns);

    // A file dialog the application put in front of the user, and what came back.
    // Nothing means it was dismissed. The answer is stored relative to the
    // executable directory when it lies inside it, so a recording of the staged
    // content replays from a different build tree.
    void RecordDialog(
        const std::optional<std::filesystem::path>& answer,
        const std::filesystem::path& executable_directory);

    // Writes the recording. framebuffer_size is enforced on replay, so it must be
    // the size the events were produced against. application is carried through
    // unchanged so a replayed run sees the configuration the recorded one did.
    void Write(
        edt::Vec2<u32> framebuffer_size,
        std::optional<u64> fixed_step_ns,
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
    events::EventSubscription event_subscription_;
    std::vector<DiagnosticInputConfig> input_;
    std::vector<u64> frame_durations_ns_;
    std::vector<DiagnosticDialogConfig> dialogs_;
    std::optional<edt::Vec2f> last_recorded_position_;
    u64 current_frame_ = 1;
};

}  // namespace klvk
