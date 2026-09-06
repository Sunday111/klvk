#include <fmt/core.h>

extern "C"
{
#include <libavformat/avformat.h>
}

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostic_test_support.hpp"
#include "diagnostics/diagnostic_replay_scheduler.hpp"
#include "diagnostics/diagnostic_video_recorder.hpp"
#include "edt/functional/on_scope_leave.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/event_listener.hpp"

namespace
{

using klvk::tests::Ensure;
using klvk::tests::EnsureThrows;

class QuitObserver
{
public:
    explicit QuitObserver(klvk::events::EventManager& events)
    {
        listener_ = klvk::events::EventListener<klvk::events::OnApplicationQuitRequested>::PtrFromFunctions(
            [this](const klvk::events::OnApplicationQuitRequested&) { ++count_; });
        subscription_ = events.AddEventListener(*listener_);
    }

    [[nodiscard]] size_t GetCount() const noexcept { return count_; }

private:
    std::unique_ptr<klvk::events::IEventListener> listener_;
    klvk::events::EventSubscription subscription_;
    size_t count_ = 0;
};

void TestFramePhasesAndCompletion()
{
    klvk::events::EventManager events;
    QuitObserver quit(events);
    klvk::DiagnosticRunConfig config;
    config.input = {{
        .frame = 1,
        .time_ns = std::nullopt,
        .event = klvk::DiagnosticKeyInput{.key = klvk::Key::W, .action = klvk::InputAction::Press},
    }};
    config.captures = {
        {.frame = 1, .time_ns = std::nullopt, .path = "before_ui.ppm", .include_ui = false},
        {.frame = 2, .time_ns = std::nullopt, .path = "after_ui.ppm", .include_ui = true}};
    config.dialogs = {{.frame = 1, .answer = "chosen.json"}, {.frame = 2, .answer = std::nullopt}};
    config.exit.frame = 3;

    std::vector<klvk::DiagnosticInputEvent> applied;
    klvk::DiagnosticReplayScheduler replay(
        config,
        events,
        [&](const klvk::DiagnosticInputEvent& input) { applied.push_back(input); });

    replay.AdvanceInput(1, klvk::TimerDuration::zero());
    Ensure(applied == std::vector{config.input.front().event}, "frame input was not applied before capture scheduling");
    Ensure(!replay.HasCaptureDue(false), "capture became due during the input phase");

    replay.Advance(1, klvk::TimerDuration::zero());
    Ensure(replay.HasCaptureDue(false), "before-UI capture did not become due");
    Ensure(!replay.HasCaptureDue(true), "after-UI capture became due too early");
    const klvk::DiagnosticCaptureBatch before_ui = replay.GetCaptureBatch(false);
    Ensure(before_ui.paths == std::vector<std::filesystem::path>{"before_ui.ppm"}, "wrong before-UI batch");
    EnsureThrows([&] { replay.EnsureComplete(); }, "an incomplete replay was accepted");
    replay.MarkCaptured(before_ui);

    replay.Advance(2, klvk::TimerDuration::zero());
    const klvk::DiagnosticCaptureBatch after_ui = replay.GetCaptureBatch(true);
    Ensure(after_ui.paths == std::vector<std::filesystem::path>{"after_ui.ppm"}, "wrong after-UI batch");
    replay.MarkCaptured(after_ui);

    Ensure(replay.AnswersDialogs(), "recorded dialog answers were not exposed");
    Ensure(replay.TakeDialogAnswer() == std::filesystem::path("chosen.json"), "wrong recorded dialog answer");
    Ensure(!replay.TakeDialogAnswer().has_value(), "dismissed dialog gained an answer");
    EnsureThrows([&] { (void)replay.TakeDialogAnswer(); }, "dialog exhaustion was not detected");

    replay.Advance(3, klvk::TimerDuration::zero());
    Ensure(quit.GetCount() == 1, "frame exit did not request application shutdown");
    replay.EnsureComplete();
}

void TestTimeCatchUpAndAfterLastCapture()
{
    klvk::events::EventManager events;
    QuitObserver quit(events);
    klvk::DiagnosticRunConfig config;
    config.input = {
        {.frame = std::nullopt,
         .time_ns = 5,
         .event = klvk::DiagnosticKeyInput{.key = klvk::Key::A, .action = klvk::InputAction::Press}},
        {.frame = std::nullopt,
         .time_ns = 10,
         .event = klvk::DiagnosticKeyInput{.key = klvk::Key::B, .action = klvk::InputAction::Press}},
        {.frame = std::nullopt,
         .time_ns = 10,
         .event = klvk::DiagnosticKeyInput{.key = klvk::Key::C, .action = klvk::InputAction::Press}}};
    config.captures = {
        {.frame = std::nullopt, .time_ns = 5, .path = "first.ppm", .include_ui = false},
        {.frame = std::nullopt, .time_ns = 10, .path = "second.ppm", .include_ui = false}};
    config.exit.after_last_capture = true;

    std::vector<klvk::DiagnosticInputEvent> applied;
    klvk::DiagnosticReplayScheduler replay(
        config,
        events,
        [&](const klvk::DiagnosticInputEvent& input) { applied.push_back(input); });

    replay.AdvanceInput(1, klvk::TimerDuration{10});
    Ensure(
        applied == std::vector{config.input[0].event, config.input[1].event, config.input[2].event},
        "time catch-up changed input ordering");
    replay.Advance(1, klvk::TimerDuration{10});
    Ensure(quit.GetCount() == 1, "after-last-capture did not request shutdown after all triggers");
    const klvk::DiagnosticCaptureBatch batch = replay.GetCaptureBatch(false);
    Ensure(
        batch.paths == std::vector<std::filesystem::path>{"first.ppm", "second.ppm"},
        "time catch-up changed capture ordering");
    replay.MarkCaptured(batch);

    replay.AdvanceInput(2, klvk::TimerDuration{20});
    replay.Advance(2, klvk::TimerDuration{20});
    Ensure(applied.size() == 3 && quit.GetCount() == 1, "one-shot replay work ran more than once");
    replay.EnsureComplete();
}

void TestCheckpointCapturePlan()
{
    klvk::events::EventManager events;
    klvk::DiagnosticRunConfig config;
    config.captures = {{.frame = 2, .time_ns = std::nullopt, .path = "frame.ppm", .include_ui = false}};
    config.checkpoints = klvk::DiagnosticCheckpointConfig{.every_frames = 2, .include_ui = false, .expected = {}};
    config.exit.frame = 5;

    klvk::DiagnosticReplayScheduler replay(config, events, [](const klvk::DiagnosticInputEvent&) {});
    replay.Advance(2, klvk::TimerDuration::zero());
    const klvk::DiagnosticCaptureBatch frame_two = replay.GetCaptureBatch(false);
    Ensure(frame_two.capture_indices.size() == 2, "coincident capture and checkpoint were not coalesced");
    Ensure(frame_two.paths == std::vector<std::filesystem::path>{"frame.ppm"}, "checkpoint gained an output path");
    Ensure(frame_two.checkpoint_frame == 2, "wrong first checkpoint frame");
    replay.MarkCaptured(frame_two);

    replay.Advance(4, klvk::TimerDuration::zero());
    const klvk::DiagnosticCaptureBatch frame_four = replay.GetCaptureBatch(false);
    Ensure(frame_four.paths.empty() && frame_four.checkpoint_frame == 4, "wrong second checkpoint batch");
    replay.MarkCaptured(frame_four);
    replay.EnsureComplete();
}

void TestCaptureBatchStateTransitions()
{
    klvk::events::EventManager events;
    klvk::DiagnosticRunConfig config;
    config.captures = {
        {.frame = 1, .time_ns = std::nullopt, .path = "first.ppm", .include_ui = false},
        {.frame = 2, .time_ns = std::nullopt, .path = "second.ppm", .include_ui = false}};
    config.exit.frame = 3;

    klvk::DiagnosticReplayScheduler replay(config, events, [](const klvk::DiagnosticInputEvent&) {});
    replay.Advance(1, klvk::TimerDuration::zero());
    const klvk::DiagnosticCaptureBatch stale = replay.GetCaptureBatch(false);
    replay.Advance(2, klvk::TimerDuration::zero());
    EnsureThrows([&] { replay.MarkCaptured(stale); }, "a stale capture batch was accepted");

    const klvk::DiagnosticCaptureBatch current = replay.GetCaptureBatch(false);
    Ensure(
        current.paths == std::vector<std::filesystem::path>{"first.ppm", "second.ppm"},
        "the current capture batch lost queued work");
    replay.MarkCaptured(current);
    Ensure(!replay.HasCaptureDue(false), "a recorded capture remained queued");
    EnsureThrows([&] { replay.MarkCaptured(current); }, "a capture batch was recorded twice");
    replay.EnsureComplete();
}

void TestDisabledVideoRecording()
{
    const klvk::DiagnosticRunConfig config;
    klvk::DiagnosticVideoRecorder recorder(config);
    Ensure(!recorder.NeedsFrame(false) && !recorder.NeedsFrame(true), "an unconfigured video requested frames");
    Ensure(!recorder.ReserveFrame(false).has_value(), "an unconfigured video reserved a frame");
    Ensure(!recorder.ReserveFrame(true).has_value(), "an unconfigured video reserved an after-UI frame");
    recorder.Finish();
    EnsureThrows([&] { recorder.WriteFrame({}, false, 0); }, "an unconfigured video recorder accepted pixel data");
}

void TestVideoRecording()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("klvk_diagnostic_video_test_" + std::to_string(nonce));
    std::filesystem::create_directories(root);
    auto cleanup = edt::OnScopeLeave([&] { std::filesystem::remove_all(root); });

    klvk::DiagnosticRunConfig config;
    config.framebuffer_size = edt::Vec2<u32>{4, 4};
    config.clock.fixed_step_ns = 20'000'000;
    config.video = klvk::DiagnosticVideoConfig{
        .path = root / "recording.mp4",
        .encoding = klvk::DiagnosticVideoEncoding::Mpeg4,
        .encoding_device = klvk::DiagnosticVideoEncodingDevice::Cpu,
        .compression_level = 3,
        .include_ui = false,
        .log_ffmpeg = false};

    klvk::DiagnosticVideoRecorder recorder(config);
    Ensure(recorder.NeedsFrame(false), "configured video stage was not requested");
    Ensure(!recorder.NeedsFrame(true), "video requested the wrong UI stage");
    for (u64 frame_index = 0; frame_index != 3; ++frame_index)
    {
        const std::optional<u64> reserved = recorder.ReserveFrame(false);
        Ensure(reserved == frame_index, "video frame sequence was not contiguous");
        std::vector<std::byte> pixels(4 * 4 * 4, static_cast<std::byte>(frame_index * 40));
        for (size_t pixel = 0; pixel != 16; ++pixel) pixels[pixel * 4 + 3] = std::byte{255};
        recorder.WriteFrame(std::move(pixels), false, *reserved);
    }
    recorder.Finish();
    recorder.Finish();

    AVFormatContext* format = nullptr;
    Ensure(
        avformat_open_input(&format, config.video->path.string().c_str(), nullptr, nullptr) >= 0,
        "recorded video could not be opened");
    auto close_format = edt::OnScopeLeave([&] { avformat_close_input(&format); });
    Ensure(avformat_find_stream_info(format, nullptr) >= 0, "recorded video stream could not be inspected");

    int video_stream = -1;
    for (unsigned int index = 0; index != format->nb_streams; ++index)
    {
        if (format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream = static_cast<int>(index);
            break;
        }
    }
    Ensure(video_stream >= 0, "recorded file has no video stream");
    const AVCodecParameters& parameters = *format->streams[static_cast<size_t>(video_stream)]->codecpar;
    Ensure(parameters.codec_id == AV_CODEC_ID_MPEG4, "recorded video uses the wrong codec");
    Ensure(parameters.width == 4 && parameters.height == 4, "recorded video has the wrong dimensions");

    AVPacket* packet = av_packet_alloc();
    Ensure(packet != nullptr, "FFmpeg packet allocation failed");
    auto free_packet = edt::OnScopeLeave([&] { av_packet_free(&packet); });
    size_t frame_packets = 0;
    while (av_read_frame(format, packet) >= 0)
    {
        if (packet->stream_index == video_stream) ++frame_packets;
        av_packet_unref(packet);
    }
    Ensure(frame_packets == 3, "recorded video has the wrong frame count");
}

void Run()
{
    TestFramePhasesAndCompletion();
    TestTimeCatchUpAndAfterLastCapture();
    TestCheckpointCapturePlan();
    TestCaptureBatchStateTransitions();
    TestDisabledVideoRecording();
    TestVideoRecording();
    klvk::tests::RunDiagnosticFramebufferReadbackTests();
    klvk::tests::RunDiagnosticInputPlayerTests();
}

}  // namespace

int main()
{
    try
    {
        Run();
        fmt::println("diagnostic replay tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
}
