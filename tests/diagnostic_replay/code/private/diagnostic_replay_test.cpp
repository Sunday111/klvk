#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <fmt/core.h>
#include <imgui.h>

extern "C"
{
#include <libavformat/avformat.h>
}

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "application_frame_clock.hpp"
#include "application_imgui.hpp"
#include "diagnostic_test_support.hpp"
#include "diagnostics/diagnostic_replay_scheduler.hpp"
#include "diagnostics/diagnostic_video_recorder.hpp"
#include "diagnostics/input_recorder.hpp"
#include "edt/functional/on_scope_leave.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/event_listener.hpp"
#include "klvk/filesystem/filesystem.hpp"

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

void TestDiagnosticOutputWrites()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("klvk_diagnostic_output_test_" + std::to_string(nonce));
    std::filesystem::create_directories(root);
    auto cleanup = edt::OnScopeLeave([&] { std::filesystem::remove_all(root); });

    const std::filesystem::path path = root / "checkpoints.json";
    klvk::Filesystem::WriteFile(path, "previous checkpoint contents");
    constexpr std::string_view contents = "{\"checkpoints\": []}";
    klvk::Filesystem::WriteFile(path, contents);
    std::string written;
    klvk::Filesystem::ReadFile(path, written);
    Ensure(written == contents, "diagnostic output was not flushed or truncated");
    EnsureThrows(
        [&] { klvk::Filesystem::WriteFile(root / "missing" / "recording.json", contents); },
        "an output open failure was ignored");

#if defined(__linux__)
    Ensure(std::filesystem::exists("/dev/full"), "the full-device fixture is unavailable");
    EnsureThrows(
        [&] { klvk::Filesystem::WriteFile("/dev/full", contents); },
        "a buffered output failure during close was ignored");
    EnsureThrows(
        [] { klvk::Filesystem::WriteFile("/dev/full", std::string(16'384, 'x')); },
        "a large output write failure was ignored");
#endif
}

void TestRecordedFrameClock()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path =
        std::filesystem::temp_directory_path() / ("klvk_recorded_clock_" + std::to_string(nonce) + ".json");
    auto cleanup = edt::OnScopeLeave([&] { std::filesystem::remove(path); });
    klvk::events::EventManager events;
    klvk::DiagnosticInputRecorder recorder(path, events);
    std::vector<u64> durations(120, 8'333'333);
    durations.push_back(20'000'001);
    durations.push_back(42'000'000);
    durations.push_back(5'000'000);
    klvk::ApplicationFrameClock live_clock;
    klvk::ApplicationFrameClock::TimePoint frame_start{std::chrono::seconds{1}};
    live_clock.Initialize(std::nullopt, {}, frame_start);
    std::vector<float> live_deltas;
    float recorded_distance = 0.f;
    for (size_t frame = 0; frame != durations.size(); ++frame)
    {
        recorder.BeginFrame(frame + 1);
        frame_start += std::chrono::nanoseconds{durations[frame]};
        live_clock.RegisterFrameStart(false, frame_start);
        live_deltas.push_back(live_clock.GetLastFrameDurationSeconds());
        recorder.RecordFrameDuration(live_clock.GetLastFrameDurationNanoseconds(), live_deltas.back());
        if (frame == 19) events.Emit(klvk::events::OnKey{.key = klvk::Key::W, .action = klvk::InputAction::Press});
        recorded_distance += live_deltas.back();
    }
    recorder.Write({320, 240}, std::nullopt, nlohmann::json::object(), path.parent_path());
    const auto config = klvk::LoadDiagnosticRunConfig(path, path.parent_path());
    Ensure(!config.clock.fixed_step_ns.has_value(), "recording substituted a fixed simulation clock");
    Ensure(config.input.size() == 1 && config.input.front().frame == 20, "recording changed input frame association");
    Ensure(config.clock.frame_durations_ns == durations, "recording did not preserve measured frame durations");
    klvk::ApplicationFrameClock clock;
    clock.Initialize(config.clock.fixed_step_ns, config.clock.frame_durations_ns);
    u64 elapsed = 0;
    float replayed_distance = 0.f;
    for (size_t frame = 0; frame != durations.size(); ++frame)
    {
        clock.RegisterFrameStart();
        elapsed += durations[frame];
        Ensure(
            std::bit_cast<u32>(clock.GetLastFrameDurationSeconds()) == std::bit_cast<u32>(live_deltas[frame]),
            "replay changed the live simulation delta bits");
        Ensure(
            std::bit_cast<u32>(config.clock.imgui_frame_durations_seconds[frame]) ==
                std::bit_cast<u32>(live_deltas[frame]),
            "recording changed the ImGui delta bits");
        replayed_distance += clock.GetLastFrameDurationSeconds();
        Ensure(clock.GetLastFrameDurationNanoseconds() == durations[frame], "replay changed a frame duration");
        Ensure(clock.GetElapsedTime(frame).count() == elapsed, "recorded logical time did not accumulate durations");
        Ensure(
            std::abs(clock.GetCurrentFrameStartTime(frame) - clock.GetRelativeTimeSeconds(frame)) < 0.000'001f,
            "recorded frame timestamp used wall time");
    }
    Ensure(
        std::bit_cast<u32>(replayed_distance) == std::bit_cast<u32>(recorded_distance),
        "replayed delta-time movement diverged from recording");
    clock.RegisterFrameStart();
    Ensure(
        clock.GetLastFrameDurationNanoseconds() == durations.back(),
        "extended replay did not retain its final duration");
    clock.Initialize(20'000'000);
    clock.RegisterFrameStart();
    Ensure(clock.GetLastFrameDurationNanoseconds() == 20'000'000, "fixed clock did not survive recorded-clock reset");
    Ensure(clock.GetElapsedTime(3).count() == 60'000'000, "fixed clock logical time changed");
    recorder.Write({320, 240}, 20'000'000, nlohmann::json::object(), path.parent_path());
    const auto fixed_config = klvk::LoadDiagnosticRunConfig(path, path.parent_path());
    Ensure(
        fixed_config.clock.fixed_step_ns == 20'000'000 && fixed_config.clock.frame_durations_ns.empty() &&
            fixed_config.clock.imgui_frame_durations_seconds.empty(),
        "fixed recording retained variable frame durations");
    auto legacy_document = klvk::DiagnosticRunConfigToJson(config);
    legacy_document["clock"].erase("imgui_frame_durations_seconds");
    klvk::Filesystem::WriteFile(path, legacy_document.dump());
    const auto legacy_config = klvk::LoadDiagnosticRunConfig(path, path.parent_path());
    Ensure(
        legacy_config.clock.frame_durations_ns == durations &&
            legacy_config.clock.imgui_frame_durations_seconds.empty(),
        "recorded clock without separate ImGui durations was rejected");
    for (const auto& invalid : std::vector<nlohmann::json>{
             {{"mode", "recorded"}, {"frame_durations_ns", nlohmann::json::array()}},
             {{"mode", "recorded"}, {"frame_durations_ns", {0}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {-1}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {1.5}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {std::numeric_limits<u64>::max(), 1}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {10}}, {"step_ns", 10}},
             {{"mode", "fixed"}, {"frame_durations_ns", {10}}, {"step_ns", 10}},
             {{"mode", "recorded"}, {"frame_durations_ns", {10}}, {"imgui_frame_durations_seconds", {0}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {10}}, {"imgui_frame_durations_seconds", {-1}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {10}}, {"imgui_frame_durations_seconds", {1e100}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {10}}, {"imgui_frame_durations_seconds", {1e-100}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {10}}, {"imgui_frame_durations_seconds", {nullptr}}},
             {{"mode", "recorded"}, {"frame_durations_ns", {10, 20}}, {"imgui_frame_durations_seconds", {0.01}}},
             {{"mode", "recorded"},
              {"frame_durations_ns", {10}},
              {"imgui_frame_durations_seconds", nlohmann::json::array()}},
             {{"mode", "fixed"}, {"step_ns", 10}, {"imgui_frame_durations_seconds", {0.01}}}})
    {
        auto document = klvk::DiagnosticRunConfigToJson(config);
        document["clock"] = invalid;
        klvk::Filesystem::WriteFile(path, document.dump());
        EnsureThrows(
            [&] { (void)klvk::LoadDiagnosticRunConfig(path, path.parent_path()); },
            "invalid recorded clock was accepted");
    }
}

void TestRecordedFrameStartDeadlines()
{
    using namespace std::chrono_literals;
    using TimePoint = klvk::ApplicationFrameClock::TimePoint;
    const std::array<u64, 3> durations{20'000'000, 200'000'000, 20'000'000};
    const std::array deadlines{20ms, 220ms, 240ms, 260ms};
    klvk::ApplicationFrameClock clock;
    clock.Initialize(std::nullopt, durations, TimePoint{1s});
    for (auto offset : deadlines)
    {
        const auto deadline = clock.GetNextRecordedFrameStartDeadline();
        Ensure(deadline == TimePoint{1s + offset}, "recorded pacing targeted the wrong frame timestamp");
        clock.RegisterFrameStart(false, *deadline);
        Ensure(
            clock.GetElapsedTime(0) == std::chrono::duration_cast<klvk::TimerDuration>(offset),
            "pacing deadline did not match the frame being rendered");
    }
    clock.Initialize(std::nullopt, durations, TimePoint::max() - 10ms);
    Ensure(!clock.GetNextRecordedFrameStartDeadline(), "recorded pacing overflowed the clock range");
    clock.Initialize(20'000'000);
    Ensure(!clock.GetNextRecordedFrameStartDeadline(), "fixed clock acquired recorded frame pacing");
    clock.Initialize(std::nullopt);
    Ensure(!clock.GetNextRecordedFrameStartDeadline(), "ordinary clock acquired recorded frame pacing");
}

void TestRecordedImGuiTiming()
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    Ensure(glfwInit() == GLFW_TRUE, "failed to initialize headless GLFW");
    auto terminate_glfw = edt::OnScopeLeave([] { glfwTerminate(); });
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(320, 240, "ImGui timing test", nullptr, nullptr);
    Ensure(window != nullptr, "failed to create a headless GLFW window");
    auto destroy_window = edt::OnScopeLeave([&] { glfwDestroyWindow(window); });
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() / ("klvk_imgui_clock_" + std::to_string(nonce) + ".json");
    auto cleanup = edt::OnScopeLeave([&] { std::filesystem::remove(path); });
    klvk::events::EventManager events;
    klvk::DiagnosticInputRecorder recorder(path, events);
    std::array<float, 3> live_durations{};
    for (bool replay : {false, true})
    {
        ImGui::CreateContext();
        auto destroy_imgui = edt::OnScopeLeave([] { ImGui::DestroyContext(); });
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.Fonts->AddFontDefault();
        Ensure(io.Fonts->Build(), "failed to build the ImGui timing font atlas");
        Ensure(ImGui_ImplGlfw_InitForVulkan(window, false), "failed to initialize headless ImGui input");
        auto shutdown_backend = edt::OnScopeLeave([] { ImGui_ImplGlfw_Shutdown(); });
        const auto config =
            replay ? klvk::LoadDiagnosticRunConfig(path, path.parent_path()) : klvk::DiagnosticRunConfig{};
        constexpr std::array times{1.0, 1.016666667, 1.6};
        for (size_t frame = 0; frame != times.size(); ++frame)
        {
            glfwSetTime(times[frame]);
            ImGui_ImplGlfw_NewFrame();
            io.AddFocusEvent(true);
            io.AddMousePosEvent(100.f, 100.f);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, frame != 1);
            const float duration = klvk::ApplicationImGui::BeginFrame(
                replay ? std::optional<u64>{16'666'667} : std::nullopt,
                replay ? std::optional<float>{config.clock.imgui_frame_durations_seconds[frame]} : std::nullopt);
            if (replay)
            {
                Ensure(
                    std::bit_cast<u32>(duration) == std::bit_cast<u32>(live_durations[frame]),
                    "replay changed the ImGui interaction delta");
            }
            else
            {
                live_durations[frame] = duration;
                recorder.BeginFrame(frame + 1);
                recorder.RecordFrameDuration(16'666'667, duration);
            }
            if (frame == 2)
            {
                Ensure(ImGui::IsMouseClicked(ImGuiMouseButton_Left), "timing test lost the second click");
                Ensure(
                    !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left),
                    "replay turned separate clicks into a double click");
            }
            ImGui::EndFrame();
        }
        if (!replay) recorder.Write({320, 240}, std::nullopt, nlohmann::json::object(), path.parent_path());
        const float fixed_duration = klvk::ApplicationImGui::BeginFrame(20'000'000);
        Ensure(
            std::bit_cast<u32>(fixed_duration) ==
                std::bit_cast<u32>(klvk::TimerDurationToSeconds(klvk::TimerDuration{20'000'000})),
            "fixed-clock ImGui timing changed");
        ImGui::EndFrame();
    }
}

void Run()
{
    TestRecordedFrameClock();
    TestRecordedFrameStartDeadlines();
    TestRecordedImGuiTiming();
    TestFramePhasesAndCompletion();
    TestTimeCatchUpAndAfterLastCapture();
    TestCheckpointCapturePlan();
    TestCaptureBatchStateTransitions();
    TestDisabledVideoRecording();
    TestVideoRecording();
    TestDiagnosticOutputWrites();
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
