#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "edt/math/matrix.hpp"
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

// A checkpoint is a hash of the rendered framebuffer at one frame. Recorded on
// a blessed run and re-checked on later ones, checkpoints answer the question a
// replay actually needs answered: not "is this bit-exact" but "at which frame
// did it stop matching".
struct DiagnosticCheckpoint
{
    u64 frame = 0;
    u64 hash = 0;

    friend bool operator==(const DiagnosticCheckpoint&, const DiagnosticCheckpoint&) = default;
};

struct DiagnosticCheckpointConfig
{
    // Checkpoints are taken at every multiple of this frame count.
    u64 every_frames = 0;
    bool include_ui = false;
    // Empty until a run is blessed; a later run compares against these.
    std::vector<DiagnosticCheckpoint> expected;
};

struct DiagnosticMouseMoveInput
{
    edt::Vec2f position{};

    friend bool operator==(const DiagnosticMouseMoveInput&, const DiagnosticMouseMoveInput&) = default;
};

struct DiagnosticMouseButtonInput
{
    MouseButton button = MouseButton::Left;
    InputAction action = InputAction::Release;

    friend bool operator==(const DiagnosticMouseButtonInput&, const DiagnosticMouseButtonInput&) = default;
};

struct DiagnosticMouseScrollInput
{
    edt::Vec2f offset{};

    friend bool operator==(const DiagnosticMouseScrollInput&, const DiagnosticMouseScrollInput&) = default;
};

struct DiagnosticKeyInput
{
    Key key = Key::Tab;
    InputAction action = InputAction::Release;

    friend bool operator==(const DiagnosticKeyInput&, const DiagnosticKeyInput&) = default;
};

using DiagnosticInputEvent =
    std::variant<DiagnosticMouseMoveInput, DiagnosticMouseButtonInput, DiagnosticMouseScrollInput, DiagnosticKeyInput>;

struct DiagnosticInputConfig
{
    std::optional<u64> frame;
    std::optional<u64> time_ns;
    DiagnosticInputEvent event;
};

// What the user answered when the application asked for a file. A replay serves
// these back in the order they were recorded instead of putting a dialog on
// screen, so a session that opened one still replays headlessly.
struct DiagnosticDialogConfig
{
    u64 frame = 0;
    // Nothing when the dialog was dismissed. A relative path resolves against the
    // executable directory, so a recording survives being replayed from another
    // build tree.
    std::optional<std::filesystem::path> answer;

    friend bool operator==(const DiagnosticDialogConfig&, const DiagnosticDialogConfig&) = default;
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
    std::vector<DiagnosticDialogConfig> dialogs;
    std::optional<DiagnosticVideoConfig> video;
    std::optional<DiagnosticCheckpointConfig> checkpoints;
    DiagnosticExitConfig exit;
    nlohmann::json application = nlohmann::json::object();
};

// Every path in the result is absolute: those written relative in the document
// are resolved against the executable directory here, so nothing downstream has
// to know where the process was launched from.
[[nodiscard]] DiagnosticRunConfig LoadDiagnosticRunConfig(
    const std::filesystem::path& path,
    const std::filesystem::path& executable_directory);

// Inverse of the parser, kept beside it so the two cannot drift apart: the
// result is a document LoadDiagnosticRunConfig accepts verbatim. Times are
// written as exact 'time_ns'/'step_ns', never as seconds.
[[nodiscard]] nlohmann::json DiagnosticRunConfigToJson(const DiagnosticRunConfig& config);

struct DiagnosticCommandLine
{
    std::optional<std::filesystem::path> config_path;
    std::optional<std::filesystem::path> input_record_path;
    // Overrides the loaded configuration's presentation, so one recording can be
    // replayed offscreen in CI and in a real window while debugging without
    // editing the file.
    std::optional<DiagnosticPresentation> presentation;
    // Writes the configuration back with this run's checkpoint hashes filled in,
    // blessing it as the reference a later run is compared against.
    std::optional<std::filesystem::path> write_checkpoints_path;
};

// Recognizes every klvk option in one pass, each spelled either as
// '<option> <value>' or '<option>=<value>'. Arguments that do not begin with
// '--klvk-' belong to the application and are ignored; an unrecognized
// '--klvk-' argument is an error, so a typo cannot be silently swallowed.
[[nodiscard]] DiagnosticCommandLine ParseDiagnosticCommandLine(std::span<const std::string_view> arguments);

// Convenience over ParseDiagnosticCommandLine for --klvk-diagnostics alone.
[[nodiscard]] std::optional<DiagnosticRunConfig> LoadDiagnosticRunConfigFromArguments(
    std::span<const std::string_view> arguments,
    const std::filesystem::path& executable_directory);

}  // namespace klvk
