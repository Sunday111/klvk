#include "klvk/diagnostics/diagnostic_run_config.hpp"

#include <fstream>
#include <string>
#include <unordered_set>

#include "diagnostics/diagnostic_run_config_json.hpp"
#include "json/json_reader.hpp"
#include "klvk/error_handling.hpp"

namespace klvk
{
DiagnosticRunConfig LoadDiagnosticRunConfig(
    const std::filesystem::path& path,
    const std::filesystem::path& executable_directory)
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
        const nlohmann::json document = nlohmann::json::parse(stream, callback);
        return DiagnosticRunConfigJson::Read(JsonReader{document, "root"}, executable_directory);
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
    constexpr std::string_view kWriteCheckpointsOption = "--klvk-write-checkpoints";

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
        if (const auto value = MatchOption(arguments, index, kWriteCheckpointsOption))
        {
            AssignOnce(result.write_checkpoints_path, *value, kWriteCheckpointsOption);
            continue;
        }
        if (const auto value = MatchOption(arguments, index, kPresentationOption))
        {
            ErrorHandling::Ensure(
                !result.presentation.has_value(),
                "{} was specified more than once",
                kPresentationOption);
            const auto parsed = DiagnosticRunConfigJson::PresentationFromName(*value);
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

std::optional<DiagnosticRunConfig> LoadDiagnosticRunConfigFromArguments(
    std::span<const std::string_view> arguments,
    const std::filesystem::path& executable_directory)
{
    const DiagnosticCommandLine command_line = ParseDiagnosticCommandLine(arguments);
    if (!command_line.config_path.has_value()) return std::nullopt;
    return LoadDiagnosticRunConfig(*command_line.config_path, executable_directory);
}

nlohmann::json DiagnosticRunConfigToJson(const DiagnosticRunConfig& config)
{
    return DiagnosticRunConfigJson::Write(config);
}

}  // namespace klvk
