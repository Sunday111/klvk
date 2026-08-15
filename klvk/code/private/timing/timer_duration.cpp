#include <cmath>

#include "klvk/error_handling.hpp"
#include "klvk/timing/timer_manager.hpp"

namespace klvk
{

TimerDuration TimerDurationFromSeconds(double seconds)
{
    constexpr double kNanosecondsPerSecond = 1'000'000'000.0;
    constexpr double kExclusiveNanosecondLimit = 18'446'744'073'709'551'616.0;

    ErrorHandling::Ensure(
        std::isfinite(seconds) && seconds >= 0.0,
        "Timer duration must be a finite non-negative number of seconds");
    const double nanoseconds = std::round(seconds * kNanosecondsPerSecond);
    ErrorHandling::Ensure(
        nanoseconds < kExclusiveNanosecondLimit,
        "Timer duration exceeds the representable nanosecond range");
    const auto result = static_cast<u64>(nanoseconds);
    ErrorHandling::Ensure(
        seconds == 0.0 || result != 0,
        "Timer duration must be zero or round to at least one nanosecond");
    return TimerDuration{result};
}

float TimerDurationToSeconds(TimerDuration value) noexcept
{
    constexpr float kNanosecondsPerSecond = 1'000'000'000.f;
    return static_cast<float>(value.count()) / kNanosecondsPerSecond;
}

}  // namespace klvk
