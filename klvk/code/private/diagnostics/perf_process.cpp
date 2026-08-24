#include "perf_process.hpp"

#if defined(__linux__)

#include <fcntl.h>
#include <fmt/format.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <string_view>
#include <thread>

namespace klvk::perf_process
{
namespace
{

constexpr auto kPollInterval = std::chrono::milliseconds{10};

[[nodiscard]] bool IsExecutable(const std::filesystem::path& path)
{
    return ::access(path.c_str(), X_OK) == 0;
}

[[nodiscard]] std::vector<char*> ArgumentPointers(std::vector<std::string>& arguments)
{
    std::vector<char*> result;
    result.reserve(arguments.size() + 1);
    for (std::string& argument : arguments) result.push_back(argument.data());
    result.push_back(nullptr);
    return result;
}

}  // namespace

bool IsExecutableAvailable(const std::string& executable)
{
    if (executable.find('/') != std::string::npos) return IsExecutable(executable);

    const char* path = std::getenv("PATH");
    if (path == nullptr) return false;

    std::string_view remaining{path};
    for (;;)
    {
        const size_t separator = remaining.find(':');
        const std::string_view directory = remaining.substr(0, separator);
        const std::filesystem::path candidate =
            directory.empty() ? std::filesystem::path{executable} : std::filesystem::path{directory} / executable;
        if (IsExecutable(candidate)) return true;
        if (separator == std::string_view::npos) break;
        remaining.remove_prefix(separator + 1);
    }
    return false;
}

int Spawn(
    const std::string& executable,
    std::vector<std::string>& arguments,
    const std::filesystem::path& output,
    int output_flags,
    const std::optional<std::filesystem::path>& error_output,
    Id& process)
{
    const std::vector<char*> argv = ArgumentPointers(arguments);
    posix_spawn_file_actions_t actions{};
    int result = posix_spawn_file_actions_init(&actions);
    if (result != 0) return result;

    result = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output.c_str(), output_flags, 0644);
    if (result == 0)
    {
        result = error_output.has_value() ? posix_spawn_file_actions_addopen(
                                                &actions,
                                                STDERR_FILENO,
                                                error_output->c_str(),
                                                O_WRONLY | O_CREAT | O_APPEND,
                                                0644)
                                          : posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    }
    if (result == 0) result = posix_spawnp(&process, executable.c_str(), &actions, nullptr, argv.data(), ::environ);
    posix_spawn_file_actions_destroy(&actions);
    return result;
}

WaitResult Wait(Id process, std::chrono::steady_clock::duration timeout) noexcept
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        int status = 0;
        const Id result = ::waitpid(process, &status, WNOHANG);
        if (result == process) return {.state = WaitState::Complete, .status = status};
        if (result < 0 && errno != EINTR) return {.state = WaitState::Failed, .error = errno};
        if (std::chrono::steady_clock::now() >= deadline) return {};
        std::this_thread::sleep_for(kPollInterval);
    }
}

std::string Status(int status)
{
    if (WIFEXITED(status)) return fmt::format("exited with status {}", WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return fmt::format("terminated by signal {}", WTERMSIG(status));
    return "ended unexpectedly";
}

bool Succeeded(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace klvk::perf_process

#endif
