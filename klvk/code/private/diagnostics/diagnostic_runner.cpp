#include "diagnostic_runner.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <set>

#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/template/on_scope_leave.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{
namespace
{

bool IsCaptureFormat(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB ||
           format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
}

size_t CheckedPixelCount(VkExtent2D extent)
{
    const u64 pixel_count = static_cast<u64>(extent.width) * extent.height;
    ErrorHandling::Ensure(
        pixel_count <= std::numeric_limits<VkDeviceSize>::max() / 4,
        "Diagnostic framebuffer is too large for a Vulkan readback buffer");
    ErrorHandling::Ensure(
        pixel_count <= std::numeric_limits<size_t>::max() / 4,
        "Diagnostic framebuffer is too large for this process address space");
    ErrorHandling::Ensure(
        pixel_count <= static_cast<u64>(std::numeric_limits<std::streamsize>::max()) / 3,
        "Diagnostic framebuffer is too large for PPM output");
    return static_cast<size_t>(pixel_count);
}

}  // namespace

DiagnosticRunner::DiagnosticRunner(
    DiagnosticRunConfig config,
    const std::filesystem::path& executable_directory,
    size_t frames_in_flight)
    : config_(std::move(config)),
      pending_(frames_in_flight)
{
    std::set<std::filesystem::path> resolved_paths;
    captures_.reserve(config_.captures.size());
    for (auto capture : config_.captures)
    {
        if (capture.path.is_relative()) capture.path = executable_directory / capture.path;
        capture.path = capture.path.lexically_normal();
        ErrorHandling::Ensure(
            capture.path.extension() == ".ppm",
            "Diagnostic capture '{}' must use the .ppm extension",
            capture.path.string());
        ErrorHandling::Ensure(
            resolved_paths.insert(capture.path).second,
            "Multiple diagnostic captures resolve to output path '{}'",
            capture.path.string());
        captures_.push_back({.config = std::move(capture)});
    }
}

bool DiagnosticRunner::IsDue(const Capture& capture, u64 frame, double time_seconds) const
{
    return !capture.recorded &&
           ((capture.config.frame.has_value() && frame >= *capture.config.frame) ||
            (capture.config.time_seconds.has_value() && time_seconds >= *capture.config.time_seconds));
}

bool DiagnosticRunner::HasDueCaptures(u64 frame, double time_seconds, bool include_ui) const
{
    return std::ranges::any_of(
        captures_,
        [&](const Capture& capture)
        { return capture.config.include_ui == include_ui && IsDue(capture, frame, time_seconds); });
}

bool DiagnosticRunner::RecordDueCaptures(
    DeviceContext& context,
    VkCommandBuffer command_buffer,
    size_t frame_in_flight,
    u64 frame,
    double time_seconds,
    bool include_ui,
    VkImage image,
    VkFormat format,
    VkExtent2D extent,
    VkImageLayout final_layout)
{
    std::vector<std::filesystem::path> paths;
    for (Capture& capture : captures_)
    {
        if (capture.config.include_ui != include_ui || !IsDue(capture, frame, time_seconds)) continue;
        capture.recorded = true;
        paths.push_back(capture.config.path);
    }
    if (paths.empty()) return false;

    ErrorHandling::Ensure(frame_in_flight < pending_.size(), "Invalid diagnostic frame-in-flight index");
    ErrorHandling::Ensure(extent.width != 0 && extent.height != 0, "Cannot capture an empty framebuffer");
    ErrorHandling::Ensure(
        IsCaptureFormat(format),
        "Diagnostic PPM capture does not support Vulkan format {}",
        static_cast<int>(format));
    const size_t pixel_count = CheckedPixelCount(extent);
    const VkDeviceSize byte_size = static_cast<VkDeviceSize>(pixel_count) * 4;
    auto& pending = pending_[frame_in_flight].emplace_back(
        PendingCapture{
            .buffer = GpuBuffer(context, VK_BUFFER_USAGE_TRANSFER_DST_BIT, byte_size, GpuBufferHostAccess::Random),
            .format = format,
            .extent = extent,
            .paths = std::move(paths)});

    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier};
    Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
    const std::array regions{VkBufferImageCopy{
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {.width = extent.width, .height = extent.height, .depth = 1}}};
    Vulkan::CmdCopyImageToBuffer(
        command_buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        pending.buffer.GetHandle(),
        regions);

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = final_layout;
    if (final_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else
    {
        ErrorHandling::Ensure(
            final_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            "Unsupported diagnostic capture final image layout");
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;
    }
    Vulkan::CmdPipelineBarrier2(command_buffer, dependency);
    return true;
}

void DiagnosticRunner::WritePpm(PendingCapture& capture)
{
    const bool bgra = capture.format == VK_FORMAT_B8G8R8A8_UNORM || capture.format == VK_FORMAT_B8G8R8A8_SRGB;
    ErrorHandling::Ensure(IsCaptureFormat(capture.format), "Pending diagnostic capture has an invalid format");
    const size_t pixel_count = CheckedPixelCount(capture.extent);
    std::vector<std::byte> source(pixel_count * 4);
    capture.buffer.Read(source);
    std::vector<char> rgb(pixel_count * 3);
    for (size_t pixel = 0; pixel != pixel_count; ++pixel)
    {
        const auto channel = [&](size_t index)
        {
            return static_cast<char>(source[pixel * 4 + index]);
        };
        rgb[pixel * 3] = channel(bgra ? 2 : 0);
        rgb[pixel * 3 + 1] = channel(1);
        rgb[pixel * 3 + 2] = channel(bgra ? 0 : 2);
    }

    for (const std::filesystem::path& path : capture.paths)
    {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::filesystem::path temporary = path;
        static std::atomic<u64> temporary_sequence = 0;
        temporary +=
            fmt::format(".tmp.{}.{}", os::GetProcessId(), temporary_sequence.fetch_add(1, std::memory_order_relaxed));
        bool installed = false;
        auto remove_temporary = OnScopeLeave(
            [&]
            {
                if (installed) return;
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            });
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            ErrorHandling::Ensure(stream.is_open(), "Failed to open diagnostic capture '{}'", temporary.string());
            stream << fmt::format("P6\n{} {}\n255\n", capture.extent.width, capture.extent.height);
            stream.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
            stream.flush();
            ErrorHandling::Ensure(stream.good(), "Failed to flush diagnostic capture '{}'", temporary.string());
            stream.close();
            ErrorHandling::Ensure(!stream.fail(), "Failed to close diagnostic capture '{}'", temporary.string());
        }
        Filesystem::InstallFileAtomically(temporary, path);
        installed = true;
        fmt::println(
            "klvk: captured {}x{} framebuffer to {}",
            capture.extent.width,
            capture.extent.height,
            path.string());
    }
}

void DiagnosticRunner::ProcessCompletedFrame(size_t frame_in_flight)
{
    ErrorHandling::Ensure(frame_in_flight < pending_.size(), "Invalid diagnostic frame-in-flight index");
    for (PendingCapture& capture : pending_[frame_in_flight]) WritePpm(capture);
    pending_[frame_in_flight].clear();
}

void DiagnosticRunner::ProcessAllCompleted()
{
    for (size_t frame = 0; frame != pending_.size(); ++frame) ProcessCompletedFrame(frame);
}

void DiagnosticRunner::EnsureComplete() const
{
    const size_t missing =
        static_cast<size_t>(std::ranges::count_if(captures_, [](const Capture& capture) { return !capture.recorded; }));
    ErrorHandling::Ensure(
        missing == 0,
        "Diagnostic run ended before {} requested capture{} could be recorded",
        missing,
        missing == 1 ? "" : "s");
}

bool DiagnosticRunner::ShouldExit(u64 frame, double time_seconds) const
{
    if (config_.exit.frame.has_value() && frame >= *config_.exit.frame) return true;
    if (config_.exit.time_seconds.has_value() && time_seconds >= *config_.exit.time_seconds) return true;
    return config_.exit.after_last_capture && std::ranges::all_of(captures_, &Capture::recorded);
}

}  // namespace klvk
