#include "klvk/diagnostics/perf_recorder.hpp"

#if defined(__linux__)

#include <fcntl.h>
#include <fmt/format.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/platform/os/os.hpp"
#include "perf_process.hpp"

namespace klvk
{
namespace
{

constexpr auto kRecordFinalizeTimeout = std::chrono::seconds{2};
constexpr auto kTerminateTimeout = std::chrono::milliseconds{500};
constexpr auto kKillTimeout = std::chrono::milliseconds{500};
constexpr auto kControlTimeout = std::chrono::seconds{1};

using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

[[nodiscard]] bool WaitUntilReadable(int file, std::chrono::steady_clock::time_point deadline) noexcept
{
    pollfd descriptor{.fd = file, .events = POLLIN};
    for (;;)
    {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) return false;
        const auto timeout = std::chrono::ceil<std::chrono::milliseconds>(remaining);
        const int result = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
        if (result > 0) return (descriptor.revents & POLLIN) != 0;
        if (result == 0 || errno != EINTR) return false;
    }
}

}  // namespace

class PerfRecorder::Impl
{
public:
    explicit Impl(Config config) : config_(std::move(config))
    {
        ErrorHandling::Ensure(!config_.output_directory.empty(), "Perf output directory must not be empty");
        ErrorHandling::Ensure(!config_.executable.empty(), "Perf executable must not be empty");
        ErrorHandling::Ensure(config_.frequency != 0, "Perf sampling frequency must be positive");
        available_ = perf_process::IsExecutableAvailable(config_.executable);
        if (!available_) last_error_ = fmt::format("Could not find '{}' on PATH", config_.executable);
    }

    ~Impl() { Finish(); }

    [[nodiscard]] bool Start()
    {
        Update();
        if (!CanStart())
        {
            if (available_) last_error_ = "The previous profile segment is still finalizing";
            return false;
        }

        try
        {
            std::filesystem::create_directories(config_.output_directory);
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            last_error_ = error.what();
            return false;
        }

        const size_t number = captures_.size() + 1;
        const std::string stem = fmt::format("profile-{:03}", number);
        Capture capture{
            .number = number,
            .state = CaptureState::Recording,
            .data_path = config_.output_directory / (stem + ".data"),
            .log_path = config_.output_directory / (stem + ".log"),
        };

        const std::filesystem::path control_path = config_.output_directory / (stem + ".control");
        const std::filesystem::path acknowledge_path = config_.output_directory / (stem + ".ack");
        if (::mkfifo(control_path.c_str(), 0600) != 0)
        {
            last_error_ = fmt::format("Failed to create perf control FIFO: {}", std::strerror(errno));
            return false;
        }
        if (::mkfifo(acknowledge_path.c_str(), 0600) != 0)
        {
            const int error = errno;
            std::error_code ignored;
            std::filesystem::remove(control_path, ignored);
            last_error_ = fmt::format("Failed to create perf acknowledgement FIFO: {}", std::strerror(error));
            return false;
        }
        File control_file{std::fopen(control_path.c_str(), "r+e"), &std::fclose};
        File acknowledge_file{std::fopen(acknowledge_path.c_str(), "r+e"), &std::fclose};
        if (control_file == nullptr || acknowledge_file == nullptr ||
            std::setvbuf(control_file.get(), nullptr, _IONBF, 0) != 0 ||
            std::setvbuf(acknowledge_file.get(), nullptr, _IONBF, 0) != 0)
        {
            std::error_code ignored;
            std::filesystem::remove(control_path, ignored);
            std::filesystem::remove(acknowledge_path, ignored);
            last_error_ = "Failed to open perf control FIFOs";
            return false;
        }

        std::vector<std::string> arguments{
            config_.executable,
            "record",
            "--output",
            capture.data_path.string(),
            "--event",
            "cycles:u",
            "--freq",
            std::to_string(config_.frequency),
            "--call-graph",
            "fp",
            "--control",
            fmt::format("fifo:{},{}", control_path.string(), acknowledge_path.string()),
            "--pid",
            std::to_string(os::GetProcessId()),
        };
        perf_process::Id process = perf_process::kNoProcess;
        const int result = perf_process::Spawn(
            config_.executable,
            arguments,
            capture.log_path,
            O_WRONLY | O_CREAT | O_TRUNC,
            std::nullopt,
            process);

        if (result != 0)
        {
            std::error_code ignored;
            std::filesystem::remove(control_path, ignored);
            std::filesystem::remove(acknowledge_path, ignored);
            last_error_ = fmt::format("Failed to start '{}': {}", config_.executable, std::strerror(result));
            return false;
        }

        captures_.push_back(std::move(capture));
        recording_process_ = process;
        control_file_ = std::move(control_file);
        acknowledge_file_ = std::move(acknowledge_file);
        control_path_ = control_path;
        acknowledge_path_ = acknowledge_path;
        active_capture_ = captures_.size() - 1;
        last_error_.clear();
        fmt::println("klvk: recording CPU profile segment {} to {}", number, captures_.back().data_path.string());
        return true;
    }

    void Pause() noexcept
    {
        if (recording_process_ == perf_process::kNoProcess || !active_capture_.has_value()) return;
        if (captures_[*active_capture_].state != CaptureState::Recording) return;

        if (!SendControl("disable\n"))
        {
            last_error_ = "Perf did not acknowledge the pause command";
            return;
        }
        captures_[*active_capture_].state = CaptureState::Paused;
        last_error_.clear();
    }

    void Resume() noexcept
    {
        if (recording_process_ == perf_process::kNoProcess || !active_capture_.has_value()) return;
        if (captures_[*active_capture_].state != CaptureState::Paused) return;

        if (!SendControl("enable\n"))
        {
            last_error_ = "Perf did not acknowledge the resume command";
            return;
        }
        captures_[*active_capture_].state = CaptureState::Recording;
        last_error_.clear();
    }

    void Stop() noexcept
    {
        if (recording_process_ == perf_process::kNoProcess || !active_capture_.has_value()) return;
        const CaptureState state = captures_[*active_capture_].state;
        if (state != CaptureState::Recording && state != CaptureState::Paused) return;

        if (!SendControl("disable\n"))
        {
            last_error_ = "Failed to disable perf recording before stopping";
            return;
        }
        if (::kill(recording_process_, SIGINT) != 0 && errno != ESRCH)
        {
            last_error_ = fmt::format("Failed to stop perf recording: {}", std::strerror(errno));
            return;
        }
        captures_[*active_capture_].state = CaptureState::Finalizing;
        last_error_.clear();
    }

    void Update() noexcept
    {
        if (recording_process_ != perf_process::kNoProcess)
        {
            int status = 0;
            const perf_process::Id result = ::waitpid(recording_process_, &status, WNOHANG);
            if (result == recording_process_)
            {
                CompleteRecording(status);
            }
            else if (result < 0 && errno != EINTR)
            {
                FailActiveCapture(fmt::format("Failed to wait for perf record: {}", std::strerror(errno)));
            }
        }
    }

    void Finish() noexcept
    {
        Stop();
        FinishRecording();
    }

    [[nodiscard]] bool IsAvailable() const noexcept { return available_; }

    [[nodiscard]] bool IsRecording() const noexcept
    {
        return active_capture_.has_value() && captures_[*active_capture_].state == CaptureState::Recording;
    }

    [[nodiscard]] bool IsPaused() const noexcept
    {
        return active_capture_.has_value() && captures_[*active_capture_].state == CaptureState::Paused;
    }

    [[nodiscard]] bool IsFinalizing() const noexcept
    {
        return active_capture_.has_value() && captures_[*active_capture_].state == CaptureState::Finalizing;
    }

    [[nodiscard]] bool CanStart() const noexcept { return available_ && !active_capture_.has_value(); }

    [[nodiscard]] const std::filesystem::path& GetOutputDirectory() const noexcept { return config_.output_directory; }

    [[nodiscard]] std::span<const Capture> GetCaptures() const noexcept { return captures_; }

    [[nodiscard]] const std::string& GetLastError() const noexcept { return last_error_; }

private:
    [[nodiscard]] bool SendControl(std::string_view command) noexcept
    {
        if (control_file_ == nullptr || acknowledge_file_ == nullptr) return false;
        if (std::fwrite(command.data(), 1, command.size(), control_file_.get()) != command.size()) return false;
        if (std::fflush(control_file_.get()) != 0) return false;

        std::array<char, 16> response{};
        size_t response_size = 0;
        const auto deadline = std::chrono::steady_clock::now() + kControlTimeout;
        while (response_size != response.size())
        {
            if (!WaitUntilReadable(::fileno(acknowledge_file_.get()), deadline)) return false;
            const int result = std::fgetc(acknowledge_file_.get());
            if (result != EOF)
            {
                const char character = static_cast<char>(result);
                if (character == '\0') continue;
                if (character == '\n') return std::string_view{response.data(), response_size} == "ack";
                response[response_size] = character;
                ++response_size;
                continue;
            }
            if (std::ferror(acknowledge_file_.get()) != 0 && errno == EINTR)
            {
                std::clearerr(acknowledge_file_.get());
                continue;
            }
            return false;
        }
        return false;
    }

    void CloseControlFile() noexcept
    {
        control_file_.reset();
        acknowledge_file_.reset();
        if (!control_path_.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(control_path_, ignored);
            control_path_.clear();
        }
        if (!acknowledge_path_.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(acknowledge_path_, ignored);
            acknowledge_path_.clear();
        }
    }

    void FinishRecording() noexcept
    {
        if (recording_process_ == perf_process::kNoProcess) return;

        perf_process::WaitResult wait = perf_process::Wait(recording_process_, kRecordFinalizeTimeout);
        if (wait.state == perf_process::WaitState::Complete)
        {
            CompleteRecording(wait.status);
            return;
        }
        if (wait.state == perf_process::WaitState::Failed)
        {
            FailActiveCapture(fmt::format("Failed to finalize perf record: {}", std::strerror(wait.error)));
            return;
        }

        if (::kill(recording_process_, SIGTERM) != 0 && errno != ESRCH)
        {
            FailActiveCapture(fmt::format("Failed to terminate perf record: {}", std::strerror(errno)));
            return;
        }
        wait = perf_process::Wait(recording_process_, kTerminateTimeout);
        if (wait.state == perf_process::WaitState::TimedOut)
        {
            if (::kill(recording_process_, SIGKILL) != 0 && errno != ESRCH)
            {
                FailActiveCapture(fmt::format("Failed to kill perf record: {}", std::strerror(errno)));
                return;
            }
            wait = perf_process::Wait(recording_process_, kKillTimeout);
        }

        if (wait.state == perf_process::WaitState::Failed)
        {
            FailActiveCapture(fmt::format("Failed to reap perf record: {}", std::strerror(wait.error)));
            return;
        }
        if (wait.state == perf_process::WaitState::TimedOut)
        {
            FailActiveCapture("Timed out waiting for perf record to stop after SIGKILL");
            return;
        }
        FailActiveCapture(
            fmt::format(
                "perf record did not stop within {} seconds and was {}; see {}",
                kRecordFinalizeTimeout.count(),
                perf_process::Status(wait.status),
                captures_[*active_capture_].log_path.string()));
    }

    void CompleteRecording(int status) noexcept
    {
        const size_t index = *active_capture_;
        const bool stop_completed =
            captures_[index].state == CaptureState::Finalizing && WIFSIGNALED(status) && WTERMSIG(status) == SIGINT;
        CloseControlFile();
        recording_process_ = perf_process::kNoProcess;
        active_capture_.reset();

        if (!perf_process::Succeeded(status) && !stop_completed)
        {
            captures_[index].state = CaptureState::Failed;
            captures_[index].error =
                fmt::format("perf record {}; see {}", perf_process::Status(status), captures_[index].log_path.string());
            last_error_ = captures_[index].error;
            return;
        }
        captures_[index].state = CaptureState::Captured;
    }

    void FailActiveCapture(std::string error) noexcept
    {
        if (active_capture_.has_value())
        {
            captures_[*active_capture_].state = CaptureState::Failed;
            captures_[*active_capture_].error = error;
        }
        last_error_ = std::move(error);
        CloseControlFile();
        recording_process_ = perf_process::kNoProcess;
        active_capture_.reset();
    }
    Config config_;
    std::vector<Capture> captures_;
    std::string last_error_;
    bool available_ = false;
    std::optional<size_t> active_capture_;
    perf_process::Id recording_process_ = perf_process::kNoProcess;
    File control_file_{nullptr, &std::fclose};
    File acknowledge_file_{nullptr, &std::fclose};
    std::filesystem::path control_path_;
    std::filesystem::path acknowledge_path_;
};

PerfRecorder::PerfRecorder(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}

PerfRecorder::~PerfRecorder() = default;

bool PerfRecorder::IsAvailable() const noexcept
{
    return impl_->IsAvailable();
}

bool PerfRecorder::IsRecording() const noexcept
{
    return impl_->IsRecording();
}

bool PerfRecorder::IsPaused() const noexcept
{
    return impl_->IsPaused();
}

bool PerfRecorder::IsFinalizing() const noexcept
{
    return impl_->IsFinalizing();
}

bool PerfRecorder::CanStart() const noexcept
{
    return impl_->CanStart();
}

bool PerfRecorder::Start()
{
    return impl_->Start();
}

void PerfRecorder::Pause() noexcept
{
    impl_->Pause();
}

void PerfRecorder::Resume() noexcept
{
    impl_->Resume();
}

void PerfRecorder::Stop() noexcept
{
    impl_->Stop();
}

void PerfRecorder::Update() noexcept
{
    impl_->Update();
}

void PerfRecorder::Finish() noexcept
{
    impl_->Finish();
}

const std::filesystem::path& PerfRecorder::GetOutputDirectory() const noexcept
{
    return impl_->GetOutputDirectory();
}

std::span<const PerfRecorder::Capture> PerfRecorder::GetCaptures() const noexcept
{
    return impl_->GetCaptures();
}

const std::string& PerfRecorder::GetLastError() const noexcept
{
    return impl_->GetLastError();
}

}  // namespace klvk

#else

#include <utility>
#include <vector>

namespace klvk
{

class PerfRecorder::Impl
{
public:
    explicit Impl(Config config) : output_directory_(std::move(config.output_directory)) {}

    [[nodiscard]] const std::filesystem::path& GetOutputDirectory() const noexcept { return output_directory_; }
    [[nodiscard]] std::span<const Capture> GetCaptures() const noexcept { return captures_; }

private:
    std::filesystem::path output_directory_;
    std::vector<Capture> captures_;
};

PerfRecorder::PerfRecorder(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}

PerfRecorder::~PerfRecorder() = default;

bool PerfRecorder::IsAvailable() const noexcept
{
    return false;
}

bool PerfRecorder::IsRecording() const noexcept
{
    return false;
}

bool PerfRecorder::IsPaused() const noexcept
{
    return false;
}

bool PerfRecorder::IsFinalizing() const noexcept
{
    return false;
}

bool PerfRecorder::CanStart() const noexcept
{
    return false;
}

bool PerfRecorder::Start()
{
    return false;
}

void PerfRecorder::Pause() noexcept {}

void PerfRecorder::Resume() noexcept {}

void PerfRecorder::Stop() noexcept {}

void PerfRecorder::Update() noexcept {}

void PerfRecorder::Finish() noexcept {}

const std::filesystem::path& PerfRecorder::GetOutputDirectory() const noexcept
{
    return impl_->GetOutputDirectory();
}

std::span<const PerfRecorder::Capture> PerfRecorder::GetCaptures() const noexcept
{
    return impl_->GetCaptures();
}

const std::string& PerfRecorder::GetLastError() const noexcept
{
    static const std::string error = "Perf recording is only available on Linux";
    return error;
}

}  // namespace klvk

#endif
