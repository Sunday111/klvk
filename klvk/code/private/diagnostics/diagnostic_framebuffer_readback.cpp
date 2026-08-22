#include "diagnostic_framebuffer_readback.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

#include "diagnostic_video_recorder.hpp"
#include "edt/functional/on_scope_leave.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

DiagnosticFramebufferReadback::DiagnosticFramebufferReadback(
    size_t frames_in_flight,
    const std::optional<DiagnosticCheckpointConfig>& checkpoints)
    : pending_(frames_in_flight)
{
    if (checkpoints.has_value()) expected_checkpoints_ = checkpoints->expected;
}

bool DiagnosticFramebufferReadback::IsCaptureFormat(vk::Format format) noexcept
{
    return format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb ||
           format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eR8G8B8A8Srgb;
}

size_t DiagnosticFramebufferReadback::CheckedPixelCount(vk::Extent2D extent)
{
    const u64 pixel_count = static_cast<u64>(extent.width) * extent.height;
    ErrorHandling::Ensure(
        pixel_count <= std::numeric_limits<vk::DeviceSize>::max() / 4,
        "Diagnostic framebuffer is too large for a Vulkan readback buffer");
    ErrorHandling::Ensure(
        pixel_count <= std::numeric_limits<size_t>::max() / 4,
        "Diagnostic framebuffer is too large for this process address space");
    ErrorHandling::Ensure(
        pixel_count <= static_cast<u64>(std::numeric_limits<std::streamsize>::max()) / 3,
        "Diagnostic framebuffer is too large for PPM output");
    return static_cast<size_t>(pixel_count);
}

u64 DiagnosticFramebufferReadback::HashPixels(std::span<const std::byte> pixels) noexcept
{
    constexpr u64 kOffsetBasis = 14'695'981'039'346'656'037ULL;
    constexpr u64 kPrime = 1'099'511'628'211ULL;
    u64 hash = kOffsetBasis;
    for (const std::byte value : pixels)
    {
        hash ^= static_cast<u64>(std::to_integer<unsigned char>(value));
        hash *= kPrime;
    }
    return hash;
}

bool DiagnosticFramebufferReadback::Record(
    DeviceContext& context,
    vk::CommandBuffer command_buffer,
    size_t frame_in_flight,
    vk::Image image,
    vk::Format format,
    vk::Extent2D extent,
    vk::ImageLayout final_layout,
    const std::vector<std::filesystem::path>& paths,
    std::optional<u64> checkpoint_frame,
    std::optional<u64> video_frame)
{
    static_assert(std::is_nothrow_move_constructible_v<PendingReadback>);
    if (paths.empty() && !checkpoint_frame.has_value() && !video_frame.has_value()) return false;

    ErrorHandling::Ensure(frame_in_flight < pending_.size(), "Invalid diagnostic frame-in-flight index");
    ErrorHandling::Ensure(extent.width != 0 && extent.height != 0, "Cannot capture an empty framebuffer");
    ErrorHandling::Ensure(
        IsCaptureFormat(format),
        "Diagnostic readback does not support Vulkan format {}",
        static_cast<int>(format));
    const size_t pixel_count = CheckedPixelCount(extent);
    const vk::DeviceSize byte_size = static_cast<vk::DeviceSize>(pixel_count) * 4;

    vk::PipelineStageFlags2 final_stage_mask{};
    vk::AccessFlags2 final_access_mask{};
    if (final_layout == vk::ImageLayout::eColorAttachmentOptimal)
    {
        final_stage_mask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        final_access_mask = vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
    }
    else
    {
        ErrorHandling::Ensure(
            final_layout == vk::ImageLayout::ePresentSrcKHR,
            "Unsupported diagnostic capture final image layout");
        final_stage_mask = vk::PipelineStageFlagBits2::eNone;
        final_access_mask = vk::AccessFlagBits2::eNone;
    }

    auto& pending_frame = pending_[frame_in_flight];
    ErrorHandling::Ensure(
        pending_frame.size() < pending_frame.max_size(),
        "Too many pending diagnostic captures for one frame");
    pending_frame.reserve(pending_frame.size() + 1);

    PendingReadback pending{
        .buffer = GpuBuffer(context, vk::BufferUsageFlagBits::eTransferDst, byte_size, GpuBufferHostAccess::Random),
        .format = format,
        .extent = extent,
        .paths = paths,
        .video_frame = video_frame,
        .checkpoint_frame = checkpoint_frame,
        .submission_sequence = next_submission_sequence_++};

    const auto range =
        vk::ImageSubresourceRange{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLevelCount(1).setLayerCount(1);
    auto barrier = vk::ImageMemoryBarrier2{}
                       .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                       .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                       .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)
                       .setDstAccessMask(vk::AccessFlagBits2::eTransferRead)
                       .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                       .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                       .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                       .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                       .setImage(image)
                       .setSubresourceRange(range);
    const auto dependency = [&]
    {
        return vk::DependencyInfo{}.setImageMemoryBarrierCount(1).setPImageMemoryBarriers(&barrier);
    };
    command_buffer.pipelineBarrier2(dependency());
    const auto layers = vk::ImageSubresourceLayers{}.setAspectMask(vk::ImageAspectFlagBits::eColor).setLayerCount(1);
    const std::array regions{
        vk::BufferImageCopy{}.setImageSubresource(layers).setImageExtent(vk::Extent3D{extent.width, extent.height, 1})};
    command_buffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, pending.buffer.GetHandle(), regions);

    barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
    barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
    barrier.newLayout = final_layout;
    barrier.dstStageMask = final_stage_mask;
    barrier.dstAccessMask = final_access_mask;
    command_buffer.pipelineBarrier2(dependency());
    pending_frame.push_back(std::move(pending));
    return true;
}

void DiagnosticFramebufferReadback::WritePpm(
    const std::filesystem::path& path,
    vk::Extent2D extent,
    std::span<const std::byte> pixels,
    bool bgra)
{
    const size_t pixel_count = CheckedPixelCount(extent);
    std::vector<char> rgb(pixel_count * 3);
    for (size_t pixel = 0; pixel != pixel_count; ++pixel)
    {
        const auto channel = [&](size_t index)
        {
            return static_cast<char>(pixels[pixel * 4 + index]);
        };
        rgb[pixel * 3] = channel(bgra ? 2 : 0);
        rgb[pixel * 3 + 1] = channel(1);
        rgb[pixel * 3 + 2] = channel(bgra ? 0 : 2);
    }

    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::filesystem::path temporary = path;
    static std::atomic<u64> temporary_sequence = 0;
    temporary +=
        fmt::format(".tmp.{}.{}", os::GetProcessId(), temporary_sequence.fetch_add(1, std::memory_order_relaxed));
    bool installed = false;
    auto remove_temporary = edt::OnScopeLeave(
        [&]
        {
            if (installed) return;
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        });
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        ErrorHandling::Ensure(stream.is_open(), "Failed to open diagnostic capture '{}'", temporary.string());
        stream << fmt::format("P6\n{} {}\n255\n", extent.width, extent.height);
        stream.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
        stream.flush();
        ErrorHandling::Ensure(stream.good(), "Failed to flush diagnostic capture '{}'", temporary.string());
        stream.close();
        ErrorHandling::Ensure(!stream.fail(), "Failed to close diagnostic capture '{}'", temporary.string());
    }
    Filesystem::InstallFileAtomically(temporary, path);
    installed = true;
    fmt::println("klvk: captured {}x{} framebuffer to {}", extent.width, extent.height, path.string());
}

void DiagnosticFramebufferReadback::ProcessReadback(PendingReadback& readback, DiagnosticVideoRecorder& video)
{
    const bool bgra = readback.format == vk::Format::eB8G8R8A8Unorm || readback.format == vk::Format::eB8G8R8A8Srgb;
    ErrorHandling::Ensure(IsCaptureFormat(readback.format), "Pending diagnostic capture has an invalid format");
    const size_t pixel_count = CheckedPixelCount(readback.extent);
    std::vector<std::byte> pixels(pixel_count * 4);
    readback.buffer.Read(pixels);
    if (readback.checkpoint_frame.has_value()) RecordCheckpoint(*readback.checkpoint_frame, pixels);
    for (const std::filesystem::path& path : readback.paths) WritePpm(path, readback.extent, pixels, bgra);
    if (readback.video_frame.has_value()) video.WriteFrame(std::move(pixels), bgra, *readback.video_frame);
}

void DiagnosticFramebufferReadback::ProcessCompletedFrame(size_t frame_in_flight, DiagnosticVideoRecorder& video)
{
    ErrorHandling::Ensure(frame_in_flight < pending_.size(), "Invalid diagnostic frame-in-flight index");
    for (PendingReadback& readback : pending_[frame_in_flight]) ProcessReadback(readback, video);
    pending_[frame_in_flight].clear();
}

void DiagnosticFramebufferReadback::ProcessAllCompleted(DiagnosticVideoRecorder& video)
{
    std::vector<PendingReadback> remaining;
    for (auto& pending_frame : pending_)
    {
        std::ranges::move(pending_frame, std::back_inserter(remaining));
        pending_frame.clear();
    }
    std::ranges::sort(remaining, {}, &PendingReadback::submission_sequence);
    for (PendingReadback& readback : remaining) ProcessReadback(readback, video);
}

void DiagnosticFramebufferReadback::RecordCheckpoint(u64 frame, std::span<const std::byte> pixels)
{
    const DiagnosticCheckpoint checkpoint{.frame = frame, .hash = HashPixels(pixels)};
    checkpoints_.push_back(checkpoint);

    const auto expected = std::ranges::find(expected_checkpoints_, frame, &DiagnosticCheckpoint::frame);
    if (expected == std::ranges::end(expected_checkpoints_) || expected->hash == checkpoint.hash) return;
    if (first_divergence_.has_value() && first_divergence_->frame <= frame) return;
    first_divergence_ = checkpoint;
}

void DiagnosticFramebufferReadback::EnsureComplete() const
{
    if (!first_divergence_.has_value()) return;
    const auto expected =
        std::ranges::find(expected_checkpoints_, first_divergence_->frame, &DiagnosticCheckpoint::frame);
    ErrorHandling::ThrowWithMessage(
        "Diagnostic replay diverged at frame {}: expected checkpoint hash {}, got {}",
        first_divergence_->frame,
        expected == std::ranges::end(expected_checkpoints_) ? 0 : expected->hash,
        first_divergence_->hash);
}

}  // namespace klvk
