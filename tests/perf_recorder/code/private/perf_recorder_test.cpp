#include "klvk/diagnostics/perf_recorder.hpp"

#include <fmt/format.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "klvk/diagnostics/speedscope_exporter.hpp"
#include "klvk/platform/os/os.hpp"

namespace
{

#if defined(__linux__)

void Ensure(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string{message});
}

using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

[[nodiscard]] sigset_t InterruptSignals()
{
    sigset_t signals{};
    Ensure(sigemptyset(&signals) == 0, "Could not initialize the interrupt signal set");
    Ensure(sigaddset(&signals, SIGINT) == 0, "Could not add SIGINT to the signal set");
    return signals;
}

void BlockInterrupt()
{
    const sigset_t signals = InterruptSignals();
    Ensure(sigprocmask(SIG_BLOCK, &signals, nullptr) == 0, "Could not block SIGINT");
}

[[noreturn]] void ExitOnInterrupt()
{
    const sigset_t signals = InterruptSignals();
    int received_signal = 0;
    Ensure(sigwait(&signals, &received_signal) == 0, "Could not wait for SIGINT");
    Ensure(received_signal == SIGINT, "Received an unexpected signal");
    std::_Exit(0);
}

void IgnoreStopSignals()
{
    Ensure(std::signal(SIGINT, SIG_IGN) != SIG_ERR, "Could not ignore SIGINT");
    Ensure(std::signal(SIGTERM, SIG_IGN) != SIG_ERR, "Could not ignore SIGTERM");
}

[[nodiscard]] std::filesystem::path FindArgument(int argc, char** argv, std::string_view name)
{
    for (int index = 1; index + 1 < argc; ++index)
    {
        if (argv[index] == name) return argv[index + 1];
    }
    throw std::runtime_error("Missing fake perf argument");
}

int RunFakePerf(int argc, char** argv)
{
    const std::string_view command = argv[1];
    if (command == "record")
    {
        const std::filesystem::path frequency = FindArgument(argc, argv, "--freq");
        const bool ignore_stop_signals = frequency == "1";
        const bool trickle_acknowledgement = frequency == "2";
        std::ofstream(FindArgument(argc, argv, "--output")) << "fake perf data\n";
        const std::string control_argument = FindArgument(argc, argv, "--control").string();
        const size_t separator = control_argument.find(',');
        Ensure(control_argument.starts_with("fifo:") && separator != std::string::npos, "Invalid control FIFOs");
        File control{std::fopen(control_argument.substr(5, separator - 5).c_str(), "re"), &std::fclose};
        File acknowledge{std::fopen(control_argument.substr(separator + 1).c_str(), "we"), &std::fclose};
        Ensure(control != nullptr && acknowledge != nullptr, "Could not open control FIFOs");

        if (ignore_stop_signals)
        {
            IgnoreStopSignals();
        }
        else
        {
            BlockInterrupt();
            std::jthread(ExitOnInterrupt).detach();
        }
        std::string control_command;
        for (int character = std::fgetc(control.get()); character != EOF; character = std::fgetc(control.get()))
        {
            if (character != '\n')
            {
                control_command.push_back(static_cast<char>(character));
                continue;
            }
            fmt::println("control: {}", control_command);
            Ensure(std::fflush(stdout) == 0, "Could not flush fake perf log");
            if (trickle_acknowledgement)
            {
                for (size_t count = 0; count != 10; ++count)
                {
                    Ensure(std::fwrite("a", 1, 1, acknowledge.get()) == 1, "Could not write partial acknowledgement");
                    Ensure(std::fflush(acknowledge.get()) == 0, "Could not flush partial acknowledgement");
                    std::this_thread::sleep_for(std::chrono::milliseconds{200});
                }
                return 0;
            }
            Ensure(std::fwrite("ack\n", 1, 4, acknowledge.get()) == 4, "Could not acknowledge perf control command");
            Ensure(std::fflush(acknowledge.get()) == 0, "Could not flush perf control acknowledgement");
            control_command.clear();
        }
        return 0;
    }
    if (command == "script")
    {
        if (FindArgument(argc, argv, "--input").filename() == "cancel.data")
        {
            IgnoreStopSignals();
            for (;;)
            {
                fmt::println("test 1/1 [000] 1.000: cycles:");
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }
        fmt::println("test 1/1 [000] 1.000: cycles:");
        fmt::println("\t1 TestFunction (test)");
        return 0;
    }
    throw std::runtime_error("Unknown fake perf command");
}

void WaitForCaptureToStart(const klvk::PerfRecorder& recorder)
{
    for (size_t attempt = 0; attempt != 1'000; ++attempt)
    {
        const auto captures = recorder.GetCaptures();
        if (!captures.empty() && std::filesystem::exists(captures.back().data_path)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    throw std::runtime_error("Fake perf record did not start");
}

void WaitForStop(klvk::PerfRecorder& recorder)
{
    for (size_t attempt = 0; attempt != 1'000; ++attempt)
    {
        recorder.Update();
        if (!recorder.IsFinalizing()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    throw std::runtime_error("Perf record did not finish stopping");
}

[[nodiscard]] std::filesystem::path SpeedscopePath(const klvk::PerfRecorder::Capture& capture)
{
    std::filesystem::path result = capture.data_path;
    result.replace_extension(".linux-perf.txt");
    return result;
}

void TestRecorder()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output = std::filesystem::temp_directory_path() /
                        fmt::format("klvk-perf-recorder-test-{}-{}", klvk::os::GetProcessId(), unique);
    klvk::PerfRecorder recorder({
        .output_directory = output,
        .executable = std::filesystem::canonical("/proc/self/exe").string(),
    });

    Ensure(recorder.Start(), recorder.GetLastError());
    WaitForCaptureToStart(recorder);
    recorder.Pause();
    Ensure(recorder.IsPaused(), recorder.GetLastError());
    recorder.Resume();
    Ensure(recorder.IsRecording(), "Recorder did not resume");
    recorder.Stop();
    WaitForStop(recorder);
    Ensure(recorder.CanStart(), "Recorder did not become ready after stopping");
    const auto first_capture = recorder.GetCaptures().front();
    std::ifstream first_log(first_capture.log_path);
    const std::string controls{std::istreambuf_iterator<char>{first_log}, std::istreambuf_iterator<char>{}};
    Ensure(
        controls.contains("control: disable\ncontrol: enable\ncontrol: disable\n"),
        "Fake perf did not receive pause, resume, and stop controls in order");

    Ensure(recorder.Start(), recorder.GetLastError());
    WaitForCaptureToStart(recorder);
    recorder.Finish();

    const auto captures = recorder.GetCaptures();
    Ensure(captures.size() == 2, "Recorder did not preserve both segments");
    for (const klvk::PerfRecorder::Capture& capture : captures)
    {
        Ensure(capture.state == klvk::PerfRecorder::CaptureState::Captured, capture.error);
        Ensure(!std::filesystem::exists(SpeedscopePath(capture)), "Recorder wrote a Speedscope export");
    }

    klvk::SpeedscopeExporter exporter({.executable = std::filesystem::canonical("/proc/self/exe").string()});
    for (const klvk::PerfRecorder::Capture& capture : captures)
    {
        const auto result = exporter.Export(capture.data_path, SpeedscopePath(capture), capture.log_path, {});
        Ensure(result.state == klvk::SpeedscopeExporter::ResultState::Complete, result.error);
        Ensure(exporter.GetProgress() != 0, "Speedscope export is empty");
    }
    std::filesystem::remove_all(output);
}

void TestRealRecorder()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output = std::filesystem::temp_directory_path() /
                        fmt::format("klvk-perf-recorder-real-{}-{}", klvk::os::GetProcessId(), unique);
    klvk::PerfRecorder recorder({.output_directory = output});
    Ensure(recorder.IsAvailable(), recorder.GetLastError());
    Ensure(recorder.Start(), recorder.GetLastError());
    WaitForCaptureToStart(recorder);

    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
    while (std::chrono::steady_clock::now() < end) std::this_thread::yield();
    recorder.Pause();
    Ensure(recorder.IsPaused(), recorder.GetLastError());

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    recorder.Resume();
    Ensure(recorder.IsRecording(), recorder.GetLastError());
    end = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
    while (std::chrono::steady_clock::now() < end) std::this_thread::yield();

    recorder.Stop();
    recorder.Finish();
    const auto captures = recorder.GetCaptures();
    Ensure(captures.size() == 1, "Real perf recording did not create one segment");
    Ensure(captures.front().state == klvk::PerfRecorder::CaptureState::Captured, captures.front().error);

    klvk::SpeedscopeExporter exporter;
    const auto export_result =
        exporter.Export(captures.front().data_path, SpeedscopePath(captures.front()), captures.front().log_path, {});
    Ensure(export_result.state == klvk::SpeedscopeExporter::ResultState::Complete, export_result.error);
    Ensure(exporter.GetProgress() != 0, "Speedscope export is empty");
    std::filesystem::remove_all(output);
}

void TestStubbornRecorderShutdown()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output = std::filesystem::temp_directory_path() /
                        fmt::format("klvk-perf-recorder-stubborn-{}-{}", klvk::os::GetProcessId(), unique);
    klvk::PerfRecorder recorder({
        .output_directory = output,
        .executable = std::filesystem::canonical("/proc/self/exe").string(),
        .frequency = 1,
    });

    Ensure(recorder.Start(), recorder.GetLastError());
    WaitForCaptureToStart(recorder);
    recorder.Finish();

    const auto captures = recorder.GetCaptures();
    Ensure(captures.size() == 1, "Stubborn perf recording did not create one segment");
    Ensure(captures.front().state == klvk::PerfRecorder::CaptureState::Failed, "Stubborn perf recording did not fail");
    Ensure(captures.front().error.contains("did not stop within"), captures.front().error);
    std::filesystem::remove_all(output);
}

void TestControlTimeout()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output = std::filesystem::temp_directory_path() /
                        fmt::format("klvk-perf-recorder-timeout-{}-{}", klvk::os::GetProcessId(), unique);
    klvk::PerfRecorder recorder({
        .output_directory = output,
        .executable = std::filesystem::canonical("/proc/self/exe").string(),
        .frequency = 2,
    });

    Ensure(recorder.Start(), recorder.GetLastError());
    WaitForCaptureToStart(recorder);
    const auto start = std::chrono::steady_clock::now();
    recorder.Pause();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    Ensure(elapsed < std::chrono::milliseconds{1'500}, "Partial perf acknowledgement extended the control timeout");
    Ensure(!recorder.IsPaused(), "Recorder accepted a partial perf acknowledgement");
    recorder.Finish();
    std::filesystem::remove_all(output);
}

void TestCancelledExport()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output = std::filesystem::temp_directory_path() /
                        fmt::format("klvk-speedscope-export-test-{}-{}", klvk::os::GetProcessId(), unique);
    std::filesystem::create_directories(output);
    const auto perf_data = output / "cancel.data";
    const auto speedscope = output / "cancel.linux-perf.txt";
    const auto log = output / "cancel.log";
    std::ofstream(perf_data) << "fake perf data\n";

    klvk::SpeedscopeExporter exporter({.executable = std::filesystem::canonical("/proc/self/exe").string()});
    std::ofstream(speedscope) << "preserve\n";
    std::stop_source already_stopped;
    already_stopped.request_stop();
    const auto pre_cancelled = exporter.Export(perf_data, speedscope, log, already_stopped.get_token());
    Ensure(pre_cancelled.state == klvk::SpeedscopeExporter::ResultState::Cancelled, pre_cancelled.error);
    std::ifstream preserved_file(speedscope);
    const std::string preserved{std::istreambuf_iterator<char>{preserved_file}, std::istreambuf_iterator<char>{}};
    Ensure(preserved == "preserve\n", "Pre-cancelled export changed its output file");

    klvk::SpeedscopeExporter::Result result;
    std::jthread worker([&](const std::stop_token& stop_token)
                        { result = exporter.Export(perf_data, speedscope, log, stop_token); });
    for (size_t attempt = 0; attempt != 1'000 && exporter.GetProgress() == 0; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    Ensure(exporter.GetProgress() != 0, "Speedscope export did not report progress");
    worker.request_stop();
    worker.join();
    Ensure(result.state == klvk::SpeedscopeExporter::ResultState::Cancelled, result.error);
    Ensure(!std::filesystem::exists(speedscope), "Cancelled Speedscope export left a partial file");
    std::filesystem::remove_all(output);
}

#endif

}  // namespace

int main(int argc, char** argv)
{
#if defined(__linux__)
    try
    {
        if (argc > 1 && std::string_view{argv[1]} == "--real")
        {
            TestRealRecorder();
        }
        else if (argc > 1)
        {
            return RunFakePerf(argc, argv);
        }
        else
        {
            TestRecorder();
            TestStubbornRecorderShutdown();
            TestControlTimeout();
            TestCancelledExport();
        }
        fmt::println("Perf recorder tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
#else
    (void)argc;
    (void)argv;
    fmt::println("Perf recorder tests skipped: Linux is required");
    return 0;
#endif
}
