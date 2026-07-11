#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace klvk
{

class Filesystem
{
public:
    static void ReadFile(const std::filesystem::path& path, std::string& buffer);
    static void WriteFile(const std::filesystem::path& path, std::string_view buffer);
    static void AppendFileContentToBuffer(const std::filesystem::path& path, std::string& buffer);

    // Note: this one is not recursive
    static std::optional<std::filesystem::path> FindFirstWithExtension(
        std::filesystem::path& directory,
        const std::string_view& extension);
};

}  // namespace klvk
