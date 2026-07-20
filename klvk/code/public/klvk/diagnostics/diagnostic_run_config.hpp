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

// Trigger points are exact nanoseconds. A configuration may spell them in
// seconds for a human author; the parser rounds once and everything downstream
// works in integers, so a replayed run reproduces its recording exactly.
struct DiagnosticClockConfig
{
    std::optional<u64> fixed_step_ns;
};

struct DiagnosticCaptureConfig
{
    std::optional<u64> frame;
    std::optional<u64> time_ns;
    std::filesystem::path path;
    bool include_ui = true;
};

enum class DiagnosticVideoEncoding : u8
{
    Av1,
    H264,
    Mpeg4,
};

enum class DiagnosticVideoEncodingDevice : u8
{
    Cpu,
    Gpu,
};

struct DiagnosticVideoConfig
{
    static constexpr u8 kMaximumCompressionLevel = 10;

    std::filesystem::path path;
    DiagnosticVideoEncoding encoding = DiagnosticVideoEncoding::Av1;
    DiagnosticVideoEncodingDevice encoding_device = DiagnosticVideoEncodingDevice::Cpu;
    u8 compression_level = 3;
    bool include_ui = true;
    bool log_ffmpeg = true;
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
    std::optional<u64> time_ns;
    DiagnosticInputEvent event;
};

struct DiagnosticExitConfig
{
    std::optional<u64> frame;
    std::optional<u64> time_ns;
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
    std::optional<DiagnosticVideoConfig> video;
    DiagnosticExitConfig exit;
    nlohmann::json application = nlohmann::json::object();
};

[[nodiscard]] DiagnosticRunConfig LoadDiagnosticRunConfig(const std::filesystem::path& path);

// Finds --klvk-diagnostics <path> or --klvk-diagnostics=<path>. Other arguments
// belong to the application and are ignored by this parser.
[[nodiscard]] std::optional<DiagnosticRunConfig> LoadDiagnosticRunConfigFromArguments(
    std::span<const std::string_view> arguments);

}  // namespace klvk
