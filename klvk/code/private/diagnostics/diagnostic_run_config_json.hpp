#pragma once

#include <array>
#include <filesystem>

#include "json/json_reader.hpp"
#include "klvk/diagnostics/diagnostic_run_config.hpp"

namespace klvk
{

// The diagnostic document, read and written in one place. Reading produces a
// whole DiagnosticRunConfig with every field validated and every path resolved,
// so nothing downstream ever sees the document itself.
//
// Writing lives here too because the two directions share one table per
// enumeration: a value cannot become readable but unwritable, or be spelled
// differently in each direction.
class DiagnosticRunConfigJson
{
public:
    [[nodiscard]] static DiagnosticRunConfig Read(
        const JsonReader& root,
        const std::filesystem::path& executable_directory);

    [[nodiscard]] static nlohmann::json Write(const DiagnosticRunConfig& config);

    // The command line accepts the same presentation names the document does, so
    // it asks here instead of keeping a second table that could drift.
    [[nodiscard]] static std::optional<DiagnosticPresentation> PresentationFromName(std::string_view name) noexcept;

private:
    static constexpr auto kPresentationNames = std::to_array<JsonReader::EnumName<DiagnosticPresentation>>({
        {.name = "visible", .value = DiagnosticPresentation::Visible},
        {.name = "hidden", .value = DiagnosticPresentation::Hidden},
        {.name = "offscreen", .value = DiagnosticPresentation::Offscreen},
    });
    static constexpr std::string_view kPresentationExpectation = "'visible', 'hidden', or 'offscreen'";

    static constexpr auto kEncodingNames = std::to_array<JsonReader::EnumName<DiagnosticVideoEncoding>>({
        {.name = "av1", .value = DiagnosticVideoEncoding::Av1},
        {.name = "h264", .value = DiagnosticVideoEncoding::H264},
        {.name = "mpeg4", .value = DiagnosticVideoEncoding::Mpeg4},
    });
    static constexpr std::string_view kEncodingExpectation = "'av1', 'h264', or 'mpeg4'";

    static constexpr auto kEncodingDeviceNames = std::to_array<JsonReader::EnumName<DiagnosticVideoEncodingDevice>>({
        {.name = "cpu", .value = DiagnosticVideoEncodingDevice::Cpu},
        {.name = "gpu", .value = DiagnosticVideoEncodingDevice::Gpu},
    });
    static constexpr std::string_view kEncodingDeviceExpectation = "'cpu' or 'gpu'";

    static constexpr auto kActionNames = std::to_array<JsonReader::EnumName<InputAction>>({
        {.name = "press", .value = InputAction::Press},
        {.name = "release", .value = InputAction::Release},
    });
    static constexpr std::string_view kActionExpectation = "'press' or 'release'";

    static constexpr auto kMouseButtonNames = std::to_array<JsonReader::EnumName<MouseButton>>({
        {.name = "left", .value = MouseButton::Left},
        {.name = "right", .value = MouseButton::Right},
        {.name = "middle", .value = MouseButton::Middle},
        {.name = "button4", .value = MouseButton::Button4},
        {.name = "button5", .value = MouseButton::Button5},
    });
    static constexpr std::string_view kMouseButtonExpectation = "'left', 'right', 'middle', 'button4', or 'button5'";

    // Exactly one of these is present on every scheduled entry, spelled as a
    // frame or as a time. Read in one place so captures, input and exit agree.
    struct Trigger
    {
        std::optional<u64> frame;
        std::optional<u64> time_ns;
    };

    [[nodiscard]] static std::optional<u64> ReadOptionalFrame(const JsonReader& object);
    [[nodiscard]] static std::optional<u64> ReadOptionalTime(const JsonReader& object);
    [[nodiscard]] static Trigger ReadTrigger(const JsonReader& object);

    [[nodiscard]] static edt::Vec2f ReadFloatVector(const JsonReader& value);
    [[nodiscard]] static std::optional<edt::Vec2<u32>> ReadFramebufferSize(const JsonReader& root);
    [[nodiscard]] static std::optional<u64> ReadClock(const JsonReader& root);
    [[nodiscard]] static DiagnosticInputConfig ReadInput(const JsonReader& value);
    [[nodiscard]] static DiagnosticCaptureConfig ReadCapture(const JsonReader& value);
    [[nodiscard]] static DiagnosticDialogConfig ReadDialog(const JsonReader& value);
    [[nodiscard]] static DiagnosticVideoConfig ReadVideo(const JsonReader& value);
    [[nodiscard]] static DiagnosticCheckpointConfig ReadCheckpoints(const JsonReader& value);
    static void ReadExit(const JsonReader& root, DiagnosticRunConfig& config);

    // Checks that need more than one field, run once the whole document is in.
    static void ValidateCombination(const DiagnosticRunConfig& config);
    static void ValidateExitDomain(const DiagnosticRunConfig& config);
    static void ResolvePaths(DiagnosticRunConfig& config, const std::filesystem::path& executable_directory);

    static void
    WriteTrigger(nlohmann::json& object, const std::optional<u64>& frame, const std::optional<u64>& time_ns);
    [[nodiscard]] static nlohmann::json WriteInputEvent(const DiagnosticInputEvent& event);
};

}  // namespace klvk
