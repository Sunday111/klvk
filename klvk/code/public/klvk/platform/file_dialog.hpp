#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace klvk
{

class FileDialog
{
public:
    // One entry of a dialog's file type list. Extensions are named without a dot
    // and separated by commas, the way the underlying dialogs spell them:
    // "json", "png,jpg,jpeg".
    struct Filter
    {
        std::string_view name;
        std::string_view extensions;
    };

    // Blocks until the user picks a file or dismisses the dialog, so the
    // caller's frame takes as long as the user does. Nothing when the dialog was
    // cancelled.
    [[nodiscard]] static std::optional<std::filesystem::path>
    Open(std::string_view title, std::span<const Filter> filters = {}, const std::filesystem::path& default_path = {});

    // As above, for choosing where to write. The dialog itself warns about
    // overwriting, so a returned path is one the user meant to replace.
    [[nodiscard]] static std::optional<std::filesystem::path>
    Save(std::string_view title, std::span<const Filter> filters = {}, const std::filesystem::path& default_path = {});
};

}  // namespace klvk
