#include "../../../../klvk/code/private/application_frame_pacing.hpp"

#include <fmt/core.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

using namespace std::chrono_literals;
using klvk::FramePacingFrame;
using klvk::FramePacingSchedule;

void Ensure(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
void EnsureThrows(Function&& function, std::string_view message)
{
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        return;
    }
    throw std::runtime_error(std::string(message));
}

FramePacingFrame OrdinaryFrame(std::chrono::nanoseconds frame_start, std::chrono::nanoseconds now)
{
    return FramePacingFrame{
        .application_start = 1s,
        .frame_start = frame_start,
        .now = now,
    };
}

void EnsureDeadline(
    std::optional<std::chrono::nanoseconds> actual,
    std::chrono::nanoseconds expected,
    std::string_view message)
{
    Ensure(actual.has_value() && *actual == expected, message);
}

void TestTargetValidation()
{
    FramePacingSchedule schedule;
    schedule.SetTargetFramerate(std::nullopt);
    schedule.SetTargetFramerate(1.f);
    schedule.SetTargetFramerate(1'000'000'000.f);

    EnsureThrows([&] { schedule.SetTargetFramerate(0.f); }, "zero target framerate was accepted");
    EnsureThrows([&] { schedule.SetTargetFramerate(-1.f); }, "negative target framerate was accepted");
    EnsureThrows(
        [&] { schedule.SetTargetFramerate(std::numeric_limits<float>::infinity()); },
        "infinite target framerate was accepted");
    EnsureThrows(
        [&] { schedule.SetTargetFramerate(std::numeric_limits<float>::quiet_NaN()); },
        "NaN target framerate was accepted");
    EnsureThrows([&] { schedule.SetTargetFramerate(2'000'000'000.f); }, "sub-nanosecond frame period was accepted");
}

void TestDisabledPacing()
{
    FramePacingSchedule schedule;
    Ensure(!schedule.HasTargetFramerate(), "a new schedule reported an active target framerate");
    Ensure(
        !schedule.GetDeadline(OrdinaryFrame(10s, 10s + 5ms)).has_value(),
        "an unset target framerate produced a deadline");

    schedule.SetTargetFramerate(60.f);
    Ensure(schedule.HasTargetFramerate(), "an assigned target framerate was not reported active");
    [[maybe_unused]] const auto active = schedule.GetDeadline(OrdinaryFrame(10s, 10s + 5ms));
    schedule.SetTargetFramerate(std::nullopt);
    Ensure(!schedule.HasTargetFramerate(), "a disabled target framerate remained active");
    Ensure(
        !schedule.GetDeadline(OrdinaryFrame(20s, 20s + 5ms)).has_value(),
        "disabling an active target framerate retained its deadline");
}

void TestAbsoluteTargetDeadlines()
{
    FramePacingSchedule schedule;
    schedule.SetTargetFramerate(60.f);

    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(10s, 10s + 1ms)),
        10s + 16'666'666ns,
        "first target deadline was not anchored to the frame start");
    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(10s + 17ms, 10s + 20ms)),
        10s + 33'333'333ns,
        "second target deadline drifted from the schedule origin");
    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(10s + 34ms, 10s + 40ms)),
        10s + 50ms,
        "fractional nanoseconds accumulated into target deadline drift");
}

void TestLongRunningScheduleDoesNotDrift()
{
    FramePacingSchedule schedule;
    schedule.SetTargetFramerate(60.f);

    constexpr u64 kFrameCount = 100'000;
    constexpr auto kExpectedElapsed = 1'666'666'666'666ns;
    constexpr auto kStart = 10s;
    std::chrono::nanoseconds now = kStart;
    std::optional<std::chrono::nanoseconds> deadline;
    for (u64 frame = 0; frame < kFrameCount; ++frame)
    {
        deadline = schedule.GetDeadline(OrdinaryFrame(now, now));
        Ensure(deadline.has_value(), "a long-running schedule unexpectedly skipped a deadline");
        now = *deadline;
    }
    EnsureDeadline(deadline, kStart + kExpectedElapsed, "a long-running target schedule accumulated drift");
}

void TestLateFrameRecovery()
{
    FramePacingSchedule schedule;
    schedule.SetTargetFramerate(60.f);

    Ensure(
        !schedule.GetDeadline(OrdinaryFrame(10s, 10s + 40ms)).has_value(),
        "a late frame requested sleep for an expired deadline");
    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(10s + 40ms, 10s + 45ms)),
        10s + 50ms,
        "a missed deadline was not skipped to the next absolute deadline");
    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(10s + 51ms, 10s + 55ms)),
        10s + 66'666'666ns,
        "the schedule drifted after recovering from a missed deadline");

    FramePacingSchedule boundary;
    boundary.SetTargetFramerate(100.f);
    Ensure(
        !boundary.GetDeadline(OrdinaryFrame(20s, 20s + 10ms)).has_value(),
        "a deadline equal to now requested a redundant sleep");
    EnsureDeadline(
        boundary.GetDeadline(OrdinaryFrame(20s + 10ms, 20s + 11ms)),
        20s + 20ms,
        "the schedule did not advance past an exactly reached deadline");
}

void TestTargetChangesResetTheSchedule()
{
    FramePacingSchedule schedule;
    schedule.SetTargetFramerate(100.f);
    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(10s, 10s + 1ms)),
        10s + 10ms,
        "initial 100 Hz deadline was incorrect");

    schedule.SetTargetFramerate(100.f);
    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(10s + 10ms, 10s + 11ms)),
        10s + 20ms,
        "setting the same target framerate reset the active schedule");

    schedule.SetTargetFramerate(50.f);
    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(30s, 30s + 1ms)),
        30s + 20ms,
        "changing target framerate did not start a fresh schedule");

    schedule.SetTargetFramerate(std::nullopt);
    schedule.SetTargetFramerate(25.f);
    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(40s, 40s + 1ms)),
        40s + 40ms,
        "re-enabling target pacing reused an old schedule origin");
}

void TestFixedStepReplayPacing()
{
    FramePacingSchedule schedule;
    schedule.SetTargetFramerate(60.f);
    FramePacingFrame replay{
        .fixed_step_nanoseconds = 16'666'667,
        .pace_fixed_step_to_real_time = false,
        .completed_frames = 3,
        .application_start = 10s,
        .frame_start = 30s,
        .now = 40s,
    };
    Ensure(!schedule.GetDeadline(replay).has_value(), "an unpaced fixed-step replay produced a wall deadline");

    replay.pace_fixed_step_to_real_time = true;
    EnsureDeadline(
        schedule.GetDeadline(replay),
        10s + 50'000'001ns,
        "visible fixed-step replay did not use logical time from application start");

    replay.completed_frames = 0;
    EnsureDeadline(schedule.GetDeadline(replay), 10s, "fixed-step frame zero did not target application start");

    replay.completed_frames = std::numeric_limits<u64>::max();
    Ensure(!schedule.GetDeadline(replay).has_value(), "an overflowing fixed-step deadline was produced");

    EnsureDeadline(
        schedule.GetDeadline(OrdinaryFrame(50s, 50s + 1ms)),
        50s + 16'666'666ns,
        "leaving fixed-step mode reused a stale target schedule");
}

void Run()
{
    TestTargetValidation();
    TestDisabledPacing();
    TestAbsoluteTargetDeadlines();
    TestLongRunningScheduleDoesNotDrift();
    TestLateFrameRecovery();
    TestTargetChangesResetTheSchedule();
    TestFixedStepReplayPacing();
}

}  // namespace

int main()
{
    try
    {
        Run();
        fmt::println("application frame pacing tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
}
