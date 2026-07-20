#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/input.hpp"
#include "klvk/integral_aliases.hpp"

namespace klvk
{

enum class DiagnosticPresentation : u8
{
    Visible,
    Hidden,
    Offscreen,
};

struct DiagnosticClockConfig
{
    std::optional<double> fixed_step_seconds;
};

struct DiagnosticCaptureConfig
{
    std::optional<u64> frame;
    std::optional<double> time_seconds;
    std::filesystem::path path;
    bool include_ui = true;
};

struct DiagnosticMouseMoveInput
{
    edt::Vec2f position{};
};

struct DiagnosticMouseButtonInput
{
    MouseButton button = MouseButton::Left;
    InputAction action = InputAction::Release;
};

struct DiagnosticMouseScrollInput
{
    edt::Vec2f offset{};
};

struct DiagnosticKeyInput
{
    Key key = Key::Tab;
    InputAction action = InputAction::Release;
};

using DiagnosticInputEvent =
    std::variant<DiagnosticMouseMoveInput, DiagnosticMouseButtonInput, DiagnosticMouseScrollInput, DiagnosticKeyInput>;

struct DiagnosticInputConfig
{
    std::optional<u64> frame;
    std::optional<double> time_seconds;
    DiagnosticInputEvent event;
};

struct DiagnosticExitConfig
{
    std::optional<u64> frame;
    std::optional<double> time_seconds;
    bool after_last_capture = false;
};

struct DiagnosticRunConfig
{
    static constexpr u32 kVersion = 1;

    DiagnosticPresentation presentation = DiagnosticPresentation::Hidden;
    std::optional<edt::Vec2<u32>> framebuffer_size;
    DiagnosticClockConfig clock;
    std::vector<DiagnosticInputConfig> input;
    std::vector<DiagnosticCaptureConfig> captures;
    DiagnosticExitConfig exit;
    nlohmann::json application = nlohmann::json::object();
};

[[nodiscard]] DiagnosticRunConfig LoadDiagnosticRunConfig(const std::filesystem::path& path);

// Finds --klvk-diagnostics <path> or --klvk-diagnostics=<path>. Other arguments
// belong to the application and are ignored by this parser.
[[nodiscard]] std::optional<DiagnosticRunConfig> LoadDiagnosticRunConfigFromArguments(
    std::span<const std::string_view> arguments);

}  // namespace klvk
