#include <type_traits>
#include <utility>

#include "diagnostic_run_config_json.hpp"
#include "klvk/error_handling.hpp"
#include "platform/input_mapping.hpp"

namespace klvk
{

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
