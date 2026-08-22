#include "diagnostic_run_config_json.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>

#include "klvk/error_handling.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "platform/input_mapping.hpp"

namespace klvk
{
namespace
{

constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

}  // namespace

std::optional<DiagnosticPresentation> DiagnosticRunConfigJson::PresentationFromName(std::string_view name) noexcept
{
    const auto found = std::ranges::find(kPresentationNames, name, &JsonReader::EnumName<DiagnosticPresentation>::name);
    if (found == std::ranges::end(kPresentationNames)) return std::nullopt;
    return found->value;
}

std::optional<u64> DiagnosticRunConfigJson::ReadOptionalFrame(const JsonReader& object)
{
    const auto frame = object.OptionalField("frame");
    if (!frame) return std::nullopt;
    return frame->PositiveUInt();
}

// 'time_ns' is exact and is what a recorded run emits. 'time_seconds' stays for
// hand-authored documents and is rounded to nanoseconds once, here.
std::optional<u64> DiagnosticRunConfigJson::ReadOptionalTime(const JsonReader& object)
{
    const auto seconds = object.OptionalField("time_seconds");
    const auto nanoseconds = object.OptionalField("time_ns");
    ErrorHandling::Ensure(
        !(seconds && nanoseconds),
        "Field '{}' must contain at most one of 'time_seconds' and 'time_ns'",
        object.Path());
    if (nanoseconds) return nanoseconds->UInt();
    if (!seconds) return std::nullopt;
    return TimerDurationFromSeconds(seconds->Number()).count();
}

DiagnosticRunConfigJson::Trigger DiagnosticRunConfigJson::ReadTrigger(const JsonReader& object)
{
    Trigger result{.frame = ReadOptionalFrame(object), .time_ns = ReadOptionalTime(object)};
    ErrorHandling::Ensure(
        result.frame.has_value() != result.time_ns.has_value(),
        "Field '{}' must contain exactly one trigger: 'frame', 'time_seconds', or 'time_ns'",
        object.Path());
    return result;
}

edt::Vec2f DiagnosticRunConfigJson::ReadFloatVector(const JsonReader& value)
{
    const auto elements = value.Elements();
    ErrorHandling::Ensure(elements.size() == 2, "Field '{}' must contain two numbers", value.Path());
    return {elements[0].Float(), elements[1].Float()};
}

std::optional<edt::Vec2<u32>> DiagnosticRunConfigJson::ReadFramebufferSize(const JsonReader& root)
{
    const auto size = root.OptionalField("framebuffer_size");
    if (!size) return std::nullopt;

    const auto elements = size->Elements();
    ErrorHandling::Ensure(elements.size() == 2, "Field '{}' must contain width and height", size->Path());
    const u64 width = elements[0].PositiveUInt();
    const u64 height = elements[1].PositiveUInt();
    constexpr auto maximum = static_cast<u64>(std::numeric_limits<int>::max());
    ErrorHandling::Ensure(
        width <= maximum && height <= maximum,
        "Field '{}' dimensions exceed the runtime integer size limit",
        size->Path());
    return edt::Vec2<u32>{static_cast<u32>(width), static_cast<u32>(height)};
}

std::optional<u64> DiagnosticRunConfigJson::ReadClock(const JsonReader& root)
{
    const auto clock = root.OptionalField("clock");
    if (!clock) return std::nullopt;
    clock->EnsureKnownKeys({"mode", "step_seconds", "step_ns"});

    const std::string mode = clock->Field("mode").String();
    ErrorHandling::Ensure(mode == "fixed", "Unknown diagnostic clock mode '{}' (expected 'fixed')", mode);

    const auto seconds = clock->OptionalField("step_seconds");
    const auto nanoseconds = clock->OptionalField("step_ns");
    ErrorHandling::Ensure(
        !(seconds && nanoseconds),
        "Fixed diagnostic clock must contain at most one of 'clock.step_seconds' and 'clock.step_ns'");
    ErrorHandling::Ensure(
        seconds || nanoseconds,
        "Fixed diagnostic clock requires clock.step_seconds or clock.step_ns");

    u64 step_ns = 0;
    if (nanoseconds)
    {
        step_ns = nanoseconds->UInt();
        ErrorHandling::Ensure(step_ns > 0, "clock.step_ns must be positive");
    }
    else
    {
        step_ns = TimerDurationFromSeconds(seconds->Number(false)).count();
    }

    // Frame duration and framerate still reach ImGui and the application as
    // floats, so the step has to survive that conversion.
    const auto runtime_step = static_cast<float>(static_cast<double>(step_ns) / kNanosecondsPerSecond);
    ErrorHandling::Ensure(
        std::isfinite(runtime_step) && runtime_step > 0.f && std::isfinite(1.f / runtime_step),
        "The fixed diagnostic clock step must have a finite positive float duration and reciprocal");
    return step_ns;
}

DiagnosticInputConfig DiagnosticRunConfigJson::ReadInput(const JsonReader& value)
{
    value.EnsureObject();

    DiagnosticInputConfig result;
    const Trigger trigger = ReadTrigger(value);
    result.frame = trigger.frame;
    result.time_ns = trigger.time_ns;

    const std::string type = value.Field("type").String();
    if (type == "mouse_move")
    {
        value.EnsureKnownKeys({"frame", "time_seconds", "time_ns", "type", "position"});
        result.event = DiagnosticMouseMoveInput{.position = ReadFloatVector(value.Field("position"))};
    }
    else if (type == "mouse_button")
    {
        value.EnsureKnownKeys({"frame", "time_seconds", "time_ns", "type", "button", "action"});
        result.event = DiagnosticMouseButtonInput{
            .button = value.Field("button").EnumValue(kMouseButtonNames, kMouseButtonExpectation),
            .action = value.Field("action").EnumValue(kActionNames, kActionExpectation),
        };
    }
    else if (type == "mouse_scroll")
    {
        value.EnsureKnownKeys({"frame", "time_seconds", "time_ns", "type", "offset"});
        result.event = DiagnosticMouseScrollInput{.offset = ReadFloatVector(value.Field("offset"))};
    }
    else if (type == "key")
    {
        value.EnsureKnownKeys({"frame", "time_seconds", "time_ns", "type", "key", "action"});
        const JsonReader key_field = value.Field("key");
        const std::string key = key_field.String();
        const std::optional<Key> parsed = KeyFromName(key);
        ErrorHandling::Ensure(parsed.has_value(), "Unknown diagnostic key '{}' in '{}'", key, key_field.Path());
        result.event = DiagnosticKeyInput{
            .key = *parsed,
            .action = value.Field("action").EnumValue(kActionNames, kActionExpectation),
        };
    }
    else
    {
        ErrorHandling::ThrowWithMessage(
            "Unknown diagnostic input type '{}' in '{}' "
            "(expected 'mouse_move', 'mouse_button', 'mouse_scroll', or 'key')",
            type,
            value.Path());
    }
    return result;
}

DiagnosticCaptureConfig DiagnosticRunConfigJson::ReadCapture(const JsonReader& value)
{
    value.EnsureKnownKeys({"frame", "time_seconds", "time_ns", "path", "include_ui"});

    DiagnosticCaptureConfig result;
    const Trigger trigger = ReadTrigger(value);
    result.frame = trigger.frame;
    result.time_ns = trigger.time_ns;

    const JsonReader path = value.Field("path");
    result.path = path.NonEmptyString();
    ErrorHandling::Ensure(result.path.extension() == ".ppm", "Field '{}' must use the .ppm extension", path.Path());

    if (const auto include_ui = value.OptionalField("include_ui")) result.include_ui = include_ui->Bool();
    return result;
}

DiagnosticDialogConfig DiagnosticRunConfigJson::ReadDialog(const JsonReader& value)
{
    value.EnsureKnownKeys({"frame", "answer"});

    DiagnosticDialogConfig result;
    result.frame = value.Field("frame").PositiveUInt();

    // A missing answer is the recording of a dismissed dialog, which a replay
    // has to reproduce just as faithfully as a chosen file.
    if (const auto answer = value.OptionalField("answer")) result.answer = answer->NonEmptyString();
    return result;
}

DiagnosticVideoConfig DiagnosticRunConfigJson::ReadVideo(const JsonReader& value)
{
    value.EnsureKnownKeys({"path", "encoding", "encoding_device", "compression_level", "include_ui", "log_ffmpeg"});

    const JsonReader path = value.Field("path");
    DiagnosticVideoConfig result{.path = path.NonEmptyString()};
    ErrorHandling::Ensure(result.path.extension() == ".mp4", "Field '{}' must use the .mp4 extension", path.Path());

    if (const auto encoding = value.OptionalField("encoding"))
    {
        result.encoding = encoding->EnumValue(kEncodingNames, kEncodingExpectation);
    }
    if (const auto device = value.OptionalField("encoding_device"))
    {
        result.encoding_device = device->EnumValue(kEncodingDeviceNames, kEncodingDeviceExpectation);
    }
    ErrorHandling::Ensure(
        result.encoding_device != DiagnosticVideoEncodingDevice::Gpu ||
            result.encoding != DiagnosticVideoEncoding::Mpeg4,
        "Diagnostic video encoding 'mpeg4' does not support encoding_device 'gpu'; "
        "use encoding_device 'cpu', encoding 'h264', or encoding 'av1'");

    if (const auto level = value.OptionalField("compression_level"))
    {
        const u64 compression_level = level->UInt();
        ErrorHandling::Ensure(
            compression_level <= DiagnosticVideoConfig::kMaximumCompressionLevel,
            "Field '{}' must be between 0 and {}",
            level->Path(),
            DiagnosticVideoConfig::kMaximumCompressionLevel);
        result.compression_level = static_cast<u8>(compression_level);
    }
    if (const auto include_ui = value.OptionalField("include_ui")) result.include_ui = include_ui->Bool();
    if (const auto log_ffmpeg = value.OptionalField("log_ffmpeg")) result.log_ffmpeg = log_ffmpeg->Bool();
    return result;
}

DiagnosticCheckpointConfig DiagnosticRunConfigJson::ReadCheckpoints(const JsonReader& value)
{
    value.EnsureKnownKeys({"every_frames", "include_ui", "hashes"});

    DiagnosticCheckpointConfig result;
    result.every_frames = value.Field("every_frames").PositiveUInt();
    if (const auto include_ui = value.OptionalField("include_ui")) result.include_ui = include_ui->Bool();

    const auto hashes = value.OptionalField("hashes");
    if (!hashes) return result;

    std::set<u64> frames;
    for (const JsonReader& entry : hashes->Elements())
    {
        entry.EnsureKnownKeys({"frame", "hash"});
        DiagnosticCheckpoint checkpoint;
        checkpoint.frame = entry.Field("frame").PositiveUInt();
        ErrorHandling::Ensure(
            checkpoint.frame % result.every_frames == 0,
            "Field '{}.frame' must be a multiple of checkpoints.every_frames",
            entry.Path());
        ErrorHandling::Ensure(frames.insert(checkpoint.frame).second, "Field '{}.frame' is duplicated", entry.Path());
        checkpoint.hash = entry.Field("hash").UInt();
        result.expected.push_back(checkpoint);
    }
    std::ranges::sort(result.expected, {}, &DiagnosticCheckpoint::frame);
    return result;
}

void DiagnosticRunConfigJson::ReadExit(const JsonReader& root, DiagnosticRunConfig& config)
{
    const JsonReader exit = root.Field("exit");
    exit.EnsureKnownKeys({"frame", "time_seconds", "time_ns", "after_last_capture"});
    config.exit.frame = ReadOptionalFrame(exit);
    config.exit.time_ns = ReadOptionalTime(exit);
    if (const auto after = exit.OptionalField("after_last_capture")) config.exit.after_last_capture = after->Bool();

    const size_t conditions = static_cast<size_t>(config.exit.frame.has_value()) +
                              static_cast<size_t>(config.exit.time_ns.has_value()) +
                              static_cast<size_t>(config.exit.after_last_capture);
    ErrorHandling::Ensure(conditions == 1, "exit must specify exactly one exit condition");
}

DiagnosticRunConfig DiagnosticRunConfigJson::Read(
    const JsonReader& root,
    const std::filesystem::path& executable_directory)
{
    root.EnsureKnownKeys(
        {"version",
         "presentation",
         "framebuffer_size",
         "clock",
         "input",
         "captures",
         "dialogs",
         "video",
         "checkpoints",
         "exit",
         "application"});

    const u64 version = root.Field("version").UInt();
    ErrorHandling::Ensure(
        version == DiagnosticRunConfig::kVersion,
        "Unsupported diagnostic configuration version {} (expected {})",
        version,
        DiagnosticRunConfig::kVersion);

    DiagnosticRunConfig config;
    if (const auto presentation = root.OptionalField("presentation"))
    {
        config.presentation = presentation->EnumValue(kPresentationNames, kPresentationExpectation);
    }
    config.framebuffer_size = ReadFramebufferSize(root);
    config.clock.fixed_step_ns = ReadClock(root);

    if (const auto input = root.OptionalField("input"))
    {
        for (const JsonReader& entry : input->Elements()) config.input.push_back(ReadInput(entry));
    }

    if (const auto captures = root.OptionalField("captures"))
    {
        std::set<std::filesystem::path> paths;
        for (const JsonReader& entry : captures->Elements())
        {
            auto capture = ReadCapture(entry);
            ErrorHandling::Ensure(
                paths.insert(capture.path.lexically_normal()).second,
                "Multiple diagnostic captures use output path '{}'",
                capture.path.string());
            config.captures.push_back(std::move(capture));
        }
    }

    if (const auto dialogs = root.OptionalField("dialogs"))
    {
        for (const JsonReader& entry : dialogs->Elements())
        {
            auto dialog = ReadDialog(entry);
            // Answers are served in order, so a recording whose frames run
            // backwards could not have come from a real session.
            ErrorHandling::Ensure(
                config.dialogs.empty() || config.dialogs.back().frame <= dialog.frame,
                "Field '{}.frame' goes backwards",
                entry.Path());
            config.dialogs.push_back(std::move(dialog));
        }
    }

    if (const auto video = root.OptionalField("video")) config.video = ReadVideo(*video);
    ReadExit(root, config);
    if (const auto checkpoints = root.OptionalField("checkpoints")) config.checkpoints = ReadCheckpoints(*checkpoints);
    if (const auto application = root.OptionalField("application"))
    {
        application->EnsureObject();
        config.application = application->Value();
    }

    ValidateCombination(config);
    ResolvePaths(config, executable_directory);
    return config;
}

}  // namespace klvk
