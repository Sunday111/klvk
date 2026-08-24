#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include "klvk/integral_aliases.hpp"

namespace klvk
{

class PerfRecorder
{
public:
    enum class CaptureState : u8
    {
        Recording,
        Paused,
        Finalizing,
        Captured,
        Failed,
    };

    struct Capture
    {
        size_t number = 0;
        CaptureState state = CaptureState::Recording;
        std::filesystem::path data_path;
        std::filesystem::path log_path;
        std::string error;
    };

    struct Config
    {
        std::filesystem::path output_directory;
        std::string executable = "perf";
        u32 frequency = 999;
    };

    explicit PerfRecorder(Config config);
    PerfRecorder(const PerfRecorder&) = delete;
    PerfRecorder(PerfRecorder&&) = delete;
    ~PerfRecorder();

    PerfRecorder& operator=(const PerfRecorder&) = delete;
    PerfRecorder& operator=(PerfRecorder&&) = delete;

    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] bool IsRecording() const noexcept;
    [[nodiscard]] bool IsPaused() const noexcept;
    [[nodiscard]] bool IsFinalizing() const noexcept;
    [[nodiscard]] bool CanStart() const noexcept;
    [[nodiscard]] bool Start();
    void Pause() noexcept;
    void Resume() noexcept;
    void Stop() noexcept;
    void Update() noexcept;
    void Finish() noexcept;

    [[nodiscard]] const std::filesystem::path& GetOutputDirectory() const noexcept;
    [[nodiscard]] std::span<const Capture> GetCaptures() const noexcept;
    [[nodiscard]] const std::string& GetLastError() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace klvk
