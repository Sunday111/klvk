#include <set>

#include "diagnostic_run_config_json.hpp"
#include "klvk/error_handling.hpp"

namespace klvk
{

void DiagnosticRunConfigJson::ValidateExitDomain(const DiagnosticRunConfig& config)
{
    const auto validate = [&](std::optional<u64> DiagnosticInputConfig::* input_trigger,
                              std::optional<u64> DiagnosticCaptureConfig::* capture_trigger,
                              std::optional<u64> exit_trigger,
                              std::string_view domain)
    {
        if (!exit_trigger.has_value()) return;
        for (const DiagnosticInputConfig& input : config.input)
        {
            const std::optional<u64>& trigger = input.*input_trigger;
            ErrorHandling::Ensure(
                trigger.has_value() && *trigger <= *exit_trigger,
                "A {}-based exit must not precede or use a different trigger domain from diagnostic input",
                domain);
        }
        for (const DiagnosticCaptureConfig& capture : config.captures)
        {
            const std::optional<u64>& trigger = capture.*capture_trigger;
            ErrorHandling::Ensure(
                trigger.has_value() && *trigger <= *exit_trigger,
                "A {}-based exit must not precede or use a different trigger domain from a capture",
                domain);
        }
    };

    validate(&DiagnosticInputConfig::frame, &DiagnosticCaptureConfig::frame, config.exit.frame, "frame");
    validate(&DiagnosticInputConfig::time_ns, &DiagnosticCaptureConfig::time_ns, config.exit.time_ns, "time");
}

void DiagnosticRunConfigJson::ValidateCombination(const DiagnosticRunConfig& config)
{
    ErrorHandling::Ensure(
        config.presentation != DiagnosticPresentation::Offscreen || config.framebuffer_size.has_value(),
        "Offscreen diagnostic presentation requires an explicit framebuffer_size");
    ErrorHandling::Ensure(
        config.captures.empty() || config.framebuffer_size.has_value(),
        "Diagnostic captures require an explicit framebuffer_size");

    if (config.video.has_value())
    {
        ErrorHandling::Ensure(
            config.presentation == DiagnosticPresentation::Offscreen,
            "Diagnostic video capture requires offscreen presentation");
        ErrorHandling::Ensure(
            config.framebuffer_size.has_value(),
            "Diagnostic video capture requires an explicit framebuffer_size");
        ErrorHandling::Ensure(
            config.clock.fixed_step_ns.has_value(),
            "Diagnostic video capture requires a fixed clock");
        const auto size = *config.framebuffer_size;
        ErrorHandling::Ensure(
            size.x() % 2 == 0 && size.y() % 2 == 0,
            "Diagnostic video capture requires even framebuffer dimensions");
    }

    if (!config.captures.empty() || config.video.has_value())
    {
        constexpr u64 maximum_readback_bytes = u64{1} << 30;
        const auto size = *config.framebuffer_size;
        const u64 pixel_count = static_cast<u64>(size.x()) * size.y();
        ErrorHandling::Ensure(
            pixel_count <= maximum_readback_bytes / 4,
            "framebuffer_size exceeds the 1 GiB diagnostic readback limit");
    }

    ErrorHandling::Ensure(
        !config.exit.after_last_capture || !config.captures.empty(),
        "exit.after_last_capture requires at least one capture");

    if (config.checkpoints.has_value())
    {
        ErrorHandling::Ensure(
            config.framebuffer_size.has_value(),
            "Diagnostic checkpoints require an explicit framebuffer_size");
        ErrorHandling::Ensure(config.exit.frame.has_value(), "Diagnostic checkpoints require a frame-based exit");
        for (const DiagnosticCheckpoint& checkpoint : config.checkpoints->expected)
        {
            ErrorHandling::Ensure(
                checkpoint.frame <= *config.exit.frame,
                "A checkpoint frame must not exceed the exit frame");
        }
    }

    ValidateExitDomain(config);
}

void DiagnosticRunConfigJson::ResolvePaths(
    DiagnosticRunConfig& config,
    const std::filesystem::path& executable_directory)
{
    const auto resolve = [&](std::filesystem::path& value)
    {
        if (value.is_relative()) value = executable_directory / value;
        value = value.lexically_normal();
    };

    std::set<std::filesystem::path> capture_paths;
    for (DiagnosticCaptureConfig& capture : config.captures)
    {
        resolve(capture.path);
        ErrorHandling::Ensure(
            capture_paths.insert(capture.path).second,
            "Multiple diagnostic captures resolve to output path '{}'",
            capture.path.string());
    }

    if (config.video) resolve(config.video->path);
    for (DiagnosticDialogConfig& dialog : config.dialogs)
    {
        if (dialog.answer) resolve(*dialog.answer);
    }
}

}  // namespace klvk
