#pragma once

#if defined(__linux__)

#include <sys/types.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "klvk/integral_aliases.hpp"

namespace klvk::perf_process
{

using Id = pid_t;
constexpr Id kNoProcess = -1;

enum class WaitState : u8
{
    Complete,
    TimedOut,
    Failed,
};

struct WaitResult
{
    WaitState state = WaitState::TimedOut;
    int status = 0;
    int error = 0;
};

[[nodiscard]] bool IsExecutableAvailable(const std::string& executable);
[[nodiscard]] int Spawn(
    const std::string& executable,
    std::vector<std::string>& arguments,
    const std::filesystem::path& output,
    int output_flags,
    const std::optional<std::filesystem::path>& error_output,
    Id& process);
[[nodiscard]] WaitResult Wait(Id process, std::chrono::steady_clock::duration timeout) noexcept;
[[nodiscard]] std::string Status(int status);
[[nodiscard]] bool Succeeded(int status);

}  // namespace klvk::perf_process

#endif
