#include "application_frame_clock.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <thread>
#include <utility>

#include "klvk/error_handling.hpp"

#if defined(__linux__)
#include <ctime>
#endif

namespace klvk
{
namespace
{

constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

template <typename Duration>
std::chrono::nanoseconds ToNanoseconds(Duration duration)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
}

void SleepUntil(std::chrono::steady_clock::time_point deadline)
{
#if defined(__linux__)
    const auto remaining = std::chrono::ceil<std::chrono::nanoseconds>(deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::nanoseconds::zero()) return;

    timespec sleep_deadline{};
    ErrorHandling::Ensure(
        clock_gettime(CLOCK_MONOTONIC, &sleep_deadline) == 0,
        "clock_gettime(CLOCK_MONOTONIC) failed with error {}",
        errno);

    constexpr i64 kNanosecondsPerSecondInteger = 1'000'000'000;
    sleep_deadline.tv_sec += remaining.count() / kNanosecondsPerSecondInteger;
    sleep_deadline.tv_nsec += remaining.count() % kNanosecondsPerSecondInteger;
    if (sleep_deadline.tv_nsec >= kNanosecondsPerSecondInteger)
    {
        ++sleep_deadline.tv_sec;
        sleep_deadline.tv_nsec -= kNanosecondsPerSecondInteger;
    }

    int result = 0;
    do
    {
        result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sleep_deadline, nullptr);
    } while (result == EINTR);
    ErrorHandling::Ensure(result == 0, "clock_nanosleep(CLOCK_MONOTONIC) failed with error {}", result);
#else
    std::this_thread::sleep_until(deadline);
#endif
}

}  // namespace

void ApplicationFrameClock::Initialize(
    std::optional<u64> fixed_step_nanoseconds,
    std::span<const u64> frame_durations_ns,
    TimePoint application_start)
{
    fixed_step_nanoseconds_ = fixed_step_nanoseconds;
    frame_durations_ns_ = frame_durations_ns;
    replay_frame_ = 0;
    replay_elapsed_ns_ = 0;
    app_start_time_ = application_start;
    std::ranges::fill(frame_start_time_history_, app_start_time_);
    pacing_schedule_.Reset();
}

void ApplicationFrameClock::RegisterFrameStart(bool pace_recorded_to_real_time, std::optional<TimePoint> frame_start)
{
    if (!frame_durations_ns_.empty())
    {
        last_frame_duration_ns_ = frame_durations_ns_[std::min(replay_frame_, frame_durations_ns_.size() - 1)];
        ErrorHandling::Ensure(
            last_frame_duration_ns_ <= std::numeric_limits<u64>::max() - replay_elapsed_ns_,
            "Recorded logical time overflowed the nanosecond range");
        if (pace_recorded_to_real_time)
        {
            if (const auto deadline = GetNextRecordedFrameStartDeadline()) SleepUntil(*deadline);
        }
        replay_elapsed_ns_ += last_frame_duration_ns_;
        ++replay_frame_;
        last_frame_duration_seconds_ = DurationToSeconds<float>(TimerDuration{last_frame_duration_ns_});
        framerate_ = 1.f / last_frame_duration_seconds_;
        return;
    }
    if (const auto step = GetFixedStepSeconds())
    {
        last_frame_duration_ns_ = *fixed_step_nanoseconds_;
        last_frame_duration_seconds_ = static_cast<float>(*step);
        framerate_ = static_cast<float>(1.0 / *step);
        return;
    }

    const TimePoint previous_frame_start_time = frame_start_time_history_[current_frame_time_index_];
    current_frame_time_index_ = (current_frame_time_index_ + 1) % frame_start_time_history_.size();
    const TimePoint current_frame_start_time = frame_start.has_value() ? *frame_start : Clock::now();
    const TimePoint oldest_frame_start_time =
        std::exchange(frame_start_time_history_[current_frame_time_index_], current_frame_start_time);

    framerate_ = static_cast<float>(
        static_cast<double>(frame_start_time_history_.size()) /
        DurationToSeconds<double>(current_frame_start_time - oldest_frame_start_time));
    last_frame_duration_ns_ =
        static_cast<u64>(ToNanoseconds(current_frame_start_time - previous_frame_start_time).count());
    last_frame_duration_seconds_ = DurationToSeconds<float>(current_frame_start_time - previous_frame_start_time);
}

std::optional<ApplicationFrameClock::TimePoint> ApplicationFrameClock::GetNextRecordedFrameStartDeadline() const
{
    if (frame_durations_ns_.empty()) return std::nullopt;
    const u64 next_duration = frame_durations_ns_[std::min(replay_frame_, frame_durations_ns_.size() - 1)];
    const auto maximum_delay = ToNanoseconds(TimePoint::max() - app_start_time_);
    const u64 maximum_elapsed = static_cast<u64>(maximum_delay.count());
    if (replay_elapsed_ns_ > maximum_elapsed || next_duration > maximum_elapsed - replay_elapsed_ns_)
        return std::nullopt;
    const auto elapsed = std::chrono::nanoseconds{static_cast<i64>(replay_elapsed_ns_ + next_duration)};
    return app_start_time_ + std::chrono::duration_cast<Clock::duration>(elapsed);
}

void ApplicationFrameClock::SetTargetFramerate(std::optional<float> framerate)
{
    pacing_schedule_.SetTargetFramerate(framerate);
}

void ApplicationFrameClock::AlignWithFramerate(bool pace_fixed_step_to_real_time, u64 completed_frames)
{
    if (!frame_durations_ns_.empty()) return;
    std::chrono::nanoseconds now{};
    if (!fixed_step_nanoseconds_.has_value() && pacing_schedule_.HasTargetFramerate())
    {
        now = ToNanoseconds(Clock::now().time_since_epoch());
    }
    const std::optional<std::chrono::nanoseconds> deadline = pacing_schedule_.GetDeadline(
        FramePacingFrame{
            .fixed_step_nanoseconds = fixed_step_nanoseconds_,
            .pace_fixed_step_to_real_time = pace_fixed_step_to_real_time,
            .completed_frames = completed_frames,
            .application_start = ToNanoseconds(app_start_time_.time_since_epoch()),
            .frame_start = ToNanoseconds(frame_start_time_history_[current_frame_time_index_].time_since_epoch()),
            .now = now,
        });
    if (deadline.has_value()) SleepUntil(TimePoint{std::chrono::duration_cast<Clock::duration>(*deadline)});
}

TimerDuration ApplicationFrameClock::GetElapsedTime(u64 completed_frames) const
{
    if (!frame_durations_ns_.empty()) return TimerDuration{replay_elapsed_ns_};
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
    if (!frame_durations_ns_.empty())
        return static_cast<float>(static_cast<double>(replay_elapsed_ns_) / kNanosecondsPerSecond);
    if (const auto step = GetFixedStepSeconds())
    {
        return static_cast<float>(static_cast<double>(completed_frames) * *step);
    }
    return DurationToSeconds(Clock::now() - app_start_time_);
}

float ApplicationFrameClock::GetCurrentFrameStartTime(u64 completed_frames) const
{
    if (fixed_step_nanoseconds_.has_value() || !frame_durations_ns_.empty())
        return GetRelativeTimeSeconds(completed_frames);
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

}  // namespace klvk
