#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace klvk
{

// One entry of a dialog's file type list. Extensions are named without a dot and
// separated by commas, the way the underlying dialogs spell them: "json",
// "png,jpg,jpeg".
struct FileDialogFilter
{
    std::string_view name;
    std::string_view extensions;
};

// Blocks until the user picks a file or dismisses the dialog, so the caller's
// frame takes as long as the user does. Nothing when the dialog was cancelled.
[[nodiscard]] std::optional<std::filesystem::path> OpenFileDialog(
    std::string_view title,
    std::span<const FileDialogFilter> filters = {},
    const std::filesystem::path& default_path = {});

// As above, for choosing where to write. The dialog itself warns about
// overwriting, so a returned path is one the user meant to replace.
[[nodiscard]] std::optional<std::filesystem::path> SaveFileDialog(
    std::string_view title,
    std::span<const FileDialogFilter> filters = {},
    const std::filesystem::path& default_path = {});

}  // namespace klvk
