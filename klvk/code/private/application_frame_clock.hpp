#pragma once

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <optional>

#include "application_frame_pacing.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/timing/timer_manager.hpp"

namespace klvk
{

class ApplicationFrameClock
{
public:
    void Initialize(std::optional<u64> fixed_step_nanoseconds);
    void RegisterFrameStart();
    void SetTargetFramerate(std::optional<float> framerate);
    void AlignWithFramerate(bool pace_fixed_step_to_real_time, u64 completed_frames);

    [[nodiscard]] TimerDuration GetElapsedTime(u64 completed_frames) const;
    [[nodiscard]] float GetRelativeTimeSeconds(u64 completed_frames) const;
    [[nodiscard]] float GetCurrentFrameStartTime(u64 completed_frames) const;
    [[nodiscard]] float GetFramerate() const noexcept;
    [[nodiscard]] float GetLastFrameDurationSeconds() const noexcept;

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr size_t kFrameTimeHistorySize = 128;

    template <std::floating_point Result = float, typename Duration>
    [[nodiscard]] static Result DurationToSeconds(Duration duration)
    {
        return std::chrono::duration_cast<std::chrono::duration<Result, std::chrono::seconds::period>>(duration)
            .count();
    }

    [[nodiscard]] std::optional<double> GetFixedStepSeconds() const noexcept;

    TimePoint app_start_time_{};
    std::array<TimePoint, kFrameTimeHistorySize> frame_start_time_history_{};
    std::optional<u64> fixed_step_nanoseconds_;
    FramePacingSchedule pacing_schedule_;
    float last_frame_duration_seconds_ = 0.f;
    float framerate_ = 0.f;
    u8 current_frame_time_index_ = kFrameTimeHistorySize - 1;
};

}  // namespace klvk
