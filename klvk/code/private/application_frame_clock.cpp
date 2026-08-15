#include "application_frame_clock.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

#include "klvk/error_handling.hpp"

namespace klvk
{
namespace
{

constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

}  // namespace

void ApplicationFrameClock::Initialize(std::optional<u64> fixed_step_nanoseconds)
{
    fixed_step_nanoseconds_ = fixed_step_nanoseconds;
    app_start_time_ = Clock::now();
    std::ranges::fill(frame_start_time_history_, app_start_time_);
}

void ApplicationFrameClock::RegisterFrameStart()
{
    if (const auto step = GetFixedStepSeconds())
    {
        last_frame_duration_seconds_ = static_cast<float>(*step);
        framerate_ = static_cast<float>(1.0 / *step);
        return;
    }

    const TimePoint previous_frame_start_time = frame_start_time_history_[current_frame_time_index_];
    current_frame_time_index_ = (current_frame_time_index_ + 1) % frame_start_time_history_.size();
    const TimePoint current_frame_start_time = Clock::now();
    const TimePoint oldest_frame_start_time =
        std::exchange(frame_start_time_history_[current_frame_time_index_], current_frame_start_time);

    framerate_ = static_cast<float>(
        static_cast<double>(frame_start_time_history_.size()) /
        DurationToSeconds<double>(current_frame_start_time - oldest_frame_start_time));
    last_frame_duration_seconds_ = DurationToSeconds<float>(current_frame_start_time - previous_frame_start_time);
}

void ApplicationFrameClock::SetTargetFramerate(std::optional<float> framerate)
{
    ErrorHandling::Ensure(
        !framerate.has_value() || (std::isfinite(*framerate) && *framerate > 0.f),
        "Target framerate must be finite and positive");
    target_framerate_ = framerate;
}

void ApplicationFrameClock::AlignWithFramerate(bool pace_fixed_step_to_real_time, u64 completed_frames) const
{
    if (pace_fixed_step_to_real_time)
    {
        PaceFixedStepToRealTime(completed_frames);
        return;
    }
    if (fixed_step_nanoseconds_.has_value() || !target_framerate_.has_value()) return;

    const float frame_start = GetCurrentFrameStartTime(completed_frames);
    const float target_frame_duration = (1.f / *target_framerate_) * 0.9995f;
    while (GetRelativeTimeSeconds(completed_frames) - frame_start < target_frame_duration)
    {
    }
}

TimerDuration ApplicationFrameClock::GetElapsedTime(u64 completed_frames) const
{
    if (fixed_step_nanoseconds_.has_value())
    {
        ErrorHandling::Ensure(
            completed_frames == 0 || *fixed_step_nanoseconds_ <= std::numeric_limits<u64>::max() / completed_frames,
            "Diagnostic logical time overflowed the nanosecond range");
        return TimerDuration{*fixed_step_nanoseconds_ * completed_frames};
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - app_start_time_).count();
    return TimerDuration{elapsed > 0 ? static_cast<u64>(elapsed) : 0};
}

float ApplicationFrameClock::GetRelativeTimeSeconds(u64 completed_frames) const
{
    if (const auto step = GetFixedStepSeconds())
    {
        return static_cast<float>(static_cast<double>(completed_frames) * *step);
    }
    return DurationToSeconds(Clock::now() - app_start_time_);
}

float ApplicationFrameClock::GetCurrentFrameStartTime(u64 completed_frames) const
{
    if (fixed_step_nanoseconds_.has_value()) return GetRelativeTimeSeconds(completed_frames);
    return DurationToSeconds(frame_start_time_history_[current_frame_time_index_] - app_start_time_);
}

float ApplicationFrameClock::GetFramerate() const noexcept
{
    return framerate_;
}

float ApplicationFrameClock::GetLastFrameDurationSeconds() const noexcept
{
    return last_frame_duration_seconds_;
}

std::optional<double> ApplicationFrameClock::GetFixedStepSeconds() const noexcept
{
    if (!fixed_step_nanoseconds_.has_value()) return std::nullopt;
    return static_cast<double>(*fixed_step_nanoseconds_) / kNanosecondsPerSecond;
}

void ApplicationFrameClock::PaceFixedStepToRealTime(u64 completed_frames) const
{
    const u64 step_nanoseconds = *fixed_step_nanoseconds_;
    if (completed_frames != 0 && step_nanoseconds > std::numeric_limits<u64>::max() / completed_frames) return;
    const auto elapsed = std::chrono::nanoseconds{static_cast<i64>(step_nanoseconds * completed_frames)};
    std::this_thread::sleep_until(app_start_time_ + elapsed);
}

}  // namespace klvk
