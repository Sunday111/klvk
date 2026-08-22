#include "diagnostic_video_recorder.hpp"

#include <utility>

#include "diagnostic_video_encoder.hpp"
#include "klvk/error_handling.hpp"

namespace klvk
{

DiagnosticVideoRecorder::DiagnosticVideoRecorder(const DiagnosticRunConfig& config)
{
    if (!config.video.has_value()) return;

    ErrorHandling::Ensure(
        config.framebuffer_size.has_value() && config.clock.fixed_step_ns.has_value(),
        "Diagnostic video configuration was not validated");
    const auto size = *config.framebuffer_size;
    encoder_ = std::make_unique<DiagnosticVideoEncoder>(
        config.video->path,
        size.x(),
        size.y(),
        *config.clock.fixed_step_ns,
        config.video->encoding,
        config.video->encoding_device,
        config.video->compression_level,
        config.video->log_ffmpeg);
    include_ui_ = config.video->include_ui;
}

DiagnosticVideoRecorder::~DiagnosticVideoRecorder() = default;

bool DiagnosticVideoRecorder::NeedsFrame(bool include_ui) const noexcept
{
    return encoder_ != nullptr && include_ui_ == include_ui;
}

std::optional<u64> DiagnosticVideoRecorder::ReserveFrame(bool include_ui) noexcept
{
    if (!NeedsFrame(include_ui)) return std::nullopt;
    return next_frame_++;
}

void DiagnosticVideoRecorder::WriteFrame(std::vector<std::byte> pixels, bool bgra, u64 frame_index)
{
    ErrorHandling::Ensure(encoder_ != nullptr, "Diagnostic video frame has no encoder");
    encoder_->WriteFrame(std::move(pixels), bgra, frame_index);
}

void DiagnosticVideoRecorder::Finish()
{
    if (encoder_ != nullptr) encoder_->Finish();
}

}  // namespace klvk
