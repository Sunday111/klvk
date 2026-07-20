#include "klvk/diagnostics/diagnostic_run_config.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <unordered_set>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "platform/input_mapping.hpp"

namespace klvk
{
namespace
{

void EnsureObject(const nlohmann::json& value, std::string_view name)
{
    ErrorHandling::Ensure(value.is_object(), "Diagnostic configuration field '{}' must be an object", name);
}

void EnsureKnownKeys(
    const nlohmann::json& object,
    std::string_view name,
    std::initializer_list<std::string_view> known_keys)
{
    EnsureObject(object, name);
    for (const auto& [key, value] : object.items())
    {
        (void)value;
        const bool known = std::ranges::find(known_keys, key) != known_keys.end();
        ErrorHandling::Ensure(known, "Unknown diagnostic configuration field '{}.{}'", name, key);
    }
}

u64 ReadNonNegativeInteger(const nlohmann::json& value, std::string_view name)
{
    ErrorHandling::Ensure(value.is_number_integer(), "Diagnostic configuration field '{}' must be an integer", name);
    if (value.is_number_unsigned()) return value.get<u64>();
    const i64 result = value.get<i64>();
    ErrorHandling::Ensure(result >= 0, "Diagnostic configuration field '{}' cannot be negative", name);
    return static_cast<u64>(result);
}

double ReadNonNegativeNumber(const nlohmann::json& value, std::string_view name, bool allow_zero = true)
{
    ErrorHandling::Ensure(value.is_number(), "Diagnostic configuration field '{}' must be a number", name);
    const double result = value.get<double>();
    ErrorHandling::Ensure(
        std::isfinite(result) && (allow_zero ? result >= 0.0 : result > 0.0),
        "Diagnostic configuration field '{}' must be a finite {} number",
        name,
        allow_zero ? "non-negative" : "positive");
    return result;
}

float ReadFiniteFloat(const nlohmann::json& value, std::string_view name)
{
    ErrorHandling::Ensure(value.is_number(), "Diagnostic configuration field '{}' must be a number", name);
    const double parsed = value.get<double>();
    const auto result = static_cast<float>(parsed);
    ErrorHandling::Ensure(
        std::isfinite(parsed) && std::isfinite(result),
        "Diagnostic configuration field '{}' must be a finite float",
        name);
    return result;
}

std::optional<DiagnosticPresentation> PresentationFromName(std::string_view name) noexcept
{
    if (name == "visible") return DiagnosticPresentation::Visible;
    if (name == "hidden") return DiagnosticPresentation::Hidden;
    if (name == "offscreen") return DiagnosticPresentation::Offscreen;
    return std::nullopt;
}

std::optional<u64> ReadOptionalFrame(const nlohmann::json& object, std::string_view prefix)
{
    if (!object.contains("frame")) return std::nullopt;
    const std::string name = std::string(prefix) + ".frame";
    const u64 frame = ReadNonNegativeInteger(object.at("frame"), name);
    ErrorHandling::Ensure(frame > 0, "Diagnostic configuration field '{}' is one-based and must be positive", name);
    return frame;
}

// 'time_ns' is exact and is what a recorded run emits. 'time_seconds' stays for
// hand-authored configurations and is rounded to nanoseconds once, here.
std::optional<u64> ReadOptionalTime(const nlohmann::json& object, std::string_view prefix)
{
    const bool has_seconds = object.contains("time_seconds");
    const bool has_nanoseconds = object.contains("time_ns");
    ErrorHandling::Ensure(
        !(has_seconds && has_nanoseconds),
        "Diagnostic configuration field '{}' must contain at most one of 'time_seconds' and 'time_ns'",
        prefix);
    if (has_nanoseconds) return ReadNonNegativeInteger(object.at("time_ns"), std::string(prefix) + ".time_ns");
    if (!has_seconds) return std::nullopt;
    const double seconds = ReadNonNegativeNumber(object.at("time_seconds"), std::string(prefix) + ".time_seconds");
    return TimerDurationFromSeconds(seconds).count();
}

DiagnosticCaptureConfig ParseCapture(const nlohmann::json& value, size_t index)
{
    const std::string name = "captures[" + std::to_string(index) + "]";
    EnsureKnownKeys(value, name, {"frame", "time_seconds", "time_ns", "path", "include_ui"});
    DiagnosticCaptureConfig result;
    result.frame = ReadOptionalFrame(value, name);
    result.time_ns = ReadOptionalTime(value, name);
    ErrorHandling::Ensure(
        result.frame.has_value() != result.time_ns.has_value(),
        "Diagnostic configuration field '{}' must contain exactly one trigger: 'frame', 'time_seconds', or 'time_ns'",
        name);
    ErrorHandling::Ensure(value.contains("path") && value.at("path").is_string(), "{}.path must be a string", name);
    result.path = value.at("path").get<std::string>();
    ErrorHandling::Ensure(!result.path.empty(), "{}.path cannot be empty", name);
    ErrorHandling::Ensure(result.path.extension() == ".ppm", "{}.path must use the .ppm extension", name);
    if (value.contains("include_ui"))
    {
        ErrorHandling::Ensure(value.at("include_ui").is_boolean(), "{}.include_ui must be a boolean", name);
        result.include_ui = value.at("include_ui").get<bool>();
    }
    return result;
}

DiagnosticVideoConfig ParseVideo(const nlohmann::json& value)
{
    EnsureKnownKeys(
        value,
        "video",
        {"path", "encoding", "encoding_device", "compression_level", "include_ui", "log_ffmpeg"});
    ErrorHandling::Ensure(
        value.contains("path") && value.at("path").is_string(),
        "Diagnostic configuration field 'video.path' must be a string");
    DiagnosticVideoConfig result{.path = value.at("path").get<std::string>()};
    ErrorHandling::Ensure(!result.path.empty(), "video.path cannot be empty");
    ErrorHandling::Ensure(result.path.extension() == ".mp4", "video.path must use the .mp4 extension");
    if (value.contains("encoding"))
    {
        ErrorHandling::Ensure(value.at("encoding").is_string(), "video.encoding must be a string");
        const std::string encoding = value.at("encoding").get<std::string>();
        if (encoding == "av1")
        {
            result.encoding = DiagnosticVideoEncoding::Av1;
        }
        else if (encoding == "h264")
        {
            result.encoding = DiagnosticVideoEncoding::H264;
        }
        else if (encoding == "mpeg4")
        {
            result.encoding = DiagnosticVideoEncoding::Mpeg4;
        }
        else
        {
            ErrorHandling::ThrowWithMessage(
                "Unknown diagnostic video encoding '{}' (expected 'av1', 'h264', or 'mpeg4')",
                encoding);
        }
    }
    if (value.contains("encoding_device"))
    {
        ErrorHandling::Ensure(value.at("encoding_device").is_string(), "video.encoding_device must be a string");
        const std::string device = value.at("encoding_device").get<std::string>();
        if (device == "cpu")
        {
            result.encoding_device = DiagnosticVideoEncodingDevice::Cpu;
        }
        else if (device == "gpu")
        {
            result.encoding_device = DiagnosticVideoEncodingDevice::Gpu;
        }
        else
        {
            ErrorHandling::ThrowWithMessage(
                "Unknown diagnostic video encoding device '{}' (expected 'cpu' or 'gpu')",
                device);
        }
    }
    ErrorHandling::Ensure(
        result.encoding_device != DiagnosticVideoEncodingDevice::Gpu ||
            result.encoding != DiagnosticVideoEncoding::Mpeg4,
        "Diagnostic video encoding 'mpeg4' does not support encoding_device 'gpu'; "
        "use encoding_device 'cpu', encoding 'h264', or encoding 'av1'");
    if (value.contains("compression_level"))
    {
        const u64 compression_level = ReadNonNegativeInteger(value.at("compression_level"), "video.compression_level");
        ErrorHandling::Ensure(
            compression_level <= DiagnosticVideoConfig::kMaximumCompressionLevel,
            "video.compression_level must be between 0 and {}",
            DiagnosticVideoConfig::kMaximumCompressionLevel);
        result.compression_level = static_cast<u8>(compression_level);
    }
    if (value.contains("include_ui"))
    {
        ErrorHandling::Ensure(value.at("include_ui").is_boolean(), "video.include_ui must be a boolean");
        result.include_ui = value.at("include_ui").get<bool>();
    }
    if (value.contains("log_ffmpeg"))
    {
        ErrorHandling::Ensure(value.at("log_ffmpeg").is_boolean(), "video.log_ffmpeg must be a boolean");
        result.log_ffmpeg = value.at("log_ffmpeg").get<bool>();
    }
    return result;
}

InputAction ParseInputAction(const nlohmann::json& value, std::string_view name)
{
    ErrorHandling::Ensure(value.is_string(), "Diagnostic configuration field '{}' must be a string", name);
    const std::string action = value.get<std::string>();
    if (action == "press") return InputAction::Press;
    if (action == "release") return InputAction::Release;
    ErrorHandling::ThrowWithMessage(
        "Unknown diagnostic input action '{}' in '{}' (expected 'press' or 'release')",
        action,
        name);
}

MouseButton ParseMouseButton(const nlohmann::json& value, std::string_view name)
{
    ErrorHandling::Ensure(value.is_string(), "Diagnostic configuration field '{}' must be a string", name);
    const std::string button = value.get<std::string>();
    if (button == "left") return MouseButton::Left;
    if (button == "right") return MouseButton::Right;
    if (button == "middle") return MouseButton::Middle;
    if (button == "button4") return MouseButton::Button4;
    if (button == "button5") return MouseButton::Button5;
    ErrorHandling::ThrowWithMessage(
        "Unknown diagnostic mouse button '{}' in '{}' (expected 'left', 'right', 'middle', 'button4', or 'button5')",
        button,
        name);
}

Key ParseKey(const nlohmann::json& value, std::string_view name)
{
    ErrorHandling::Ensure(value.is_string(), "Diagnostic configuration field '{}' must be a string", name);
    const std::string key = value.get<std::string>();
    if (const std::optional<Key> parsed = KeyFromName(key)) return *parsed;
    ErrorHandling::ThrowWithMessage("Unknown diagnostic key '{}' in '{}'", key, name);
}

edt::Vec2f ParseFloatVector(const nlohmann::json& value, std::string_view name)
{
    ErrorHandling::Ensure(
        value.is_array() && value.size() == 2,
        "Diagnostic configuration field '{}' must be an array containing two numbers",
        name);
    return {
        ReadFiniteFloat(value[0], std::string(name) + "[0]"),
        ReadFiniteFloat(value[1], std::string(name) + "[1]"),
    };
}

DiagnosticInputConfig ParseInput(const nlohmann::json& value, size_t index)
{
    const std::string name = "input[" + std::to_string(index) + "]";
    EnsureObject(value, name);
    ErrorHandling::Ensure(value.contains("type") && value.at("type").is_string(), "{}.type must be a string", name);

    DiagnosticInputConfig result;
    result.frame = ReadOptionalFrame(value, name);
    result.time_ns = ReadOptionalTime(value, name);
    ErrorHandling::Ensure(
        result.frame.has_value() != result.time_ns.has_value(),
        "Diagnostic configuration field '{}' must contain exactly one trigger: 'frame', 'time_seconds', or 'time_ns'",
        name);

    const std::string type = value.at("type").get<std::string>();
    if (type == "mouse_move")
    {
        EnsureKnownKeys(value, name, {"frame", "time_seconds", "time_ns", "type", "position"});
        ErrorHandling::Ensure(value.contains("position"), "{}.position is required", name);
        result.event = DiagnosticMouseMoveInput{
            .position = ParseFloatVector(value.at("position"), name + ".position"),
        };
    }
    else if (type == "mouse_button")
    {
        EnsureKnownKeys(value, name, {"frame", "time_seconds", "time_ns", "type", "button", "action"});
        ErrorHandling::Ensure(value.contains("button"), "{}.button is required", name);
        ErrorHandling::Ensure(value.contains("action"), "{}.action is required", name);
        result.event = DiagnosticMouseButtonInput{
            .button = ParseMouseButton(value.at("button"), name + ".button"),
            .action = ParseInputAction(value.at("action"), name + ".action"),
        };
    }
    else if (type == "mouse_scroll")
    {
        EnsureKnownKeys(value, name, {"frame", "time_seconds", "time_ns", "type", "offset"});
        ErrorHandling::Ensure(value.contains("offset"), "{}.offset is required", name);
        result.event = DiagnosticMouseScrollInput{
            .offset = ParseFloatVector(value.at("offset"), name + ".offset"),
        };
    }
    else if (type == "key")
    {
        EnsureKnownKeys(value, name, {"frame", "time_seconds", "time_ns", "type", "key", "action"});
        ErrorHandling::Ensure(value.contains("key"), "{}.key is required", name);
        ErrorHandling::Ensure(value.contains("action"), "{}.action is required", name);
        result.event = DiagnosticKeyInput{
            .key = ParseKey(value.at("key"), name + ".key"),
            .action = ParseInputAction(value.at("action"), name + ".action"),
        };
    }
    else
    {
        ErrorHandling::ThrowWithMessage(
            "Unknown diagnostic input type '{}' in '{}' "
            "(expected 'mouse_move', 'mouse_button', 'mouse_scroll', or 'key')",
            type,
            name);
    }
    return result;
}

DiagnosticRunConfig ParseConfig(const nlohmann::json& root)
{
    EnsureKnownKeys(
        root,
        "root",
        {"version", "presentation", "framebuffer_size", "clock", "input", "captures", "video", "exit", "application"});
    ErrorHandling::Ensure(root.contains("version"), "Diagnostic configuration is missing 'version'");
    const u64 version = ReadNonNegativeInteger(root.at("version"), "version");
    ErrorHandling::Ensure(
        version == DiagnosticRunConfig::kVersion,
        "Unsupported diagnostic configuration version {} (expected {})",
        version,
        DiagnosticRunConfig::kVersion);

    DiagnosticRunConfig result;
    if (root.contains("presentation"))
    {
        ErrorHandling::Ensure(root.at("presentation").is_string(), "presentation must be a string");
        const std::string presentation = root.at("presentation").get<std::string>();
        const auto parsed = PresentationFromName(presentation);
        ErrorHandling::Ensure(
            parsed.has_value(),
            "Unknown diagnostic presentation '{}' (expected 'visible', 'hidden', or 'offscreen')",
            presentation);
        result.presentation = *parsed;
    }

    if (root.contains("framebuffer_size"))
    {
        const auto& size = root.at("framebuffer_size");
        ErrorHandling::Ensure(
            size.is_array() && size.size() == 2,
            "framebuffer_size must be an array containing width and height");
        const u64 width = ReadNonNegativeInteger(size[0], "framebuffer_size[0]");
        const u64 height = ReadNonNegativeInteger(size[1], "framebuffer_size[1]");
        ErrorHandling::Ensure(width > 0 && height > 0, "framebuffer_size dimensions must be positive");
        ErrorHandling::Ensure(
            width <= static_cast<u64>(std::numeric_limits<int>::max()) &&
                height <= static_cast<u64>(std::numeric_limits<int>::max()),
            "framebuffer_size dimensions exceed the runtime integer size limit");
        result.framebuffer_size = edt::Vec2<u32>{static_cast<u32>(width), static_cast<u32>(height)};
    }
    ErrorHandling::Ensure(
        result.presentation != DiagnosticPresentation::Offscreen || result.framebuffer_size.has_value(),
        "Offscreen diagnostic presentation requires an explicit framebuffer_size");

    if (root.contains("clock"))
    {
        const auto& clock = root.at("clock");
        EnsureKnownKeys(clock, "clock", {"mode", "step_seconds", "step_ns"});
        ErrorHandling::Ensure(clock.contains("mode") && clock.at("mode").is_string(), "clock.mode must be a string");
        const std::string mode = clock.at("mode").get<std::string>();
        ErrorHandling::Ensure(mode == "fixed", "Unknown diagnostic clock mode '{}' (expected 'fixed')", mode);
        const bool has_seconds = clock.contains("step_seconds");
        const bool has_nanoseconds = clock.contains("step_ns");
        ErrorHandling::Ensure(
            !(has_seconds && has_nanoseconds),
            "Fixed diagnostic clock must contain at most one of 'clock.step_seconds' and 'clock.step_ns'");
        ErrorHandling::Ensure(
            has_seconds || has_nanoseconds,
            "Fixed diagnostic clock requires clock.step_seconds or clock.step_ns");
        u64 step_ns = 0;
        if (has_nanoseconds)
        {
            step_ns = ReadNonNegativeInteger(clock.at("step_ns"), "clock.step_ns");
            ErrorHandling::Ensure(step_ns > 0, "clock.step_ns must be positive");
        }
        else
        {
            const double step = ReadNonNegativeNumber(clock.at("step_seconds"), "clock.step_seconds", false);
            step_ns = TimerDurationFromSeconds(step).count();
        }
        // Frame duration and framerate still reach ImGui and the application as
        // floats, so the step has to survive that conversion.
        const auto runtime_step = static_cast<float>(static_cast<double>(step_ns) / 1'000'000'000.0);
        ErrorHandling::Ensure(
            std::isfinite(runtime_step) && runtime_step > 0.f && std::isfinite(1.f / runtime_step),
            "The fixed diagnostic clock step must have a finite positive float duration and reciprocal");
        result.clock.fixed_step_ns = step_ns;
    }

    if (root.contains("input"))
    {
        const auto& input = root.at("input");
        ErrorHandling::Ensure(input.is_array(), "input must be an array");
        result.input.reserve(input.size());
        for (size_t index = 0; index != input.size(); ++index)
        {
            result.input.push_back(ParseInput(input[index], index));
        }
    }

    if (root.contains("captures"))
    {
        const auto& captures = root.at("captures");
        ErrorHandling::Ensure(captures.is_array(), "captures must be an array");
        std::set<std::filesystem::path> paths;
        for (size_t index = 0; index != captures.size(); ++index)
        {
            auto capture = ParseCapture(captures[index], index);
            ErrorHandling::Ensure(
                paths.insert(capture.path.lexically_normal()).second,
                "Multiple diagnostic captures use output path '{}'",
                capture.path.string());
            result.captures.push_back(std::move(capture));
        }
    }
    ErrorHandling::Ensure(
        result.captures.empty() || result.framebuffer_size.has_value(),
        "Diagnostic captures require an explicit framebuffer_size");

    if (root.contains("video"))
    {
        result.video = ParseVideo(root.at("video"));
        ErrorHandling::Ensure(
            result.presentation == DiagnosticPresentation::Offscreen,
            "Diagnostic video capture requires offscreen presentation");
        ErrorHandling::Ensure(
            result.framebuffer_size.has_value(),
            "Diagnostic video capture requires an explicit framebuffer_size");
        ErrorHandling::Ensure(
            result.clock.fixed_step_ns.has_value(),
            "Diagnostic video capture requires a fixed clock");
        const auto size = *result.framebuffer_size;
        ErrorHandling::Ensure(
            size.x() % 2 == 0 && size.y() % 2 == 0,
            "Diagnostic video capture requires even framebuffer dimensions");
    }

    if (!result.captures.empty() || result.video.has_value())
    {
        constexpr u64 maximum_readback_bytes = u64{1} << 30;
        const auto size = *result.framebuffer_size;
        const u64 pixel_count = static_cast<u64>(size.x()) * size.y();
        ErrorHandling::Ensure(
            pixel_count <= maximum_readback_bytes / 4,
            "framebuffer_size exceeds the 1 GiB diagnostic readback limit");
    }

    ErrorHandling::Ensure(root.contains("exit"), "Diagnostic configuration requires an 'exit' condition");
    const auto& exit = root.at("exit");
    EnsureKnownKeys(exit, "exit", {"frame", "time_seconds", "time_ns", "after_last_capture"});
    result.exit.frame = ReadOptionalFrame(exit, "exit");
    result.exit.time_ns = ReadOptionalTime(exit, "exit");
    if (exit.contains("after_last_capture"))
    {
        ErrorHandling::Ensure(exit.at("after_last_capture").is_boolean(), "exit.after_last_capture must be a boolean");
        result.exit.after_last_capture = exit.at("after_last_capture").get<bool>();
    }
    const size_t exit_conditions = static_cast<size_t>(result.exit.frame.has_value()) +
                                   static_cast<size_t>(result.exit.time_ns.has_value()) +
                                   static_cast<size_t>(result.exit.after_last_capture);
    ErrorHandling::Ensure(exit_conditions == 1, "exit must specify exactly one exit condition");
    ErrorHandling::Ensure(
        !result.exit.after_last_capture || !result.captures.empty(),
        "exit.after_last_capture requires at least one capture");
    if (result.exit.frame.has_value())
    {
        for (const auto& input : result.input)
        {
            ErrorHandling::Ensure(
                input.frame.has_value() && *input.frame <= *result.exit.frame,
                "A frame-based exit must not precede or use a different trigger domain from diagnostic input");
        }
        for (const auto& capture : result.captures)
        {
            ErrorHandling::Ensure(
                capture.frame.has_value() && *capture.frame <= *result.exit.frame,
                "A frame-based exit must not precede or use a different trigger domain from a capture");
        }
    }
    if (result.exit.time_ns.has_value())
    {
        for (const auto& input : result.input)
        {
            ErrorHandling::Ensure(
                input.time_ns.has_value() && *input.time_ns <= *result.exit.time_ns,
                "A time-based exit must not precede or use a different trigger domain from diagnostic input");
        }
        for (const auto& capture : result.captures)
        {
            ErrorHandling::Ensure(
                capture.time_ns.has_value() && *capture.time_ns <= *result.exit.time_ns,
                "A time-based exit must not precede or use a different trigger domain from a capture");
        }
    }

    if (root.contains("application"))
    {
        ErrorHandling::Ensure(root.at("application").is_object(), "application must be an object");
        result.application = root.at("application");
    }
    return result;
}

}  // namespace

DiagnosticRunConfig LoadDiagnosticRunConfig(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    ErrorHandling::Ensure(stream.is_open(), "Failed to open diagnostic configuration '{}'", path.string());
    std::vector<std::unordered_set<std::string>> object_keys;
    const auto callback = [&](int, nlohmann::json::parse_event_t event, nlohmann::json& parsed)
    {
        if (event == nlohmann::json::parse_event_t::object_start)
        {
            object_keys.emplace_back();
        }
        else if (event == nlohmann::json::parse_event_t::key)
        {
            const std::string key = parsed.get<std::string>();
            ErrorHandling::Ensure(
                !object_keys.empty() && object_keys.back().insert(key).second,
                "Duplicate diagnostic configuration key '{}'",
                key);
        }
        else if (event == nlohmann::json::parse_event_t::object_end)
        {
            ErrorHandling::Ensure(!object_keys.empty(), "Malformed diagnostic configuration object nesting");
            object_keys.pop_back();
        }
        return true;
    };
    try
    {
        return ParseConfig(nlohmann::json::parse(stream, callback));
    }
    catch (const nlohmann::json::exception& exception)
    {
        ErrorHandling::ThrowWithMessage(
            "Failed to parse diagnostic configuration '{}': {}",
            path.string(),
            exception.what());
    }
}

namespace
{

// Matches one option in either spelling, consuming the following argument for
// the separated form. Returns nullopt when this argument is not the option.
std::optional<std::string_view>
MatchOption(std::span<const std::string_view> arguments, size_t& index, std::string_view option)
{
    const std::string_view argument = arguments[index];
    if (argument == option)
    {
        ErrorHandling::Ensure(index + 1 < arguments.size(), "{} requires a file path", option);
        return arguments[++index];
    }
    if (argument.starts_with(option) && argument.size() > option.size() && argument[option.size()] == '=')
    {
        return argument.substr(option.size() + 1);
    }
    return std::nullopt;
}

void AssignOnce(std::optional<std::filesystem::path>& destination, std::string_view value, std::string_view option)
{
    ErrorHandling::Ensure(!destination.has_value(), "{} was specified more than once", option);
    ErrorHandling::Ensure(!value.empty(), "{} requires a non-empty file path", option);
    destination = value;
}

}  // namespace

DiagnosticCommandLine ParseDiagnosticCommandLine(std::span<const std::string_view> arguments)
{
    constexpr std::string_view kConfigOption = "--klvk-diagnostics";
    constexpr std::string_view kRecordOption = "--klvk-record-input";
    constexpr std::string_view kPresentationOption = "--klvk-presentation";

    DiagnosticCommandLine result;
    for (size_t index = 0; index != arguments.size(); ++index)
    {
        if (const auto value = MatchOption(arguments, index, kConfigOption))
        {
            AssignOnce(result.config_path, *value, kConfigOption);
            continue;
        }
        if (const auto value = MatchOption(arguments, index, kRecordOption))
        {
            AssignOnce(result.input_record_path, *value, kRecordOption);
            continue;
        }
        if (const auto value = MatchOption(arguments, index, kPresentationOption))
        {
            ErrorHandling::Ensure(
                !result.presentation.has_value(),
                "{} was specified more than once",
                kPresentationOption);
            const auto parsed = PresentationFromName(*value);
            ErrorHandling::Ensure(
                parsed.has_value(),
                "Unknown value '{}' for {} (expected 'visible', 'hidden', or 'offscreen')",
                *value,
                kPresentationOption);
            result.presentation = *parsed;
            continue;
        }
        ErrorHandling::Ensure(
            !arguments[index].starts_with("--klvk-"),
            "Unknown klvk command-line option '{}'",
            arguments[index]);
    }
    return result;
}

std::optional<DiagnosticRunConfig> LoadDiagnosticRunConfigFromArguments(std::span<const std::string_view> arguments)
{
    const DiagnosticCommandLine command_line = ParseDiagnosticCommandLine(arguments);
    if (!command_line.config_path.has_value()) return std::nullopt;
    return LoadDiagnosticRunConfig(*command_line.config_path);
}

namespace
{

std::string_view PresentationToString(DiagnosticPresentation presentation)
{
    switch (presentation)
    {
    case DiagnosticPresentation::Visible:
        return "visible";
    case DiagnosticPresentation::Hidden:
        return "hidden";
    case DiagnosticPresentation::Offscreen:
        return "offscreen";
    }
    ErrorHandling::ThrowWithMessage("Unknown diagnostic presentation");
}

std::string_view EncodingToString(DiagnosticVideoEncoding encoding)
{
    switch (encoding)
    {
    case DiagnosticVideoEncoding::Av1:
        return "av1";
    case DiagnosticVideoEncoding::H264:
        return "h264";
    case DiagnosticVideoEncoding::Mpeg4:
        return "mpeg4";
    }
    ErrorHandling::ThrowWithMessage("Unknown diagnostic video encoding");
}

std::string_view EncodingDeviceToString(DiagnosticVideoEncodingDevice device)
{
    switch (device)
    {
    case DiagnosticVideoEncodingDevice::Cpu:
        return "cpu";
    case DiagnosticVideoEncodingDevice::Gpu:
        return "gpu";
    }
    ErrorHandling::ThrowWithMessage("Unknown diagnostic video encoding device");
}

std::string_view ActionToString(InputAction action)
{
    switch (action)
    {
    case InputAction::Press:
        return "press";
    case InputAction::Release:
        return "release";
    }
    ErrorHandling::ThrowWithMessage("Unknown diagnostic input action");
}

std::string_view MouseButtonToString(MouseButton button)
{
    switch (button)
    {
    case MouseButton::Left:
        return "left";
    case MouseButton::Right:
        return "right";
    case MouseButton::Middle:
        return "middle";
    case MouseButton::Button4:
        return "button4";
    case MouseButton::Button5:
        return "button5";
    case MouseButton::Count:
        break;
    }
    ErrorHandling::ThrowWithMessage("Unknown diagnostic mouse button");
}

// Writes the one trigger the entry carries. Recorded entries use frames, which
// pin an event to the rendered frame it happened on regardless of the clock the
// replay runs at; hand-authored entries may use time instead.
void WriteTrigger(nlohmann::json& object, const std::optional<u64>& frame, const std::optional<u64>& time_ns)
{
    if (frame.has_value())
    {
        object["frame"] = *frame;
        return;
    }
    ErrorHandling::Ensure(time_ns.has_value(), "Diagnostic entry has no trigger to serialize");
    object["time_ns"] = *time_ns;
}

nlohmann::json InputEventToJson(const DiagnosticInputEvent& event)
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
                result["button"] = MouseButtonToString(value.button);
                result["action"] = ActionToString(value.action);
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
                result["action"] = ActionToString(value.action);
            }
        },
        event);
    return result;
}

}  // namespace

nlohmann::json DiagnosticRunConfigToJson(const DiagnosticRunConfig& config)
{
    nlohmann::json result = nlohmann::json::object();
    result["version"] = DiagnosticRunConfig::kVersion;
    result["presentation"] = PresentationToString(config.presentation);
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
            nlohmann::json value = InputEventToJson(entry.event);
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

    if (config.video.has_value())
    {
        result["video"] = {
            {"path", config.video->path.generic_string()},
            {"encoding", EncodingToString(config.video->encoding)},
            {"encoding_device", EncodingDeviceToString(config.video->encoding_device)},
            {"compression_level", config.video->compression_level},
            {"include_ui", config.video->include_ui},
            {"log_ffmpeg", config.video->log_ffmpeg}};
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
