#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

#include "klvk/integral_aliases.hpp"

namespace klvk
{

// Public time values are expressed as double-precision seconds. TimerManager
// rounds them once at its boundary and stores all scheduling state as u64
// nanoseconds.
using TimerDuration = std::chrono::duration<double>;

enum class TimerDomain : u8
{
    Time,
    Frame,
};

// Fixed-rate repeating timers can either invoke every elapsed occurrence or
// collapse several elapsed occurrences into the most recent one.
enum class TimerMissedTickPolicy : u8
{
    InvokeAll,
    Coalesce,
};

class TimerHandle
{
public:
    [[nodiscard]] bool IsValid() const noexcept;
    friend bool operator==(TimerHandle, TimerHandle) = default;

private:
    friend class TimerManager;

    static constexpr u32 kInvalidSlot = ~u32{};

    u64 manager_id_ = 0;
    u64 generation_ = 0;
    u32 slot_ = kInvalidSlot;
};

struct TimerEvent
{
    TimerHandle handle;
    TimerDomain domain = TimerDomain::Time;

    // Only the field selected by domain is meaningful. occurrence is zero-based;
    // missed_occurrences is non-zero only for a coalesced repeating timer.
    TimerDuration scheduled_time{};
    u64 scheduled_frame = 0;
    u64 occurrence = 0;
    u64 missed_occurrences = 0;
};

class TimerManager
{
public:
    using Callback = std::function<void(const TimerEvent&)>;
    static constexpr u64 kDefaultCallbackBudget = 4096;

    TimerManager();
    TimerManager(const TimerManager&) = delete;
    TimerManager(TimerManager&&) = delete;
    ~TimerManager();

    TimerManager& operator=(const TimerManager&) = delete;
    TimerManager& operator=(TimerManager&&) = delete;

    [[nodiscard]] TimerHandle ScheduleAt(TimerDuration deadline, Callback callback);
    [[nodiscard]] TimerHandle ScheduleAfter(TimerDuration delay, Callback callback);
    [[nodiscard]] TimerHandle ScheduleAtFrame(u64 frame, Callback callback);
    [[nodiscard]] TimerHandle ScheduleAfterFrames(u64 frames, Callback callback);

    [[nodiscard]] TimerHandle ScheduleEvery(
        TimerDuration interval,
        Callback callback,
        TimerMissedTickPolicy missed_tick_policy = TimerMissedTickPolicy::Coalesce);
    [[nodiscard]] TimerHandle ScheduleEveryAt(
        TimerDuration first_deadline,
        TimerDuration interval,
        Callback callback,
        TimerMissedTickPolicy missed_tick_policy = TimerMissedTickPolicy::Coalesce);
    [[nodiscard]] TimerHandle ScheduleEveryFrames(
        u64 interval,
        Callback callback,
        TimerMissedTickPolicy missed_tick_policy = TimerMissedTickPolicy::Coalesce);
    [[nodiscard]] TimerHandle ScheduleEveryAtFrame(
        u64 first_frame,
        u64 interval,
        Callback callback,
        TimerMissedTickPolicy missed_tick_policy = TimerMissedTickPolicy::Coalesce);

    // Cancel is safe from inside a timer callback, including the timer's own
    // callback. A stale or already-fired handle simply returns false.
    [[nodiscard]] bool Cancel(TimerHandle handle) noexcept;
    void Clear() noexcept;

    // Advances both monotonic domains and invokes timers that were due when
    // Advance began, up to callback_budget callback invocations. InvokeAll
    // occurrences are interleaved chronologically with their domain's other due
    // timers, while fair readiness ordering prevents either domain from starving.
    // Due work left by the budget resumes without losing occurrences. Timers
    // scheduled by a callback are deferred until the next Advance, even when
    // their deadline has already elapsed.
    u64 Advance(TimerDuration elapsed, u64 frame, u64 callback_budget = kDefaultCallbackBudget);

    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] size_t GetTimerCount() const noexcept;
    [[nodiscard]] TimerDuration GetCurrentTime() const noexcept;
    [[nodiscard]] u64 GetCurrentFrame() const noexcept;
    [[nodiscard]] std::optional<TimerDuration> GetNextTimeDeadline() const noexcept;
    [[nodiscard]] std::optional<u64> GetNextFrameDeadline() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace klvk
