#include "klvk/platform/os/os.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#undef APIENTRY
#include "Windows.h"

namespace klvk::os
{
std::filesystem::path GetExecutableDir()
{
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const size_t index = std::wstring_view(path).find_last_of(L"\\/");
    path[index] = L'\0';
    return path;
}
}  // namespace klvk::os

#endif

#ifdef __unix__

#include <unistd.h>

#include <string_view>

#include "klvk/error_handling.hpp"

namespace klvk::os
{
std::filesystem::path GetExecutableDir()
{
    // readlink does not null-terminate; the buffer is only valid up to the returned length.
    char path[1024];  // NOLINT
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path));
    ErrorHandling::Ensure(
        length > 0 && length < static_cast<ssize_t>(sizeof(path)),
        "Failed to read /proc/self/exe (readlink returned {})",
        length);
    return std::filesystem::path(std::string_view(path, static_cast<size_t>(length))).parent_path();
}
}  // namespace klvk::os
#endif
