#include <spdlog/spdlog.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <ranges>
#include <string>
#include <vector>

#include "klvk/integral_aliases.hpp"
#include "klvk/platform/file_dialog.hpp"

// This file is the seam. It is the only translation unit in klvk that knows how a
// file dialog is produced, so replacing the backend is replacing this file.
//
// The dialog comes from the program the desktop already ships for the purpose,
// asked for over a pipe. klvk therefore links nothing to show one and builds
// against no additional package.

namespace klvk
{
namespace
{

enum class DialogKind : u8
{
    Open,
    Save,
};

[[nodiscard]] bool IsInPath(std::string_view program)
{
    const char* const path = std::getenv("PATH");  // NOLINT(concurrency-mt-unsafe)
    if (path == nullptr) return false;

    for (const auto directory : std::views::split(std::string_view{path}, ':'))
    {
        if (directory.empty()) continue;
        std::filesystem::path candidate{std::string_view{directory}};
        candidate /= program;
        if (::access(candidate.c_str(), X_OK) == 0) return true;
    }

    return false;
}

[[nodiscard]] std::vector<std::string> Patterns(std::string_view extensions)
{
    std::vector<std::string> patterns;
    for (const auto extension : std::views::split(extensions, ','))
    {
        const std::string_view text{extension};
        if (!text.empty()) patterns.push_back("*." + std::string{text});
    }

    return patterns;
}

[[nodiscard]] std::string Join(std::span<const std::string> parts, std::string_view separator)
{
    std::string joined;
    for (const auto& part : parts)
    {
        if (!joined.empty()) joined += separator;
        joined += part;
    }

    return joined;
}

[[nodiscard]] std::vector<std::string> ZenityArguments(
    std::string_view program,
    DialogKind kind,
    std::string_view title,
    std::span<const FileDialogFilter> filters,
    const std::filesystem::path& default_path)
{
    std::vector<std::string> arguments{std::string{program}, "--file-selection"};
    arguments.push_back("--title=" + std::string{title});

    if (kind == DialogKind::Save) arguments.emplace_back("--save");

    if (!default_path.empty()) arguments.push_back("--filename=" + default_path.string());

    for (const auto& filter : filters)
    {
        const auto patterns = Patterns(filter.extensions);
        if (patterns.empty()) continue;
        arguments.push_back(
            "--file-filter=" + std::string{filter.name} + " (" + Join(patterns, " ") + ") | " + Join(patterns, " "));
    }

    return arguments;
}

[[nodiscard]] std::vector<std::string> KdialogArguments(
    DialogKind kind,
    std::string_view title,
    std::span<const FileDialogFilter> filters,
    const std::filesystem::path& default_path)
{
    std::vector<std::string> arguments{"kdialog", "--title", std::string{title}};
    arguments.emplace_back(kind == DialogKind::Save ? "--getsavefilename" : "--getopenfilename");
    arguments.push_back(default_path.empty() ? "." : default_path.string());

    std::vector<std::string> entries;
    for (const auto& filter : filters)
    {
        const auto patterns = Patterns(filter.extensions);
        if (!patterns.empty()) entries.push_back(Join(patterns, " ") + "|" + std::string{filter.name});
    }

    if (!entries.empty()) arguments.push_back(Join(entries, "\n"));

    return arguments;
}

// Nothing when the helper reported no selection, which is what a dismissed
// dialog and a failed launch both look like from here.
[[nodiscard]] std::optional<std::string> ReadSelection(const std::vector<std::string>& arguments)
{
    std::array<int, 2> ends{};
    if (::pipe(ends.data()) != 0)
    {
        spdlog::error("Failed to create a pipe for the file dialog");
        return std::nullopt;
    }

    // Everything the child needs is built before the fork, so that between fork
    // and exec it only calls what is safe to call there.
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));  // NOLINT
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(ends[0]);
        ::close(ends[1]);
        spdlog::error("Failed to start the file dialog");
        return std::nullopt;
    }

    if (pid == 0)
    {
        ::close(ends[0]);
        ::dup2(ends[1], STDOUT_FILENO);
        ::close(ends[1]);
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }

    ::close(ends[1]);

    std::string output;
    std::array<char, 4096> buffer{};
    for (ssize_t count = 0; (count = ::read(ends[0], buffer.data(), buffer.size())) > 0;)
    {
        output.append(buffer.data(), static_cast<size_t>(count));
    }
    ::close(ends[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return std::nullopt;
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    if (output.empty()) return std::nullopt;

    return output;
}

[[nodiscard]] std::optional<std::filesystem::path> ShowDialog(
    DialogKind kind,
    std::string_view title,
    std::span<const FileDialogFilter> filters,
    const std::filesystem::path& default_path)
{
    for (const std::string_view program : {"zenity", "qarma"})
    {
        if (!IsInPath(program)) continue;
        const auto selection = ReadSelection(ZenityArguments(program, kind, title, filters, default_path));
        return selection ? std::optional{std::filesystem::path{*selection}} : std::nullopt;
    }

    if (IsInPath("kdialog"))
    {
        const auto selection = ReadSelection(KdialogArguments(kind, title, filters, default_path));
        return selection ? std::optional{std::filesystem::path{*selection}} : std::nullopt;
    }

    spdlog::error("No file dialog program found - install zenity, qarma or kdialog");
    return std::nullopt;
}

}  // namespace

std::optional<std::filesystem::path> OpenFileDialog(
    std::string_view title,
    std::span<const FileDialogFilter> filters,
    const std::filesystem::path& default_path)
{
    return ShowDialog(DialogKind::Open, title, filters, default_path);
}

std::optional<std::filesystem::path> SaveFileDialog(
    std::string_view title,
    std::span<const FileDialogFilter> filters,
    const std::filesystem::path& default_path)
{
    return ShowDialog(DialogKind::Save, title, filters, default_path);
}

}  // namespace klvk
