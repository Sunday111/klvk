#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"

namespace klvk
{

class DeviceContext;
class DiagnosticFramebufferReadbackTest;
class DiagnosticVideoRecorder;

class DiagnosticFramebufferReadback
{
public:
    DiagnosticFramebufferReadback(
        size_t frames_in_flight,
        const std::optional<DiagnosticCheckpointConfig>& checkpoints);

    bool Record(
        DeviceContext& context,
        vk::CommandBuffer command_buffer,
        size_t frame_in_flight,
        vk::Image image,
        vk::Format format,
        vk::Extent2D extent,
        vk::ImageLayout final_layout,
        const std::vector<std::filesystem::path>& paths,
        std::optional<u64> checkpoint_frame,
        std::optional<u64> video_frame);

    void ProcessCompletedFrame(size_t frame_in_flight, DiagnosticVideoRecorder& video);
    void ProcessAllCompleted(DiagnosticVideoRecorder& video);

    [[nodiscard]] const std::vector<DiagnosticCheckpoint>& GetCheckpoints() const noexcept { return checkpoints_; }
    [[nodiscard]] std::optional<DiagnosticCheckpoint> GetFirstDivergence() const noexcept { return first_divergence_; }
    void EnsureComplete() const;

private:
    friend class DiagnosticFramebufferReadbackTest;

    struct PendingReadback
    {
        GpuBuffer buffer;
        vk::Format format = vk::Format::eUndefined;
        vk::Extent2D extent{};
        std::vector<std::filesystem::path> paths;
        std::optional<u64> video_frame;
        std::optional<u64> checkpoint_frame;
        u64 submission_sequence = 0;
    };

    [[nodiscard]] static bool IsCaptureFormat(vk::Format format) noexcept;
    [[nodiscard]] static size_t CheckedPixelCount(vk::Extent2D extent);
    [[nodiscard]] static u64 HashPixels(std::span<const std::byte> pixels) noexcept;
    static void
    WritePpm(const std::filesystem::path& path, vk::Extent2D extent, std::span<const std::byte> pixels, bool bgra);

    void ProcessReadback(PendingReadback& readback, DiagnosticVideoRecorder& video);
    void RecordCheckpoint(u64 frame, std::span<const std::byte> pixels);

    std::vector<std::vector<PendingReadback>> pending_;
    std::vector<DiagnosticCheckpoint> checkpoints_;
    std::vector<DiagnosticCheckpoint> expected_checkpoints_;
    std::optional<DiagnosticCheckpoint> first_divergence_;
    u64 next_submission_sequence_ = 0;
};

}  // namespace klvk
