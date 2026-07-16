#include "klvk/filesystem/filesystem.hpp"

#include <fmt/format.h>
#include <fmt/std.h>

#include <fstream>

#include "klvk/error_handling.hpp"

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32)) && !defined(__CYGWIN__)
#undef APIENTRY
#include <Windows.h>
#endif

namespace klvk
{

void Filesystem::ReadFile(const std::filesystem::path& path, std::string& buffer)
{
    buffer.clear();
    AppendFileContentToBuffer(path, buffer);
}

void Filesystem::WriteFile(const std::filesystem::path& path, std::string_view buffer)
{
    std::ofstream file(path);
    klvk::ErrorHandling::Ensure(file.is_open(), "Failed to open file \"{}\" for write", path);
    file << buffer;
}

void Filesystem::AppendFileContentToBuffer(const std::filesystem::path& path, std::string& buffer)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    [[unlikely]] if (!file.is_open())
    {
        throw std::runtime_error(fmt::format("failed to open file {}", path));
    }
    const std::streamsize read_size = file.tellg();
    file.seekg(0, std::ios::beg);

    const size_t prev_size = buffer.size();
    buffer.resize(prev_size + static_cast<size_t>(read_size));

    [[unlikely]] if (!file.read(buffer.data() + prev_size, read_size))  // NOLINT
    {
        throw std::runtime_error(fmt::format("failed to read {} bytes from file {}", read_size, path));
    }
}

void Filesystem::InstallFileAtomically(const std::filesystem::path& source, const std::filesystem::path& destination)
{
#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32)) && !defined(__CYGWIN__)
    const BOOL installed =
        MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    const DWORD last_error = installed != FALSE ? ERROR_SUCCESS : GetLastError();
    ErrorHandling::Ensure(
        installed != FALSE,
        "Failed to install file '{}' as '{}' (Windows error {})",
        source.string(),
        destination.string(),
        last_error);
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    ErrorHandling::Ensure(
        !error,
        "Failed to install file '{}' as '{}': {}",
        source.string(),
        destination.string(),
        error.message());
#endif
}

std::optional<std::filesystem::path> Filesystem::FindFirstWithExtension(
    std::filesystem::path& directory,
    const std::string_view& extension)
{
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (const auto& path = entry.path(); !entry.is_directory())
        {
            const std::string filename = path.filename().string();
            if (auto index = filename.find('.'); index != std::string::npos)
            {
                if (auto ext = std::string_view(filename).substr(index + 1); ext == extension)
                {
                    return path;
                }
            }
        }
    }

    return std::nullopt;
}

}  // namespace klvk
