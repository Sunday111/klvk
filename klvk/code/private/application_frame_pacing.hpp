#pragma once

#include <chrono>
#include <optional>

#include "klvk/integral_aliases.hpp"

namespace klvk
{

struct FramePacingFrame
{
    std::optional<u64> fixed_step_nanoseconds;
    bool pace_fixed_step_to_real_time = false;
    u64 completed_frames = 0;
    std::chrono::nanoseconds application_start;
    std::chrono::nanoseconds frame_start;
    std::chrono::nanoseconds now;
};

class FramePacingSchedule
{
public:
    void Reset() noexcept;
    void SetTargetFramerate(std::optional<float> framerate);
    [[nodiscard]] bool HasTargetFramerate() const noexcept;
    [[nodiscard]] std::optional<std::chrono::nanoseconds> GetDeadline(const FramePacingFrame& frame);

private:
    struct TargetPeriod
    {
        u64 whole_nanoseconds;
        u64 fractional_numerator;
        u64 fractional_denominator;
    };

    [[nodiscard]] static std::optional<TargetPeriod> CalculateTargetPeriod(float framerate) noexcept;
    [[nodiscard]] std::optional<std::chrono::nanoseconds> GetFixedStepDeadline(
        const FramePacingFrame& frame) const noexcept;
    [[nodiscard]] std::optional<i64> GetTargetOffset(u64 frame_index) const noexcept;
    [[nodiscard]] std::optional<std::chrono::nanoseconds> GetTargetDeadline(u64 frame_index) const noexcept;
    void AdvancePast(std::chrono::nanoseconds now) noexcept;

    std::optional<float> target_framerate_;
    std::optional<TargetPeriod> target_period_;
    std::optional<std::chrono::nanoseconds> target_schedule_start_;
    u64 next_target_frame_ = 1;
};

}  // namespace klvk
