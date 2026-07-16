#include "klvk/timing/timer_manager.hpp"

#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using klvk::TimerDuration;
using klvk::TimerEvent;
using klvk::TimerHandle;
using klvk::TimerManager;
using klvk::TimerMissedTickPolicy;

TimerDuration Seconds(double value)
{
    return TimerDuration{value};
}

void Ensure(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void EnsureNear(TimerDuration actual, TimerDuration expected, std::string_view message)
{
    if (std::abs(actual.count() - expected.count()) > 1e-12) throw std::runtime_error(std::string(message));
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

void TestOneShotTimersAndOrdering()
{
    TimerManager timers;
    std::vector<int> calls;
    const TimerHandle late = timers.ScheduleAt(Seconds(2), [&](const TimerEvent&) { calls.push_back(2); });
    const TimerHandle early = timers.ScheduleAt(Seconds(1), [&](const TimerEvent&) { calls.push_back(1); });
    const TimerHandle frame = timers.ScheduleAtFrame(1, [&](const TimerEvent&) { calls.push_back(3); });
    Ensure(late.IsValid() && early.IsValid() && frame.IsValid(), "scheduled timer returned an invalid handle");
    Ensure(timers.GetTimerCount() == 3, "timer count is incorrect after scheduling");
    Ensure(timers.Advance(Seconds(0.5), 0) == 0, "timer fired before its deadline");
    Ensure(timers.Advance(Seconds(2), 1) == 3, "due one-shot timers did not all fire");
    Ensure(calls == std::vector<int>({1, 2, 3}), "timer ordering is not deterministic");
    Ensure(timers.IsEmpty(), "one-shot timers remained scheduled after firing");

    calls.clear();
    [[maybe_unused]] const TimerHandle equal_first =
        timers.ScheduleAt(Seconds(3), [&](const TimerEvent&) { calls.push_back(1); });
    [[maybe_unused]] const TimerHandle equal_second =
        timers.ScheduleAt(Seconds(3), [&](const TimerEvent&) { calls.push_back(2); });
    [[maybe_unused]] const TimerHandle equal_third =
        timers.ScheduleAt(Seconds(3), [&](const TimerEvent&) { calls.push_back(3); });
    Ensure(timers.Advance(Seconds(3), 1) == 3, "equal-deadline timers did not fire");
    Ensure(calls == std::vector<int>({1, 2, 3}), "equal-deadline timers were not FIFO");
}

void TestCancellationAndSlotReuse()
{
    TimerManager timers;
    size_t calls = 0;
    const TimerHandle stale = timers.ScheduleAt(Seconds(1), [&](const TimerEvent&) { ++calls; });
    Ensure(timers.Cancel(stale), "scheduled timer could not be cancelled");
    Ensure(!timers.Cancel(stale), "timer could be cancelled twice");
    const TimerHandle replacement = timers.ScheduleAt(Seconds(1), [&](const TimerEvent&) { ++calls; });
    Ensure(replacement != stale, "reused timer slot did not change generation");
    Ensure(!timers.Cancel(stale), "stale handle cancelled a reused slot");
    Ensure(timers.Advance(Seconds(1), 0) == 1 && calls == 1, "replacement timer did not fire exactly once");

    TimerHandle second;
    [[maybe_unused]] const TimerHandle canceller = timers.ScheduleAt(
        Seconds(2),
        [&](const TimerEvent&)
        {
            ++calls;
            Ensure(timers.Cancel(second), "due sibling timer could not be cancelled");
        });
    second = timers.ScheduleAt(Seconds(2), [&](const TimerEvent&) { ++calls; });
    Ensure(timers.Advance(Seconds(2), 0) == 1 && calls == 2, "cancelled due sibling callback was invoked");

    TimerManager other;
    const TimerHandle first_manager = timers.ScheduleAt(Seconds(3), [](const TimerEvent&) {});
    const TimerHandle second_manager = other.ScheduleAt(Seconds(3), [](const TimerEvent&) {});
    Ensure(first_manager != second_manager, "handles from different managers compare equal");
    Ensure(!other.Cancel(first_manager), "a timer handle cancelled a timer in another manager");
    Ensure(other.Cancel(second_manager), "a manager could not cancel its own timer");
    Ensure(timers.Cancel(first_manager), "first manager timer could not be cancelled");
}

void TestCallbackMutationAndExceptionSafety()
{
    TimerManager timers;
    std::vector<int> calls;
    [[maybe_unused]] const TimerHandle initial = timers.ScheduleAt(
        Seconds(1),
        [&](const TimerEvent&)
        {
            calls.push_back(1);
            [[maybe_unused]] const TimerHandle deferred =
                timers.ScheduleAt(Seconds(1), [&](const TimerEvent&) { calls.push_back(2); });
        });
    Ensure(timers.Advance(Seconds(1), 0) == 1, "initial callback did not fire");
    Ensure(calls == std::vector<int>({1}), "callback-scheduled timer was not deferred");
    Ensure(timers.Advance(Seconds(1), 0) == 1, "deferred due timer did not fire on the next advance");
    Ensure(calls == std::vector<int>({1, 2}), "deferred timer callback order is incorrect");

    [[maybe_unused]] const TimerHandle clear_trigger =
        timers.ScheduleAt(Seconds(2), [&](const TimerEvent&) { timers.Clear(); });
    [[maybe_unused]] const TimerHandle cleared_sibling =
        timers.ScheduleAt(Seconds(2), [&](const TimerEvent&) { calls.push_back(3); });
    Ensure(timers.Advance(Seconds(2), 0) == 1, "Clear from callback produced an invalid dispatch count");
    Ensure(calls == std::vector<int>({1, 2}), "Clear from callback did not cancel pending timers");
    Ensure(timers.IsEmpty(), "Clear from callback left timers active");

    [[maybe_unused]] const TimerHandle throwing =
        timers.ScheduleAt(Seconds(3), [](const TimerEvent&) { throw std::runtime_error("expected"); });
    [[maybe_unused]] const TimerHandle exception_sibling =
        timers.ScheduleAt(Seconds(3), [&](const TimerEvent&) { calls.push_back(4); });
    EnsureThrows(
        [&] { [[maybe_unused]] const u64 callback_count = timers.Advance(Seconds(3), 0); },
        "timer callback exception was swallowed");
    Ensure(timers.GetTimerCount() == 1, "callback exception discarded an undispatched sibling");
    Ensure(timers.Advance(Seconds(3), 0) == 1, "sibling was not dispatchable after callback exception");
    Ensure(calls.back() == 4, "wrong callback survived an exception");

    [[maybe_unused]] const TimerHandle recursive = timers.ScheduleAt(
        Seconds(4),
        [&](const TimerEvent&) { [[maybe_unused]] const u64 callback_count = timers.Advance(Seconds(4), 0); });
    EnsureThrows(
        [&] { [[maybe_unused]] const u64 callback_count = timers.Advance(Seconds(4), 0); },
        "recursive Advance was accepted");
    Ensure(timers.IsEmpty(), "recursive Advance failure left the active timer behind");

    size_t deferred_calls = 0;
    TimerHandle cancelled_sibling;
    [[maybe_unused]] const TimerHandle reallocating = timers.ScheduleAt(
        Seconds(5),
        [&](const TimerEvent&)
        {
            Ensure(timers.Cancel(cancelled_sibling), "pending sibling could not be cancelled before slot reuse");
            for (size_t index = 0; index != 512; ++index)
            {
                [[maybe_unused]] const TimerHandle deferred =
                    timers.ScheduleAt(Seconds(5), [&](const TimerEvent&) { ++deferred_calls; });
            }
        });
    cancelled_sibling = timers.ScheduleAt(Seconds(5), [&](const TimerEvent&) { ++deferred_calls; });
    Ensure(timers.Advance(Seconds(5), 0) == 1, "slot-reallocation callback dispatched new work immediately");
    Ensure(deferred_calls == 0, "callback-scheduled work was not deferred after slot reallocation");
    Ensure(timers.Advance(Seconds(5), 0) == 512, "deferred work was lost during slot reallocation");
    Ensure(deferred_calls == 512 && timers.IsEmpty(), "slot reallocation corrupted deferred callbacks");
}

void TestRepeatingTimers()
{
    TimerManager timers;
    std::vector<TimerEvent> coalesced;
    const TimerHandle repeating = timers.ScheduleEveryAt(
        Seconds(0.1),
        Seconds(0.1),
        [&](const TimerEvent& event) { coalesced.push_back(event); },
        TimerMissedTickPolicy::Coalesce);
    Ensure(timers.Advance(Seconds(0.35), 0) == 1, "coalesced timer invoked more than once");
    Ensure(
        coalesced.size() == 1 && coalesced[0].occurrence == 2 && coalesced[0].missed_occurrences == 2,
        "coalesced timer reported incorrect occurrences");
    EnsureNear(coalesced[0].scheduled_time, Seconds(0.3), "coalesced timer reported incorrect logical deadline");
    EnsureNear(*timers.GetNextTimeDeadline(), Seconds(0.4), "coalesced timer drifted from its fixed rate");
    Ensure(
        timers.Advance(Seconds(0.4), 0) == 1 && coalesced.back().occurrence == 3,
        "coalesced timer did not continue at the fixed rate");
    Ensure(timers.Cancel(repeating), "repeating timer could not be cancelled");

    std::vector<TimerEvent> caught_up;
    TimerHandle frame_timer;
    frame_timer = timers.ScheduleEveryAtFrame(
        2,
        2,
        [&](const TimerEvent& event)
        {
            caught_up.push_back(event);
            if (event.occurrence == 2) Ensure(timers.Cancel(frame_timer), "repeating timer could not cancel itself");
        },
        TimerMissedTickPolicy::InvokeAll);
    Ensure(timers.Advance(Seconds(0.4), 7) == 3, "catch-up timer did not invoke every elapsed occurrence");
    Ensure(caught_up.size() == 3, "catch-up callback count is incorrect");
    Ensure(
        caught_up[0].scheduled_frame == 2 && caught_up[1].scheduled_frame == 4 && caught_up[2].scheduled_frame == 6,
        "catch-up timer reported incorrect frame deadlines");
    Ensure(timers.IsEmpty(), "self-cancelled repeating timer remained active");

    std::vector<u64> budgeted_occurrences;
    const TimerHandle budgeted = timers.ScheduleEveryAtFrame(
        8,
        1,
        [&](const TimerEvent& event) { budgeted_occurrences.push_back(event.occurrence); },
        TimerMissedTickPolicy::InvokeAll);
    Ensure(timers.Advance(Seconds(0.4), 20, 3) == 3, "catch-up timer ignored the callback budget");
    Ensure(
        budgeted_occurrences == std::vector<u64>({0, 1, 2}) && timers.GetNextFrameDeadline() == 11,
        "budgeted catch-up timer lost its next occurrence");
    Ensure(timers.Advance(Seconds(0.4), 20, 4) == 4, "catch-up timer did not resume after budget exhaustion");
    Ensure(
        budgeted_occurrences == std::vector<u64>({0, 1, 2, 3, 4, 5, 6}) && timers.GetNextFrameDeadline() == 15,
        "resumed catch-up timer skipped occurrences");
    Ensure(timers.Cancel(budgeted), "budgeted repeating timer could not be cancelled");

    TimerManager chronological_timers;
    std::vector<u64> chronological_calls;
    const TimerHandle chronological_repeating = chronological_timers.ScheduleEveryAtFrame(
        1,
        1,
        [&](const TimerEvent& event) { chronological_calls.push_back(event.scheduled_frame); },
        TimerMissedTickPolicy::InvokeAll);
    [[maybe_unused]] const TimerHandle chronological_sibling =
        chronological_timers.ScheduleAtFrame(5, [&](const TimerEvent&) { chronological_calls.push_back(50); });
    Ensure(chronological_timers.Advance(Seconds(0), 7) == 8, "catch-up occurrences and a due sibling did not all fire");
    Ensure(
        chronological_calls == std::vector<u64>({1, 2, 3, 4, 5, 50, 6, 7}),
        "catch-up occurrences were not interleaved by deadline and FIFO order");
    Ensure(
        chronological_timers.Cancel(chronological_repeating),
        "chronological repeating timer could not be cancelled");

    TimerManager fair_catch_up_timers;
    std::vector<int> fair_catch_up_calls;
    const TimerHandle fair_catch_up = fair_catch_up_timers.ScheduleEveryAt(
        Seconds(0.1),
        Seconds(0.1),
        [&](const TimerEvent&) { fair_catch_up_calls.push_back(1); },
        TimerMissedTickPolicy::InvokeAll);
    [[maybe_unused]] const TimerHandle fair_catch_up_sibling =
        fair_catch_up_timers.ScheduleAtFrame(1, [&](const TimerEvent&) { fair_catch_up_calls.push_back(2); });
    Ensure(
        fair_catch_up_timers.Advance(Seconds(1), 1, 1) == 1 && fair_catch_up_calls == std::vector<int>({1}),
        "cross-domain catch-up fairness setup invoked the wrong timer");
    Ensure(
        fair_catch_up_timers.Advance(Seconds(1), 1, 1) == 1 && fair_catch_up_calls == std::vector<int>({1, 2}),
        "catch-up timer starved due work in the other domain");
    Ensure(fair_catch_up_timers.Cancel(fair_catch_up), "fair catch-up timer could not be cancelled");

    TimerManager fair_coalesced_timers;
    std::vector<int> fair_coalesced_calls;
    const TimerHandle fair_coalesced = fair_coalesced_timers.ScheduleEveryAt(
        Seconds(0.1),
        Seconds(0.1),
        [&](const TimerEvent&) { fair_coalesced_calls.push_back(1); });
    [[maybe_unused]] const TimerHandle fair_coalesced_sibling =
        fair_coalesced_timers.ScheduleAtFrame(1, [&](const TimerEvent&) { fair_coalesced_calls.push_back(2); });
    Ensure(
        fair_coalesced_timers.Advance(Seconds(0.1), 1, 1) == 1 && fair_coalesced_calls == std::vector<int>({1}),
        "cross-domain coalesced fairness setup invoked the wrong timer");
    Ensure(
        fair_coalesced_timers.Advance(Seconds(0.2), 2, 1) == 1 && fair_coalesced_calls == std::vector<int>({1, 2}),
        "coalesced timer starved due work in the other domain");
    Ensure(fair_coalesced_timers.Cancel(fair_coalesced), "fair coalesced timer could not be cancelled");

    TimerManager stable_timers;
    std::vector<u64> stable_occurrences;
    const TimerHandle stable = stable_timers.ScheduleEveryAt(
        Seconds(0.06),
        Seconds(0.06),
        [&](const TimerEvent& event) { stable_occurrences.push_back(event.occurrence); });
    for (u64 step = 1; step <= 100; ++step)
    {
        Ensure(
            stable_timers.Advance(Seconds(static_cast<double>(step) * 0.06), step) == 1,
            "fixed-rate timer drifted across a decimal deadline");
        Ensure(stable_occurrences.back() == step - 1, "fixed-rate timer reported a drifting occurrence");
    }
    Ensure(stable_timers.Cancel(stable), "stable repeating timer could not be cancelled");
}

void TestHeapModel()
{
    struct ExpectedCall
    {
        u64 frame = 0;
        size_t sequence = 0;
        bool active = true;
    };

    TimerManager timers;
    std::mt19937 random{0x51cedu};
    for (size_t round = 0; round != 8; ++round)
    {
        std::vector<ExpectedCall> expected(257);
        std::vector<TimerHandle> handles;
        std::vector<size_t> calls;
        handles.reserve(expected.size());
        for (size_t index = 0; index != expected.size(); ++index)
        {
            expected[index].frame = 1 + random() % 97;
            expected[index].sequence = index;
            handles.push_back(timers.ScheduleAtFrame(
                expected[index].frame,
                [&, index](const TimerEvent&) { calls.push_back(index); }));
        }
        for (size_t index = 0; index != expected.size(); ++index)
        {
            if (random() % 3 != 0) continue;
            Ensure(timers.Cancel(handles[index]), "randomized heap timer could not be cancelled");
            expected[index].active = false;
        }

        std::vector<size_t> expected_calls;
        for (size_t index = 0; index != expected.size(); ++index)
            if (expected[index].active) expected_calls.push_back(index);
        std::ranges::sort(
            expected_calls,
            [&](size_t left, size_t right)
            {
                if (expected[left].frame != expected[right].frame) return expected[left].frame < expected[right].frame;
                return expected[left].sequence < expected[right].sequence;
            });
        if (!expected_calls.empty())
            Ensure(
                timers.GetNextFrameDeadline() == expected[expected_calls.front()].frame,
                "randomized heap exposed an incorrect root deadline");
        Ensure(
            timers.Advance(Seconds(static_cast<double>(round)), 100 + round) == expected_calls.size(),
            "randomized heap dispatched the wrong number of timers");
        Ensure(calls == expected_calls, "randomized heap violated deadline/FIFO ordering");
        Ensure(timers.IsEmpty(), "randomized heap retained fired timers");
    }
}

void TestRelativeSchedulingAndValidation()
{
    TimerManager timers;
    Ensure(timers.Advance(Seconds(5), 10) == 0, "empty TimerManager dispatched callbacks");
    size_t calls = 0;
    [[maybe_unused]] const TimerHandle relative_time =
        timers.ScheduleAfter(Seconds(0), [&](const TimerEvent&) { ++calls; });
    [[maybe_unused]] const TimerHandle relative_frame =
        timers.ScheduleAfterFrames(0, [&](const TimerEvent&) { ++calls; });
    Ensure(timers.Advance(Seconds(5), 10) == 2 && calls == 2, "relative due-now timers did not fire");

    EnsureThrows(
        [&] { [[maybe_unused]] const TimerHandle invalid = timers.ScheduleAt(Seconds(-1), [](const TimerEvent&) {}); },
        "negative timer deadline was accepted");
    EnsureThrows(
        [&]
        {
            [[maybe_unused]] const TimerHandle invalid =
                timers.ScheduleAt(Seconds(std::numeric_limits<double>::quiet_NaN()), [](const TimerEvent&) {});
        },
        "NaN timer deadline was accepted");
    EnsureThrows(
        [&]
        { [[maybe_unused]] const TimerHandle invalid = timers.ScheduleEvery(Seconds(0), [](const TimerEvent&) {}); },
        "zero time interval was accepted");
    EnsureThrows(
        [&] { [[maybe_unused]] const TimerHandle invalid = timers.ScheduleEveryFrames(0, [](const TimerEvent&) {}); },
        "zero frame interval was accepted");
    EnsureThrows(
        [&]
        {
            [[maybe_unused]] const TimerHandle invalid =
                timers.ScheduleEvery(Seconds(1), [](const TimerEvent&) {}, static_cast<TimerMissedTickPolicy>(255));
        },
        "invalid missed-tick policy was accepted");
    EnsureThrows(
        [&] { [[maybe_unused]] const TimerHandle invalid = timers.ScheduleAt(Seconds(6), {}); },
        "empty callback was accepted");
    EnsureThrows(
        [&] { [[maybe_unused]] const u64 callback_count = timers.Advance(Seconds(4), 10); },
        "backward time was accepted");
    EnsureThrows(
        [&] { [[maybe_unused]] const u64 callback_count = timers.Advance(Seconds(5), 9); },
        "backward frame was accepted");

    TimerManager overflow;
    Ensure(overflow.Advance(Seconds(0), std::numeric_limits<u64>::max() - 1) == 0, "empty overflow setup fired");
    EnsureThrows(
        [&] { [[maybe_unused]] const TimerHandle invalid = overflow.ScheduleAfterFrames(2, [](const TimerEvent&) {}); },
        "relative frame overflow was accepted");

    TimerManager precision;
    const double large_time = std::ldexp(1.0, 53);
    Ensure(precision.Advance(Seconds(large_time), 0) == 0, "large-time precision setup fired");
    EnsureThrows(
        [&]
        {
            [[maybe_unused]] const TimerHandle invalid =
                precision.ScheduleAfter(Seconds(0.5), [](const TimerEvent&) {});
        },
        "relative time that cannot advance the clock was accepted");

    TimerManager budget;
    size_t budget_calls = 0;
    [[maybe_unused]] const TimerHandle budgeted =
        budget.ScheduleAt(Seconds(0), [&](const TimerEvent&) { ++budget_calls; });
    Ensure(budget.Advance(Seconds(0), 0, 0) == 0, "zero callback budget dispatched a timer");
    Ensure(budget.GetTimerCount() == 1, "zero callback budget discarded a due timer");
    Ensure(budget.Advance(Seconds(0), 0, 1) == 1 && budget_calls == 1, "budgeted timer did not resume");
}

void Run()
{
    TestOneShotTimersAndOrdering();
    TestCancellationAndSlotReuse();
    TestCallbackMutationAndExceptionSafety();
    TestRepeatingTimers();
    TestHeapModel();
    TestRelativeSchedulingAndValidation();
}

}  // namespace

int main()
{
    try
    {
        Run();
        fmt::println("timer manager tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
}
