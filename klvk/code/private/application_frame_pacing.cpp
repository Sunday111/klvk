#include "application_frame_pacing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "klvk/error_handling.hpp"

namespace klvk
{
namespace
{

constexpr long double kNanosecondsPerSecond = 1'000'000'000.0L;
constexpr float kMaximumTargetFramerate = 1'000'000'000.f;

}  // namespace

void FramePacingSchedule::Reset() noexcept
{
    target_schedule_start_.reset();
    next_target_frame_ = 1;
}

void FramePacingSchedule::SetTargetFramerate(std::optional<float> framerate)
{
    ErrorHandling::Ensure(
        !framerate.has_value() ||
            (std::isfinite(*framerate) && *framerate > 0.f && *framerate <= kMaximumTargetFramerate),
        "Target framerate must be finite, positive, and no greater than the nanosecond clock resolution");
    if (target_framerate_ == framerate) return;
    target_framerate_ = framerate;
    Reset();
}

bool FramePacingSchedule::HasTargetFramerate() const noexcept
{
    return target_framerate_.has_value();
}

std::optional<std::chrono::nanoseconds> FramePacingSchedule::GetDeadline(const FramePacingFrame& frame)
{
    if (frame.fixed_step_nanoseconds.has_value())
    {
        Reset();
        return GetFixedStepDeadline(frame);
    }
    if (!target_framerate_.has_value())
    {
        Reset();
        return std::nullopt;
    }

    if (!target_schedule_start_.has_value()) target_schedule_start_ = frame.frame_start;
    const std::optional<std::chrono::nanoseconds> deadline = GetTargetDeadline(next_target_frame_);
    if (!deadline.has_value()) return std::nullopt;
    if (*deadline <= frame.now)
    {
        AdvancePast(frame.now);
        return std::nullopt;
    }

    if (next_target_frame_ != std::numeric_limits<u64>::max()) ++next_target_frame_;
    return deadline;
}

std::optional<std::chrono::nanoseconds> FramePacingSchedule::GetFixedStepDeadline(
    const FramePacingFrame& frame) const noexcept
{
    if (!frame.pace_fixed_step_to_real_time) return std::nullopt;

    const u64 step = *frame.fixed_step_nanoseconds;
    constexpr u64 kMaximumDuration = static_cast<u64>(std::numeric_limits<i64>::max());
    if (frame.completed_frames != 0 && step > kMaximumDuration / frame.completed_frames) return std::nullopt;

    const i64 elapsed = static_cast<i64>(step * frame.completed_frames);
    if (frame.application_start.count() > std::numeric_limits<i64>::max() - elapsed) return std::nullopt;
    return frame.application_start + std::chrono::nanoseconds{elapsed};
}

std::optional<std::chrono::nanoseconds> FramePacingSchedule::GetTargetDeadline(u64 frame_index) const noexcept
{
    const long double offset =
        static_cast<long double>(frame_index) * kNanosecondsPerSecond / static_cast<long double>(*target_framerate_);
    if (offset > static_cast<long double>(std::numeric_limits<i64>::max())) return std::nullopt;

    const i64 offset_nanoseconds = static_cast<i64>(offset);
    if (target_schedule_start_->count() > std::numeric_limits<i64>::max() - offset_nanoseconds)
    {
        return std::nullopt;
    }
    return *target_schedule_start_ + std::chrono::nanoseconds{offset_nanoseconds};
}

void FramePacingSchedule::AdvancePast(std::chrono::nanoseconds now) noexcept
{
    const auto elapsed = now - *target_schedule_start_;
    if (elapsed <= std::chrono::nanoseconds::zero()) return;

    const long double elapsed_frames = static_cast<long double>(elapsed.count()) *
                                       static_cast<long double>(*target_framerate_) / kNanosecondsPerSecond;
    if (elapsed_frames >= static_cast<long double>(std::numeric_limits<u64>::max()))
    {
        next_target_frame_ = std::numeric_limits<u64>::max();
        return;
    }

    next_target_frame_ = std::max(next_target_frame_, static_cast<u64>(std::floor(elapsed_frames)) + 1);
    for (std::optional<std::chrono::nanoseconds> deadline = GetTargetDeadline(next_target_frame_);
         deadline.has_value() && *deadline <= now && next_target_frame_ != std::numeric_limits<u64>::max();
         deadline = GetTargetDeadline(++next_target_frame_))
    {
    }
}

}  // namespace klvk
