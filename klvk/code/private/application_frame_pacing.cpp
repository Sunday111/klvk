#include "application_frame_pacing.hpp"

#include <bit>
#include <cmath>
#include <limits>

#include "klvk/error_handling.hpp"

namespace klvk
{
namespace
{

constexpr u64 kNanosecondsPerSecond = 1'000'000'000;
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
    target_period_ = framerate.has_value() ? CalculateTargetPeriod(*framerate) : std::nullopt;
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

std::optional<FramePacingSchedule::TargetPeriod> FramePacingSchedule::CalculateTargetPeriod(float framerate) noexcept
{
    static_assert(std::numeric_limits<float>::is_iec559);
    constexpr u32 kFractionBitCount = 23;
    constexpr u32 kFractionMask = (u32{1} << kFractionBitCount) - 1;
    constexpr u32 kExponentMask = 0xff;
    constexpr i32 kExponentBiasWithFraction = 127 + kFractionBitCount;

    const u32 bits = std::bit_cast<u32>(framerate);
    const u32 encoded_exponent = (bits >> kFractionBitCount) & kExponentMask;
    const u64 significand =
        encoded_exponent == 0 ? bits & kFractionMask : (u32{1} << kFractionBitCount) | (bits & kFractionMask);
    const i32 binary_exponent =
        encoded_exponent == 0 ? -149 : static_cast<i32>(encoded_exponent) - kExponentBiasWithFraction;

    u64 denominator = significand;
    u64 whole_nanoseconds = kNanosecondsPerSecond / denominator;
    u64 fractional_numerator = kNanosecondsPerSecond % denominator;
    if (binary_exponent >= 0)
    {
        denominator <<= static_cast<u32>(binary_exponent);
        whole_nanoseconds = kNanosecondsPerSecond / denominator;
        fractional_numerator = kNanosecondsPerSecond % denominator;
    }
    else
    {
        const u64 maximum_duration = static_cast<u64>(std::numeric_limits<i64>::max());
        for (i32 bit = 0; bit < -binary_exponent; ++bit)
        {
            const u64 doubled_fraction = fractional_numerator * 2;
            const u64 carry = doubled_fraction >= denominator ? 1 : 0;
            if (whole_nanoseconds > (maximum_duration - carry) / 2) return std::nullopt;
            whole_nanoseconds = whole_nanoseconds * 2 + carry;
            fractional_numerator = doubled_fraction - carry * denominator;
        }
    }

    return TargetPeriod{
        .whole_nanoseconds = whole_nanoseconds,
        .fractional_numerator = fractional_numerator,
        .fractional_denominator = denominator,
    };
}

std::optional<i64> FramePacingSchedule::GetTargetOffset(u64 frame_index) const noexcept
{
    if (!target_period_.has_value()) return std::nullopt;

    constexpr u64 kMaximumDuration = static_cast<u64>(std::numeric_limits<i64>::max());
    const TargetPeriod& period = *target_period_;
    if (frame_index != 0 && period.whole_nanoseconds > kMaximumDuration / frame_index) return std::nullopt;
    u64 offset = frame_index * period.whole_nanoseconds;

    const u64 complete_denominators = frame_index / period.fractional_denominator;
    if (complete_denominators != 0 && period.fractional_numerator > (kMaximumDuration - offset) / complete_denominators)
    {
        return std::nullopt;
    }
    offset += complete_denominators * period.fractional_numerator;

    const u64 remaining_numerator = (frame_index % period.fractional_denominator) * period.fractional_numerator;
    const u64 remaining_offset = remaining_numerator / period.fractional_denominator;
    if (remaining_offset > kMaximumDuration - offset) return std::nullopt;
    return static_cast<i64>(offset + remaining_offset);
}

std::optional<std::chrono::nanoseconds> FramePacingSchedule::GetTargetDeadline(u64 frame_index) const noexcept
{
    const std::optional<i64> offset = GetTargetOffset(frame_index);
    if (!offset.has_value() || target_schedule_start_->count() > std::numeric_limits<i64>::max() - *offset)
    {
        return std::nullopt;
    }
    return *target_schedule_start_ + std::chrono::nanoseconds{*offset};
}

void FramePacingSchedule::AdvancePast(std::chrono::nanoseconds now) noexcept
{
    u64 expired_frame = next_target_frame_;
    u64 search_step = 1;
    u64 future_frame = expired_frame;
    while (future_frame != std::numeric_limits<u64>::max())
    {
        future_frame = search_step > std::numeric_limits<u64>::max() - expired_frame ? std::numeric_limits<u64>::max()
                                                                                     : expired_frame + search_step;
        const std::optional<std::chrono::nanoseconds> deadline = GetTargetDeadline(future_frame);
        if (!deadline.has_value() || *deadline > now) break;
        expired_frame = future_frame;
        search_step =
            search_step > std::numeric_limits<u64>::max() / 2 ? std::numeric_limits<u64>::max() : search_step * 2;
    }

    if (future_frame == expired_frame)
    {
        next_target_frame_ = future_frame;
        return;
    }

    while (future_frame - expired_frame > 1)
    {
        const u64 candidate = expired_frame + (future_frame - expired_frame) / 2;
        const std::optional<std::chrono::nanoseconds> deadline = GetTargetDeadline(candidate);
        if (deadline.has_value() && *deadline <= now)
        {
            expired_frame = candidate;
        }
        else
        {
            future_frame = candidate;
        }
    }
    next_target_frame_ = future_frame;
}

}  // namespace klvk
