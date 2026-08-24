#include "klvk/diagnostics/speedscope_exporter.hpp"

#if defined(__linux__)

#include <fcntl.h>
#include <fmt/core.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#include "edt/functional/on_scope_leave.hpp"
#include "klvk/error_handling.hpp"
#include "perf_process.hpp"

namespace klvk
{
namespace
{

constexpr auto kProgressInterval = std::chrono::milliseconds{50};
constexpr auto kTerminateTimeout = std::chrono::milliseconds{500};
constexpr auto kKillTimeout = std::chrono::milliseconds{500};

[[nodiscard]] uintmax_t ExportedSize(const std::filesystem::path& output_path) noexcept
{
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(output_path, error);
    return error ? 0 : size;
}

[[nodiscard]] bool CancelExport(perf_process::Id process, const std::filesystem::path& output_path) noexcept
{
    if (::kill(process, SIGTERM) != 0 && errno != ESRCH) return false;
    perf_process::WaitResult result = perf_process::Wait(process, kTerminateTimeout);
    if (result.state == perf_process::WaitState::TimedOut)
    {
        if (::kill(process, SIGKILL) != 0 && errno != ESRCH) return false;
        result = perf_process::Wait(process, kKillTimeout);
    }
    std::error_code ignored;
    std::filesystem::remove(output_path, ignored);
    return result.state == perf_process::WaitState::Complete;
}

}  // namespace

class SpeedscopeExporter::Impl
{
public:
    explicit Impl(Config config) : config_(std::move(config))
    {
        ErrorHandling::Ensure(!config_.executable.empty(), "Perf executable must not be empty");
        available_ = perf_process::IsExecutableAvailable(config_.executable);
        if (!available_) unavailable_reason_ = fmt::format("Could not find '{}' on PATH", config_.executable);
    }

    [[nodiscard]] bool IsAvailable() const noexcept { return available_; }

    [[nodiscard]] const std::string& GetUnavailableReason() const noexcept { return unavailable_reason_; }

    [[nodiscard]] uintmax_t GetProgress() const noexcept { return progress_.load(std::memory_order_relaxed); }

    [[nodiscard]] Result Export(
        const std::filesystem::path& perf_data_path,
        const std::filesystem::path& output_path,
        const std::filesystem::path& log_path,
        const std::stop_token& stop_token) const
    {
        if (stop_token.stop_requested()) return {.state = ResultState::Cancelled};

        bool expected = false;
        if (!exporting_.compare_exchange_strong(expected, true, std::memory_order_acquire))
        {
            return {.state = ResultState::Failed, .error = "A Speedscope export is already running"};
        }
        auto reset_exporting = edt::OnScopeLeave([this] { exporting_.store(false, std::memory_order_release); });
        progress_.store(0, std::memory_order_relaxed);
        if (!available_) return {.state = ResultState::Failed, .error = unavailable_reason_};

        std::vector<std::string> arguments{config_.executable, "script", "--input", perf_data_path.string()};
        perf_process::Id process = perf_process::kNoProcess;
        const int start_result = perf_process::Spawn(
            config_.executable,
            arguments,
            output_path,
            O_WRONLY | O_CREAT | O_TRUNC,
            log_path,
            process);
        if (start_result != 0)
        {
            return {
                .state = ResultState::Failed,
                .error = fmt::format("Failed to start perf script: {}", std::strerror(start_result)),
            };
        }

        for (;;)
        {
            if (stop_token.stop_requested())
            {
                return CancelExport(process, output_path)
                           ? Result{.state = ResultState::Cancelled}
                           : Result{.state = ResultState::Failed, .error = "Failed to cancel perf script"};
            }

            int status = 0;
            const perf_process::Id wait_result = ::waitpid(process, &status, WNOHANG);
            progress_.store(ExportedSize(output_path), std::memory_order_relaxed);
            if (wait_result == process)
            {
                if (!perf_process::Succeeded(status))
                {
                    return {
                        .state = ResultState::Failed,
                        .error = fmt::format("perf script {}; see {}", perf_process::Status(status), log_path.string()),
                    };
                }

                const uintmax_t size = ExportedSize(output_path);
                progress_.store(size, std::memory_order_relaxed);
                return {.state = size == 0 ? ResultState::Empty : ResultState::Complete};
            }
            if (wait_result < 0 && errno != EINTR)
            {
                return {
                    .state = ResultState::Failed,
                    .error = fmt::format("Failed to wait for perf script: {}", std::strerror(errno)),
                };
            }
            std::this_thread::sleep_for(kProgressInterval);
        }
    }

private:
    Config config_;
    mutable std::atomic<uintmax_t> progress_ = 0;
    mutable std::atomic<bool> exporting_ = false;
    std::string unavailable_reason_;
    bool available_ = false;
};

SpeedscopeExporter::SpeedscopeExporter() : SpeedscopeExporter(Config{}) {}

SpeedscopeExporter::SpeedscopeExporter(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}

SpeedscopeExporter::~SpeedscopeExporter() = default;

bool SpeedscopeExporter::IsAvailable() const noexcept
{
    return impl_->IsAvailable();
}

const std::string& SpeedscopeExporter::GetUnavailableReason() const noexcept
{
    return impl_->GetUnavailableReason();
}

uintmax_t SpeedscopeExporter::GetProgress() const noexcept
{
    return impl_->GetProgress();
}

SpeedscopeExporter::Result SpeedscopeExporter::Export(
    const std::filesystem::path& perf_data_path,
    const std::filesystem::path& output_path,
    const std::filesystem::path& log_path,
    const std::stop_token& stop_token) const
{
    return impl_->Export(perf_data_path, output_path, log_path, stop_token);
}

}  // namespace klvk

#else

#include <utility>

namespace klvk
{

class SpeedscopeExporter::Impl
{
};

SpeedscopeExporter::SpeedscopeExporter() : SpeedscopeExporter(Config{}) {}

SpeedscopeExporter::SpeedscopeExporter(Config) : impl_(std::make_unique<Impl>()) {}

SpeedscopeExporter::~SpeedscopeExporter() = default;

bool SpeedscopeExporter::IsAvailable() const noexcept
{
    return false;
}

const std::string& SpeedscopeExporter::GetUnavailableReason() const noexcept
{
    static const std::string error = "Speedscope export is only available on Linux";
    return error;
}

uintmax_t SpeedscopeExporter::GetProgress() const noexcept
{
    return 0;
}

SpeedscopeExporter::Result SpeedscopeExporter::Export(
    const std::filesystem::path&,
    const std::filesystem::path&,
    const std::filesystem::path&,
    const std::stop_token&) const
{
    return {.state = ResultState::Failed, .error = GetUnavailableReason()};
}

}  // namespace klvk

#endif
