#include "diagnostic_run_config_json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string>

#include "json/json_reader.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "platform/input_mapping.hpp"

namespace klvk
{
namespace
{

constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

// The text after each table is the expectation quoted in errors; keeping it
// beside the table stops the two drifting apart.
constexpr auto kPresentationNames = std::to_array<JsonReader::EnumName<DiagnosticPresentation>>({
    {.name = "visible", .value = DiagnosticPresentation::Visible},
    {.name = "hidden", .value = DiagnosticPresentation::Hidden},
    {.name = "offscreen", .value = DiagnosticPresentation::Offscreen},
});
constexpr std::string_view kPresentationExpectation = "'visible', 'hidden', or 'offscreen'";

constexpr auto kEncodingNames = std::to_array<JsonReader::EnumName<DiagnosticVideoEncoding>>({
    {.name = "av1", .value = DiagnosticVideoEncoding::Av1},
    {.name = "h264", .value = DiagnosticVideoEncoding::H264},
    {.name = "mpeg4", .value = DiagnosticVideoEncoding::Mpeg4},
});
constexpr std::string_view kEncodingExpectation = "'av1', 'h264', or 'mpeg4'";

constexpr auto kEncodingDeviceNames = std::to_array<JsonReader::EnumName<DiagnosticVideoEncodingDevice>>({
    {.name = "cpu", .value = DiagnosticVideoEncodingDevice::Cpu},
    {.name = "gpu", .value = DiagnosticVideoEncodingDevice::Gpu},
});
constexpr std::string_view kEncodingDeviceExpectation = "'cpu' or 'gpu'";

constexpr auto kActionNames = std::to_array<JsonReader::EnumName<InputAction>>({
    {.name = "press", .value = InputAction::Press},
    {.name = "release", .value = InputAction::Release},
});
constexpr std::string_view kActionExpectation = "'press' or 'release'";

constexpr auto kMouseButtonNames = std::to_array<JsonReader::EnumName<MouseButton>>({
    {.name = "left", .value = MouseButton::Left},
    {.name = "right", .value = MouseButton::Right},
    {.name = "middle", .value = MouseButton::Middle},
    {.name = "button4", .value = MouseButton::Button4},
    {.name = "button5", .value = MouseButton::Button5},
});
constexpr std::string_view kMouseButtonExpectation = "'left', 'right', 'middle', 'button4', or 'button5'";

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

// An exit fixes the run's trigger domain: everything scheduled must be in that
// same domain and must not outlive it. Both domains obey the identical rule, so
// they share one implementation parameterized on which member to read.
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
        // Checkpoint frames are enumerated up front, which needs a known last
        // frame. A recording always exits on a frame, so this costs nothing there.
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

// Paths in a document are written relative to the executable directory, so a
// configuration is only usable once they have been resolved against it. Doing it
// here means everything downstream receives a configuration it can act on
// without knowing where the process was launched from.
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

// Writes the one trigger the entry carries. Recorded entries use frames, which
// pin an event to the rendered frame it happened on regardless of the clock the
// replay runs at; hand-authored entries may use time instead.
void DiagnosticRunConfigJson::WriteTrigger(
    nlohmann::json& object,
    const std::optional<u64>& frame,
    const std::optional<u64>& time_ns)
{
    if (frame.has_value())
    {
        object["frame"] = *frame;
        return;
    }
    ErrorHandling::Ensure(time_ns.has_value(), "Diagnostic entry has no trigger to serialize");
    object["time_ns"] = *time_ns;
}

nlohmann::json DiagnosticRunConfigJson::WriteInputEvent(const DiagnosticInputEvent& event)
{
    nlohmann::json result = nlohmann::json::object();
    std::visit(
        [&](const auto& value)
        {
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, DiagnosticMouseMoveInput>)
            {
                result["type"] = "mouse_move";
                result["position"] = {value.position.x(), value.position.y()};
            }
            else if constexpr (std::is_same_v<Event, DiagnosticMouseButtonInput>)
            {
                result["type"] = "mouse_button";
                result["button"] = JsonReader::NameOf(kMouseButtonNames, value.button);
                result["action"] = JsonReader::NameOf(kActionNames, value.action);
            }
            else if constexpr (std::is_same_v<Event, DiagnosticMouseScrollInput>)
            {
                result["type"] = "mouse_scroll";
                result["offset"] = {value.offset.x(), value.offset.y()};
            }
            else
            {
                static_assert(std::is_same_v<Event, DiagnosticKeyInput>);
                const std::optional<std::string_view> name = KeyToName(value.key);
                ErrorHandling::Ensure(name.has_value(), "Recorded key has no diagnostic configuration name");
                result["type"] = "key";
                result["key"] = *name;
                result["action"] = JsonReader::NameOf(kActionNames, value.action);
            }
        },
        event);
    return result;
}

nlohmann::json DiagnosticRunConfigJson::Write(const DiagnosticRunConfig& config)
{
    nlohmann::json result = nlohmann::json::object();
    result["version"] = DiagnosticRunConfig::kVersion;
    result["presentation"] = JsonReader::NameOf(kPresentationNames, config.presentation);
    if (config.framebuffer_size.has_value())
    {
        result["framebuffer_size"] = {config.framebuffer_size->x(), config.framebuffer_size->y()};
    }
    if (config.clock.fixed_step_ns.has_value())
    {
        result["clock"] = {{"mode", "fixed"}, {"step_ns", *config.clock.fixed_step_ns}};
    }

    if (!config.input.empty())
    {
        nlohmann::json input = nlohmann::json::array();
        for (const DiagnosticInputConfig& entry : config.input)
        {
            nlohmann::json value = WriteInputEvent(entry.event);
            WriteTrigger(value, entry.frame, entry.time_ns);
            input.push_back(std::move(value));
        }
        result["input"] = std::move(input);
    }

    if (!config.captures.empty())
    {
        nlohmann::json captures = nlohmann::json::array();
        for (const DiagnosticCaptureConfig& capture : config.captures)
        {
            nlohmann::json value = nlohmann::json::object();
            WriteTrigger(value, capture.frame, capture.time_ns);
            value["path"] = capture.path.generic_string();
            value["include_ui"] = capture.include_ui;
            captures.push_back(std::move(value));
        }
        result["captures"] = std::move(captures);
    }

    if (!config.dialogs.empty())
    {
        nlohmann::json dialogs = nlohmann::json::array();
        for (const DiagnosticDialogConfig& dialog : config.dialogs)
        {
            nlohmann::json value = nlohmann::json::object();
            value["frame"] = dialog.frame;
            if (dialog.answer) value["answer"] = dialog.answer->generic_string();
            dialogs.push_back(std::move(value));
        }
        result["dialogs"] = std::move(dialogs);
    }

    if (config.video.has_value())
    {
        result["video"] = {
            {"path", config.video->path.generic_string()},
            {"encoding", JsonReader::NameOf(kEncodingNames, config.video->encoding)},
            {"encoding_device", JsonReader::NameOf(kEncodingDeviceNames, config.video->encoding_device)},
            {"compression_level", config.video->compression_level},
            {"include_ui", config.video->include_ui},
            {"log_ffmpeg", config.video->log_ffmpeg}};
    }

    if (config.checkpoints.has_value())
    {
        nlohmann::json hashes = nlohmann::json::array();
        for (const DiagnosticCheckpoint& checkpoint : config.checkpoints->expected)
        {
            hashes.push_back({{"frame", checkpoint.frame}, {"hash", checkpoint.hash}});
        }
        nlohmann::json checkpoints = {
            {"every_frames", config.checkpoints->every_frames},
            {"include_ui", config.checkpoints->include_ui}};
        if (!config.checkpoints->expected.empty()) checkpoints["hashes"] = std::move(hashes);
        result["checkpoints"] = std::move(checkpoints);
    }

    nlohmann::json exit = nlohmann::json::object();
    if (config.exit.after_last_capture)
    {
        exit["after_last_capture"] = true;
    }
    else
    {
        WriteTrigger(exit, config.exit.frame, config.exit.time_ns);
    }
    result["exit"] = std::move(exit);

    if (!config.application.empty()) result["application"] = config.application;
    return result;
}

}  // namespace klvk
