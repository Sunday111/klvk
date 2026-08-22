#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "klvk/diagnostics/diagnostic_run_config.hpp"

namespace klvk
{

class DiagnosticVideoEncoder;

class DiagnosticVideoRecorder
{
public:
    explicit DiagnosticVideoRecorder(const DiagnosticRunConfig& config);
    ~DiagnosticVideoRecorder();

    DiagnosticVideoRecorder(const DiagnosticVideoRecorder&) = delete;
    DiagnosticVideoRecorder(DiagnosticVideoRecorder&&) = delete;
    DiagnosticVideoRecorder& operator=(const DiagnosticVideoRecorder&) = delete;
    DiagnosticVideoRecorder& operator=(DiagnosticVideoRecorder&&) = delete;

    [[nodiscard]] bool NeedsFrame(bool include_ui) const noexcept;
    [[nodiscard]] std::optional<u64> ReserveFrame(bool include_ui) noexcept;
    void WriteFrame(std::vector<std::byte> pixels, bool bgra, u64 frame_index);
    void Finish();

private:
    std::unique_ptr<DiagnosticVideoEncoder> encoder_;
    u64 next_frame_ = 0;
    bool include_ui_ = true;
};

}  // namespace klvk
