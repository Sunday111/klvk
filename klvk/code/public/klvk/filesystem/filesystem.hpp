#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace klvk
{

class Filesystem
{
public:
    static void ReadFile(const std::filesystem::path& path, std::string& buffer);
    static void WriteFile(const std::filesystem::path& path, std::string_view buffer);
    static void AppendFileContentToBuffer(const std::filesystem::path& path, std::string& buffer);

    // Atomically replaces destination with an already-written file. Source and
    // destination must be in the same filesystem; source no longer exists on success.
    static void InstallFileAtomically(const std::filesystem::path& source, const std::filesystem::path& destination);

    // Note: this one is not recursive
    static std::optional<std::filesystem::path> FindFirstWithExtension(
        std::filesystem::path& directory,
        const std::string_view& extension);
};

}  // namespace klvk
