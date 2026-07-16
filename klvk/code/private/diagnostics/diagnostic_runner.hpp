#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"

namespace klvk
{

class DeviceContext;

class DiagnosticRunner
{
public:
    DiagnosticRunner(
        DiagnosticRunConfig config,
        const std::filesystem::path& executable_directory,
        size_t frames_in_flight);

    [[nodiscard]] bool HasDueCaptures(u64 frame, double time_seconds, bool include_ui) const;

    // The image must be in COLOR_ATTACHMENT_OPTIMAL. Returns true after recording
    // a copy and leaves it in final_layout.
    bool RecordDueCaptures(
        DeviceContext& context,
        VkCommandBuffer command_buffer,
        size_t frame_in_flight,
        u64 frame,
        double time_seconds,
        bool include_ui,
        VkImage image,
        VkFormat format,
        VkExtent2D extent,
        VkImageLayout final_layout);

    void ProcessCompletedFrame(size_t frame_in_flight);
    void ProcessAllCompleted();
    void EnsureComplete() const;

    [[nodiscard]] bool ShouldExit(u64 frame, double time_seconds) const;

private:
    struct Capture
    {
        DiagnosticCaptureConfig config;
        bool recorded = false;
    };

    struct PendingCapture
    {
        GpuBuffer buffer;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        std::vector<std::filesystem::path> paths;
    };

    [[nodiscard]] bool IsDue(const Capture& capture, u64 frame, double time_seconds) const;
    static void WritePpm(PendingCapture& capture);

    DiagnosticRunConfig config_;
    std::vector<Capture> captures_;
    std::vector<std::vector<PendingCapture>> pending_;
};

}  // namespace klvk
