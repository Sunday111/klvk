#include "klvk/platform/os/os.hpp"

#include <limits>
#include <string_view>
#include <vector>

#include "klvk/error_handling.hpp"

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32)) && !defined(__CYGWIN__)
#undef APIENTRY
#include "Windows.h"

namespace klvk::os
{
std::filesystem::path GetExecutableDir()
{
    std::vector<wchar_t> path(512);
    for (;;)
    {
        ErrorHandling::Ensure(
            path.size() <= std::numeric_limits<DWORD>::max(),
            "Executable path exceeds the Windows API size limit");
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        ErrorHandling::Ensure(length != 0, "GetModuleFileNameW failed with Windows error {}", GetLastError());
        if (length < path.size()) return std::filesystem::path(std::wstring_view(path.data(), length)).parent_path();
        path.resize(path.size() * 2);
    }
}

u64 GetProcessId()
{
    return static_cast<u64>(GetCurrentProcessId());
}
}  // namespace klvk::os

#elif defined(__unix__)

#include <unistd.h>

namespace klvk::os
{
std::filesystem::path GetExecutableDir()
{
    std::vector<char> path(512);
    for (;;)
    {
        // readlink does not null-terminate; only the returned prefix is valid.
        const ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
        ErrorHandling::Ensure(length > 0, "Failed to read /proc/self/exe (readlink returned {})", length);
        if (static_cast<size_t>(length) < path.size())
            return std::filesystem::path(std::string_view(path.data(), static_cast<size_t>(length))).parent_path();
        path.resize(path.size() * 2);
    }
}

u64 GetProcessId()
{
    return static_cast<u64>(getpid());
}
}  // namespace klvk::os
#else
#error Unsupported platform
#endif
