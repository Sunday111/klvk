#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "diagnostic_events.hpp"
#include "diagnostic_video_encoder.hpp"
#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/events/event_listener_interface.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"

namespace klvk
{

class DeviceContext;
class Window;
namespace events
{
class EventManager;
}

class DiagnosticRunner
{
public:
    DiagnosticRunner(
        const DiagnosticRunConfig& config,
        const std::filesystem::path& executable_directory,
        size_t frames_in_flight,
        events::EventManager& event_manager,
        Window& window);
    ~DiagnosticRunner();

    void Advance(u64 frame, TimerDuration elapsed);
    void AdvanceInput(u64 frame, TimerDuration elapsed);
    [[nodiscard]] bool NeedsReadback(bool include_ui) const noexcept;

    // The image must be in COLOR_ATTACHMENT_OPTIMAL. Returns true after recording
    // a copy and leaves it in final_layout.
    bool RecordReadback(
        DeviceContext& context,
        VkCommandBuffer command_buffer,
        size_t frame_in_flight,
        bool include_ui,
        VkImage image,
        VkFormat format,
        VkExtent2D extent,
        VkImageLayout final_layout);

    // Empty when the run requested no checkpoints. Ordered by frame.
    [[nodiscard]] const std::vector<DiagnosticCheckpoint>& GetCheckpoints() const noexcept { return checkpoints_; }

    // The first frame whose hash disagreed with the configuration's expectation,
    // or nullopt when nothing diverged (including when nothing was expected).
    [[nodiscard]] std::optional<DiagnosticCheckpoint> GetFirstDivergence() const noexcept { return first_divergence_; }

    void ProcessCompletedFrame(size_t frame_in_flight);
    void ProcessAllCompleted();
    void EnsureComplete() const;

private:
    struct Capture
    {
        DiagnosticCaptureConfig config;
        // A checkpoint is a capture whose pixels are hashed instead of written,
        // so it reuses the whole scheduling and readback path unchanged.
        std::optional<u64> checkpoint_frame;
        bool queued = false;
        bool recorded = false;
    };

    struct PendingCapture
    {
        GpuBuffer buffer;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        std::vector<std::filesystem::path> paths;
        std::optional<u64> video_frame;
        // Set when this readback exists to be hashed rather than written out.
        std::optional<u64> checkpoint_frame;
    };

    void OnCaptureDue(const events::DiagnosticCaptureDue& event);
    void ScheduleCapture(size_t capture_index, bool quit_after_last_capture);
    void ScheduleInput(const DiagnosticInputConfig& input);
    void ApplyInput(const DiagnosticInputEvent& input);
    void ApplyModifier(Key key);
    void ScheduleQuit(const DiagnosticExitConfig& exit);
    void ProcessReadback(PendingCapture& capture);

    void RecordCheckpoint(u64 frame, std::span<const std::byte> pixels);

    std::vector<Capture> captures_;
    std::vector<DiagnosticCheckpoint> checkpoints_;
    std::vector<DiagnosticCheckpoint> expected_checkpoints_;
    std::optional<DiagnosticCheckpoint> first_divergence_;
    std::vector<std::vector<PendingCapture>> pending_;
    std::vector<size_t> queued_without_ui_;
    std::vector<size_t> queued_with_ui_;
    TimerManager timers_;
    TimerManager input_timers_;
    events::EventManager& event_manager_;
    Window& window_;
    std::unique_ptr<events::IEventListener> event_listener_;
    std::unique_ptr<DiagnosticVideoEncoder> video_encoder_;
    bool video_includes_ui_ = true;
    u64 video_frame_count_ = 0;
    size_t triggered_capture_count_ = 0;
    size_t input_count_ = 0;
    size_t applied_input_count_ = 0;
};

}  // namespace klvk
