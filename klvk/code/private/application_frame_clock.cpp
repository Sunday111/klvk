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
    frame_start_time_ = application_start;
    live_elapsed_offset_ = TimerDuration::zero();
    std::ranges::fill(frame_start_time_history_, app_start_time_);
    pacing_schedule_.Reset();
}

void ApplicationFrameClock::RegisterFrameStart(std::optional<TimePoint> frame_start)
{
    frame_start_time_ = frame_start.has_value() ? *frame_start : Clock::now();
    if (!frame_durations_ns_.empty())
    {
        last_frame_duration_ns_ = frame_durations_ns_[std::min(replay_frame_, frame_durations_ns_.size() - 1)];
        ErrorHandling::Ensure(
            last_frame_duration_ns_ <= std::numeric_limits<u64>::max() - replay_elapsed_ns_,
            "Recorded logical time overflowed the nanosecond range");
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
    const TimePoint current_frame_start_time = frame_start_time_;
    const TimePoint oldest_frame_start_time =
        std::exchange(frame_start_time_history_[current_frame_time_index_], current_frame_start_time);

    framerate_ = static_cast<float>(
        static_cast<double>(frame_start_time_history_.size()) /
        DurationToSeconds<double>(current_frame_start_time - oldest_frame_start_time));
    last_frame_duration_ns_ =
        static_cast<u64>(ToNanoseconds(current_frame_start_time - previous_frame_start_time).count());
    last_frame_duration_seconds_ = DurationToSeconds<float>(current_frame_start_time - previous_frame_start_time);
}

bool ApplicationFrameClock::HasFinishedRecordedFrames() const noexcept
{
    return !frame_durations_ns_.empty() && replay_frame_ >= frame_durations_ns_.size();
}

void ApplicationFrameClock::ResumeLiveTime(u64 completed_frames)
{
    if (frame_durations_ns_.empty() && !fixed_step_nanoseconds_) return;
    live_elapsed_offset_ = GetElapsedTime(completed_frames);
    frame_durations_ns_ = {};
    fixed_step_nanoseconds_.reset();
    app_start_time_ = frame_start_time_;
    std::ranges::fill(frame_start_time_history_, frame_start_time_);
}

std::optional<ApplicationFrameClock::TimePoint> ApplicationFrameClock::GetFramePacingDeadline(
    bool enabled,
    TimePoint now)
{
    if (!enabled)
    {
        pacing_schedule_.Reset();
        return std::nullopt;
    }
    const auto deadline = pacing_schedule_.GetDeadline(
        FramePacingFrame{
            .frame_start = ToNanoseconds(frame_start_time_.time_since_epoch()),
            .now = ToNanoseconds(now.time_since_epoch()),
        });
    if (!deadline) return std::nullopt;
    return TimePoint{std::chrono::duration_cast<Clock::duration>(*deadline)};
}

void ApplicationFrameClock::SetTargetFramerate(std::optional<float> framerate)
{
    pacing_schedule_.SetTargetFramerate(framerate);
}

void ApplicationFrameClock::AlignWithFramerate(bool enabled)
{
    if (!enabled || !pacing_schedule_.HasTargetFramerate()) return;
    if (const auto deadline = GetFramePacingDeadline(true, Clock::now())) SleepUntil(*deadline);
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
    const u64 live_elapsed = elapsed > 0 ? static_cast<u64>(elapsed) : 0;
    ErrorHandling::Ensure(
        live_elapsed <= std::numeric_limits<u64>::max() - live_elapsed_offset_.count(),
        "Live logical time overflowed the nanosecond range");
    return live_elapsed_offset_ + TimerDuration{live_elapsed};
}

float ApplicationFrameClock::GetRelativeTimeSeconds(u64 completed_frames) const
{
    if (!frame_durations_ns_.empty())
    {
        return static_cast<float>(static_cast<double>(replay_elapsed_ns_) / kNanosecondsPerSecond);
    }
    if (const auto step = GetFixedStepSeconds())
    {
        return static_cast<float>(static_cast<double>(completed_frames) * *step);
    }
    return TimerDurationToSeconds(GetElapsedTime(completed_frames));
}

float ApplicationFrameClock::GetCurrentFrameStartTime(u64 completed_frames) const
{
    if (fixed_step_nanoseconds_.has_value() || !frame_durations_ns_.empty())
    {
        return GetRelativeTimeSeconds(completed_frames);
    }
    return TimerDurationToSeconds(live_elapsed_offset_) +
           DurationToSeconds(frame_start_time_history_[current_frame_time_index_] - app_start_time_);
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
