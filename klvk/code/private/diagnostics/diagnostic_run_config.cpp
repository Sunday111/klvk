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

std::optional<u64> ReadOptionalFrame(const nlohmann::json& object, std::string_view prefix)
{
    if (!object.contains("frame")) return std::nullopt;
    const std::string name = std::string(prefix) + ".frame";
    const u64 frame = ReadNonNegativeInteger(object.at("frame"), name);
    ErrorHandling::Ensure(frame > 0, "Diagnostic configuration field '{}' is one-based and must be positive", name);
    return frame;
}

std::optional<double> ReadOptionalTime(const nlohmann::json& object, std::string_view prefix)
{
    if (!object.contains("time_seconds")) return std::nullopt;
    return ReadNonNegativeNumber(object.at("time_seconds"), std::string(prefix) + ".time_seconds");
}

DiagnosticCaptureConfig ParseCapture(const nlohmann::json& value, size_t index)
{
    const std::string name = "captures[" + std::to_string(index) + "]";
    EnsureKnownKeys(value, name, {"frame", "time_seconds", "path", "include_ui"});
    DiagnosticCaptureConfig result;
    result.frame = ReadOptionalFrame(value, name);
    result.time_seconds = ReadOptionalTime(value, name);
    ErrorHandling::Ensure(
        result.frame.has_value() != result.time_seconds.has_value(),
        "Diagnostic configuration field '{}' must contain exactly one of 'frame' and 'time_seconds'",
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
    result.time_seconds = ReadOptionalTime(value, name);
    ErrorHandling::Ensure(
        result.frame.has_value() != result.time_seconds.has_value(),
        "Diagnostic configuration field '{}' must contain exactly one of 'frame' and 'time_seconds'",
        name);

    const std::string type = value.at("type").get<std::string>();
    if (type == "mouse_move")
    {
        EnsureKnownKeys(value, name, {"frame", "time_seconds", "type", "position"});
        ErrorHandling::Ensure(value.contains("position"), "{}.position is required", name);
        result.event = DiagnosticMouseMoveInput{
            .position = ParseFloatVector(value.at("position"), name + ".position"),
        };
    }
    else if (type == "mouse_button")
    {
        EnsureKnownKeys(value, name, {"frame", "time_seconds", "type", "button", "action"});
        ErrorHandling::Ensure(value.contains("button"), "{}.button is required", name);
        ErrorHandling::Ensure(value.contains("action"), "{}.action is required", name);
        result.event = DiagnosticMouseButtonInput{
            .button = ParseMouseButton(value.at("button"), name + ".button"),
            .action = ParseInputAction(value.at("action"), name + ".action"),
        };
    }
    else if (type == "mouse_scroll")
    {
        EnsureKnownKeys(value, name, {"frame", "time_seconds", "type", "offset"});
        ErrorHandling::Ensure(value.contains("offset"), "{}.offset is required", name);
        result.event = DiagnosticMouseScrollInput{
            .offset = ParseFloatVector(value.at("offset"), name + ".offset"),
        };
    }
    else if (type == "key")
    {
        EnsureKnownKeys(value, name, {"frame", "time_seconds", "type", "key", "action"});
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
        {"version", "presentation", "framebuffer_size", "clock", "input", "captures", "exit", "application"});
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
        if (presentation == "hidden")
        {
            result.presentation = DiagnosticPresentation::Hidden;
        }
        else if (presentation == "visible")
        {
            result.presentation = DiagnosticPresentation::Visible;
        }
        else if (presentation == "offscreen")
        {
            result.presentation = DiagnosticPresentation::Offscreen;
        }
        else
        {
            ErrorHandling::ThrowWithMessage(
                "Unknown diagnostic presentation '{}' (expected 'visible', 'hidden', or 'offscreen')",
                presentation);
        }
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
        EnsureKnownKeys(clock, "clock", {"mode", "step_seconds"});
        ErrorHandling::Ensure(clock.contains("mode") && clock.at("mode").is_string(), "clock.mode must be a string");
        const std::string mode = clock.at("mode").get<std::string>();
        ErrorHandling::Ensure(mode == "fixed", "Unknown diagnostic clock mode '{}' (expected 'fixed')", mode);
        ErrorHandling::Ensure(clock.contains("step_seconds"), "Fixed diagnostic clock requires clock.step_seconds");
        const double step = ReadNonNegativeNumber(clock.at("step_seconds"), "clock.step_seconds", false);
        const auto runtime_step = static_cast<float>(step);
        ErrorHandling::Ensure(
            std::isfinite(runtime_step) && runtime_step > 0.f && std::isfinite(1.f / runtime_step),
            "clock.step_seconds must have a finite positive float duration and reciprocal");
        result.clock.fixed_step_seconds = step;
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
    if (!result.captures.empty())
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
    EnsureKnownKeys(exit, "exit", {"frame", "time_seconds", "after_last_capture"});
    result.exit.frame = ReadOptionalFrame(exit, "exit");
    result.exit.time_seconds = ReadOptionalTime(exit, "exit");
    if (exit.contains("after_last_capture"))
    {
        ErrorHandling::Ensure(exit.at("after_last_capture").is_boolean(), "exit.after_last_capture must be a boolean");
        result.exit.after_last_capture = exit.at("after_last_capture").get<bool>();
    }
    const size_t exit_conditions = static_cast<size_t>(result.exit.frame.has_value()) +
                                   static_cast<size_t>(result.exit.time_seconds.has_value()) +
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
    if (result.exit.time_seconds.has_value())
    {
        for (const auto& input : result.input)
        {
            ErrorHandling::Ensure(
                input.time_seconds.has_value() && *input.time_seconds <= *result.exit.time_seconds,
                "A time-based exit must not precede or use a different trigger domain from diagnostic input");
        }
        for (const auto& capture : result.captures)
        {
            ErrorHandling::Ensure(
                capture.time_seconds.has_value() && *capture.time_seconds <= *result.exit.time_seconds,
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

std::optional<DiagnosticRunConfig> LoadDiagnosticRunConfigFromArguments(std::span<const std::string_view> arguments)
{
    constexpr std::string_view option = "--klvk-diagnostics";
    std::optional<std::filesystem::path> path;
    for (size_t index = 0; index != arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        std::optional<std::string_view> value;
        if (argument == option)
        {
            ErrorHandling::Ensure(index + 1 < arguments.size(), "{} requires a JSON file path", option);
            value = arguments[++index];
        }
        else if (argument.starts_with(option) && argument.size() > option.size() && argument[option.size()] == '=')
        {
            value = argument.substr(option.size() + 1);
        }
        if (!value.has_value())
        {
            ErrorHandling::Ensure(!argument.starts_with("--klvk-"), "Unknown klvk command-line option '{}'", argument);
            continue;
        }
        ErrorHandling::Ensure(!path.has_value(), "{} was specified more than once", option);
        ErrorHandling::Ensure(!value->empty(), "{} requires a non-empty JSON file path", option);
        path = *value;
    }
    if (!path.has_value()) return std::nullopt;
    return LoadDiagnosticRunConfig(*path);
}

}  // namespace klvk
