#include "klvk/timing/timer_manager.hpp"

#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/template/on_scope_leave.hpp"

namespace klvk
{
namespace
{

constexpr u32 kInvalidIndex = ~u32{};

u64 NextManagerId()
{
    static std::atomic<u64> next_id = 1;
    u64 id = next_id.load(std::memory_order_relaxed);
    for (;;)
    {
        ErrorHandling::Ensure(id != std::numeric_limits<u64>::max(), "TimerManager identity space is exhausted");
        if (next_id.compare_exchange_weak(id, id + 1, std::memory_order_relaxed)) return id;
    }
}

void ValidateTime(TimerDuration value, const char* name, bool allow_zero)
{
    ErrorHandling::Ensure(
        std::isfinite(value.count()) && (allow_zero ? value.count() >= 0.0 : value.count() > 0.0),
        "{} must be a finite {} duration",
        name,
        allow_zero ? "non-negative" : "positive");
}

TimerDuration AddTime(TimerDuration left, TimerDuration right, const char* operation)
{
    const TimerDuration result = left + right;
    ErrorHandling::Ensure(std::isfinite(result.count()), "Timer duration overflow while {}", operation);
    ErrorHandling::Ensure(
        right.count() == 0.0 || result > left,
        "Timer duration is too small to advance time while {}",
        operation);
    return result;
}

TimerDuration AnchoredTime(TimerDuration anchor, TimerDuration interval, u64 occurrence)
{
    // fma keeps a fixed-rate deadline to one final rounding on platforms where
    // long double is only an alias for double (notably MSVC).
    return TimerDuration{std::fma(interval.count(), static_cast<double>(occurrence), anchor.count())};
}

u64 AddFrames(u64 left, u64 right, const char* operation)
{
    ErrorHandling::Ensure(
        right <= std::numeric_limits<u64>::max() - left,
        "Frame deadline overflow while {}",
        operation);
    return left + right;
}

void ValidateMissedTickPolicy(TimerMissedTickPolicy policy)
{
    ErrorHandling::Ensure(
        policy == TimerMissedTickPolicy::InvokeAll || policy == TimerMissedTickPolicy::Coalesce,
        "Invalid missed-tick policy");
}

}  // namespace

struct TimerManager::State
{
    explicit State(u64 manager_id_) : manager_id(manager_id_) {}

    enum class SlotState : u8
    {
        Free,
        Scheduled,
        Pending,
        Dispatching,
        CancelRequested,
    };

    struct Slot
    {
        Callback callback;
        TimerDuration time_anchor{};
        TimerDuration time_deadline{};
        TimerDuration time_interval{};
        u64 frame_deadline = 0;
        u64 frame_interval = 0;
        u64 sequence = 0;
        u64 ready_order = 0;
        u64 occurrence = 0;
        u64 generation = 1;
        u32 heap_index = kInvalidIndex;
        u32 next_free = kInvalidIndex;
        TimerDomain domain = TimerDomain::Time;
        TimerMissedTickPolicy missed_tick_policy = TimerMissedTickPolicy::Coalesce;
        SlotState state = SlotState::Free;
        bool repeating = false;
    };

    struct DueEntry
    {
        u32 slot = kInvalidIndex;
        u64 generation = 0;
        TimerDuration time_deadline{};
        u64 frame_deadline = 0;
        u64 timer_sequence = 0;
        u64 ready_order = 0;
    };

    std::vector<Slot> slots;
    std::vector<u32> time_heap;
    std::vector<u32> frame_heap;
    std::vector<DueEntry> time_due_scratch;
    std::vector<DueEntry> frame_due_scratch;
    TimerDuration current_time{};
    u64 current_frame = 0;
    u64 next_order = 1;
    u64 manager_id = 0;
    size_t active_count = 0;
    u32 free_head = kInvalidIndex;
    bool advancing = false;

    [[nodiscard]] std::vector<u32>& GetHeap(TimerDomain domain)
    {
        return domain == TimerDomain::Time ? time_heap : frame_heap;
    }

    [[nodiscard]] const std::vector<u32>& GetHeap(TimerDomain domain) const
    {
        return domain == TimerDomain::Time ? time_heap : frame_heap;
    }

    [[nodiscard]] bool ComesBefore(u32 left_index, u32 right_index) const
    {
        const Slot& left = slots[left_index];
        const Slot& right = slots[right_index];
        if (left.domain == TimerDomain::Time)
        {
            if (left.time_deadline != right.time_deadline) return left.time_deadline < right.time_deadline;
        }
        else if (left.frame_deadline != right.frame_deadline)
            return left.frame_deadline < right.frame_deadline;
        return left.sequence < right.sequence;
    }

    void SwapHeapEntries(std::vector<u32>& heap, size_t left, size_t right)
    {
        std::swap(heap[left], heap[right]);
        slots[heap[left]].heap_index = static_cast<u32>(left);
        slots[heap[right]].heap_index = static_cast<u32>(right);
    }

    void SiftUp(std::vector<u32>& heap, size_t index)
    {
        while (index != 0)
        {
            const size_t parent = (index - 1) / 2;
            if (!ComesBefore(heap[index], heap[parent])) break;
            SwapHeapEntries(heap, index, parent);
            index = parent;
        }
    }

    void SiftDown(std::vector<u32>& heap, size_t index)
    {
        for (;;)
        {
            const size_t left = index * 2 + 1;
            if (left >= heap.size()) return;
            const size_t right = left + 1;
            const size_t first = right < heap.size() && ComesBefore(heap[right], heap[left]) ? right : left;
            if (!ComesBefore(heap[first], heap[index])) return;
            SwapHeapEntries(heap, index, first);
            index = first;
        }
    }

    void PushHeap(u32 slot_index)
    {
        Slot& slot = slots[slot_index];
        auto& heap = GetHeap(slot.domain);
        ErrorHandling::Ensure(heap.size() < kInvalidIndex, "Timer heap exceeds its index capacity");
        heap.push_back(slot_index);
        slot.heap_index = static_cast<u32>(heap.size() - 1);
        SiftUp(heap, heap.size() - 1);
    }

    void RemoveFromHeap(u32 slot_index)
    {
        Slot& slot = slots[slot_index];
        auto& heap = GetHeap(slot.domain);
        const size_t index = slot.heap_index;
        ErrorHandling::Ensure(index < heap.size() && heap[index] == slot_index, "Timer heap index is corrupt");
        const u32 replacement = heap.back();
        heap.pop_back();
        slot.heap_index = kInvalidIndex;
        if (index == heap.size()) return;
        heap[index] = replacement;
        slots[replacement].heap_index = static_cast<u32>(index);
        if (index != 0 && ComesBefore(replacement, heap[(index - 1) / 2]))
            SiftUp(heap, index);
        else
            SiftDown(heap, index);
    }

    [[nodiscard]] TimerHandle MakeHandle(u32 slot_index) const
    {
        TimerHandle result;
        result.manager_id_ = manager_id;
        result.slot_ = slot_index;
        result.generation_ = slots[slot_index].generation;
        return result;
    }

    [[nodiscard]] u32 AllocateSlot()
    {
        u32 result = 0;
        if (free_head != kInvalidIndex)
        {
            result = free_head;
            free_head = slots[result].next_free;
        }
        else
        {
            ErrorHandling::Ensure(slots.size() < kInvalidIndex, "TimerManager exceeds its slot capacity");
            result = static_cast<u32>(slots.size());
            slots.emplace_back();
        }
        Slot& slot = slots[result];
        ErrorHandling::Ensure(slot.state == SlotState::Free, "TimerManager free-slot list is corrupt");
        slot.next_free = kInvalidIndex;
        slot.state = SlotState::Scheduled;
        ++active_count;
        return result;
    }

    void ReleaseSlot(u32 slot_index) noexcept
    {
        Slot& slot = slots[slot_index];
        slot.callback = {};
        slot.time_anchor = {};
        slot.time_deadline = {};
        slot.time_interval = {};
        slot.frame_deadline = 0;
        slot.frame_interval = 0;
        slot.sequence = 0;
        slot.ready_order = 0;
        slot.occurrence = 0;
        slot.heap_index = kInvalidIndex;
        slot.next_free = free_head;
        slot.domain = TimerDomain::Time;
        slot.missed_tick_policy = TimerMissedTickPolicy::Coalesce;
        slot.state = SlotState::Free;
        slot.repeating = false;
        ++slot.generation;
        if (slot.generation == 0) ++slot.generation;
        free_head = slot_index;
        --active_count;
    }

    [[nodiscard]] bool IsHandleCurrent(TimerHandle handle) const noexcept
    {
        return handle.manager_id_ == manager_id && handle.slot_ < slots.size() &&
               slots[handle.slot_].generation == handle.generation_ && slots[handle.slot_].state != SlotState::Free;
    }

    [[nodiscard]] TimerHandle Schedule(
        TimerDomain domain,
        TimerDuration time_deadline,
        u64 frame_deadline,
        TimerDuration time_interval,
        u64 frame_interval,
        bool repeating,
        TimerMissedTickPolicy missed_tick_policy,
        Callback callback)
    {
        ErrorHandling::Ensure(static_cast<bool>(callback), "Cannot schedule an empty timer callback");
        ValidateMissedTickPolicy(missed_tick_policy);
        ErrorHandling::Ensure(next_order != 0, "Timer ordering space is exhausted");
        const u32 slot_index = AllocateSlot();
        Slot& slot = slots[slot_index];
        slot.callback = std::move(callback);
        slot.time_anchor = time_deadline;
        slot.time_deadline = time_deadline;
        slot.time_interval = time_interval;
        slot.frame_deadline = frame_deadline;
        slot.frame_interval = frame_interval;
        slot.sequence = next_order;
        slot.ready_order = next_order++;
        slot.occurrence = 0;
        slot.domain = domain;
        slot.missed_tick_policy = missed_tick_policy;
        slot.repeating = repeating;
        try
        {
            PushHeap(slot_index);
        }
        catch (...)
        {
            ReleaseSlot(slot_index);
            throw;
        }
        return MakeHandle(slot_index);
    }

    void ExtractDue(TimerDomain domain, std::vector<DueEntry>& result)
    {
        auto& heap = GetHeap(domain);
        while (!heap.empty())
        {
            const u32 slot_index = heap.front();
            const Slot& slot = slots[slot_index];
            const bool due =
                domain == TimerDomain::Time ? slot.time_deadline <= current_time : slot.frame_deadline <= current_frame;
            if (!due) break;
            result.push_back(
                {.slot = slot_index,
                 .generation = slots[slot_index].generation,
                 .time_deadline = slot.time_deadline,
                 .frame_deadline = slot.frame_deadline,
                 .timer_sequence = slot.sequence,
                 .ready_order = slot.ready_order});
            RemoveFromHeap(slot_index);
            slots[slot_index].state = SlotState::Pending;
        }
    }

    [[nodiscard]] bool DueComesBefore(const DueEntry& left, const DueEntry& right, TimerDomain domain) const
    {
        if (domain == TimerDomain::Time)
        {
            if (left.time_deadline != right.time_deadline) return left.time_deadline < right.time_deadline;
        }
        else if (left.frame_deadline != right.frame_deadline)
            return left.frame_deadline < right.frame_deadline;
        return left.timer_sequence < right.timer_sequence;
    }

    void SiftDownDue(std::vector<DueEntry>& heap, size_t index, TimerDomain domain) const
    {
        for (;;)
        {
            const size_t left = index * 2 + 1;
            if (left >= heap.size()) return;
            const size_t right = left + 1;
            const size_t first = right < heap.size() && DueComesBefore(heap[right], heap[left], domain) ? right : left;
            if (!DueComesBefore(heap[first], heap[index], domain)) return;
            std::swap(heap[index], heap[first]);
            index = first;
        }
    }

    void PushDue(std::vector<DueEntry>& heap, DueEntry entry, TimerDomain domain)
    {
        heap.push_back(entry);
        size_t index = heap.size() - 1;
        while (index != 0)
        {
            const size_t parent = (index - 1) / 2;
            if (!DueComesBefore(heap[index], heap[parent], domain)) break;
            std::swap(heap[index], heap[parent]);
            index = parent;
        }
    }

    [[nodiscard]] DueEntry PopDue(std::vector<DueEntry>& heap, TimerDomain domain)
    {
        ErrorHandling::Ensure(!heap.empty(), "Cannot pop an empty due-timer heap");
        DueEntry result = heap.front();
        if (heap.size() == 1)
        {
            heap.pop_back();
            return result;
        }
        heap.front() = heap.back();
        heap.pop_back();
        SiftDownDue(heap, 0, domain);
        return result;
    }

    [[nodiscard]] DueEntry MakeDueEntry(u32 slot_index) const
    {
        const Slot& slot = slots[slot_index];
        return {
            .slot = slot_index,
            .generation = slot.generation,
            .time_deadline = slot.time_deadline,
            .frame_deadline = slot.frame_deadline,
            .timer_sequence = slot.sequence,
            .ready_order = slot.ready_order};
    }

    [[nodiscard]] u64 TakeNextOrder()
    {
        ErrorHandling::Ensure(next_order != 0, "Timer ordering space is exhausted");
        return next_order++;
    }

    void RequeuePending(std::span<const DueEntry> due)
    {
        for (const DueEntry entry : due)
        {
            if (entry.slot >= slots.size() || slots[entry.slot].generation != entry.generation) continue;
            const u32 slot_index = entry.slot;
            Slot& slot = slots[slot_index];
            if (slot.state != SlotState::Pending) continue;
            slot.state = SlotState::Scheduled;
            try
            {
                PushHeap(slot_index);
            }
            catch (...)
            {
                ReleaseSlot(slot_index);
                throw;
            }
        }
    }

    void CancelDue(std::span<const DueEntry> due) noexcept
    {
        for (const DueEntry entry : due)
        {
            if (entry.slot >= slots.size() || slots[entry.slot].generation != entry.generation) continue;
            Slot& slot = slots[entry.slot];
            if (slot.state == SlotState::Scheduled)
            {
                if (slot.heap_index != kInvalidIndex) RemoveFromHeap(entry.slot);
                ReleaseSlot(entry.slot);
            }
            else if (slot.state == SlotState::Pending)
                ReleaseSlot(entry.slot);
        }
    }

    struct DispatchPlan
    {
        TimerDuration latest_time{};
        TimerDuration next_time{};
        u64 latest_frame = 0;
        u64 next_frame = 0;
        u64 additional_occurrences = 0;
        bool has_next = false;
    };

    [[nodiscard]] DispatchPlan MakeDispatchPlan(const Slot& slot) const
    {
        DispatchPlan result;
        if (!slot.repeating)
        {
            result.latest_time = slot.time_deadline;
            result.latest_frame = slot.frame_deadline;
            return result;
        }

        if (slot.domain == TimerDomain::Frame)
        {
            result.additional_occurrences = (current_frame - slot.frame_deadline) / slot.frame_interval;
            ErrorHandling::Ensure(
                result.additional_occurrences <= std::numeric_limits<u64>::max() - slot.occurrence,
                "Repeating frame timer occurrence overflow");
            result.latest_frame = slot.frame_deadline + result.additional_occurrences * slot.frame_interval;
            if (slot.frame_interval <= std::numeric_limits<u64>::max() - result.latest_frame)
            {
                result.next_frame = result.latest_frame + slot.frame_interval;
                result.has_next = result.additional_occurrences < std::numeric_limits<u64>::max() - slot.occurrence;
            }
            return result;
        }

        const long double quotient =
            (static_cast<long double>(current_time.count()) - static_cast<long double>(slot.time_anchor.count())) /
            static_cast<long double>(slot.time_interval.count());
        ErrorHandling::Ensure(quotient >= 0.0L, "Repeating time timer deadline exceeds its anchor");
        ErrorHandling::Ensure(
            quotient <= static_cast<long double>(std::numeric_limits<u64>::max()),
            "Repeating time timer occurrence overflow");
        u64 latest_occurrence = static_cast<u64>(std::floor(quotient));
        result.latest_time = AnchoredTime(slot.time_anchor, slot.time_interval, latest_occurrence);
        if (result.latest_time > current_time)
        {
            ErrorHandling::Ensure(latest_occurrence != 0, "Repeating time timer correction underflow");
            --latest_occurrence;
            result.latest_time = AnchoredTime(slot.time_anchor, slot.time_interval, latest_occurrence);
        }
        if (latest_occurrence != std::numeric_limits<u64>::max())
        {
            const TimerDuration candidate = AnchoredTime(slot.time_anchor, slot.time_interval, latest_occurrence + 1);
            if (candidate <= current_time)
            {
                ErrorHandling::Ensure(candidate > result.latest_time, "Repeating time timer exhausted time precision");
                ++latest_occurrence;
                result.latest_time = candidate;
            }
        }
        ErrorHandling::Ensure(
            latest_occurrence >= slot.occurrence && result.latest_time <= current_time,
            "Repeating time timer correction failed");
        result.additional_occurrences = latest_occurrence - slot.occurrence;
        if (latest_occurrence != std::numeric_limits<u64>::max())
        {
            const TimerDuration next = AnchoredTime(slot.time_anchor, slot.time_interval, latest_occurrence + 1);
            ErrorHandling::Ensure(
                std::isfinite(next.count()) && next > current_time && next > result.latest_time,
                "Repeating time timer cannot represent its next deadline");
            result.next_time = next;
            result.has_next = true;
        }
        return result;
    }

    [[nodiscard]] TimerEvent
    MakeEvent(TimerHandle handle, const Slot& slot, const DispatchPlan& plan, u64 offset, bool coalesced) const
    {
        TimerEvent result{
            .handle = handle,
            .domain = slot.domain,
            .occurrence = slot.occurrence + offset,
            .missed_occurrences = coalesced ? plan.additional_occurrences : 0};
        if (slot.domain == TimerDomain::Time)
            result.scheduled_time = coalesced
                                        ? plan.latest_time
                                        : AnchoredTime(slot.time_anchor, slot.time_interval, slot.occurrence + offset);
        else
            result.scheduled_frame = coalesced ? plan.latest_frame : slot.frame_deadline + slot.frame_interval * offset;
        return result;
    }

    void FinishDispatch(u32 slot_index, Callback callback, const DispatchPlan& plan)
    {
        Slot& slot = slots[slot_index];
        if (slot.state == SlotState::CancelRequested || !slot.repeating || !plan.has_next)
        {
            ReleaseSlot(slot_index);
            return;
        }
        ErrorHandling::Ensure(slot.state == SlotState::Dispatching, "Timer changed state during dispatch");
        slot.ready_order = TakeNextOrder();
        slot.callback = std::move(callback);
        slot.occurrence += plan.additional_occurrences + 1;
        if (slot.domain == TimerDomain::Time)
            slot.time_deadline = plan.next_time;
        else
            slot.frame_deadline = plan.next_frame;

        const bool remains_due = slot.missed_tick_policy == TimerMissedTickPolicy::InvokeAll &&
                                 (slot.domain == TimerDomain::Time ? slot.time_deadline <= current_time
                                                                   : slot.frame_deadline <= current_frame);
        if (remains_due)
        {
            slot.state = SlotState::Pending;
            return;
        }

        slot.state = SlotState::Scheduled;
        try
        {
            PushHeap(slot_index);
        }
        catch (...)
        {
            ReleaseSlot(slot_index);
            throw;
        }
    }

    [[nodiscard]] u64 Dispatch(u32 slot_index, u64 callback_budget)
    {
        Slot& initial_slot = slots[slot_index];
        ErrorHandling::Ensure(initial_slot.state == SlotState::Pending, "Timer dispatch state is corrupt");
        const DispatchPlan plan = MakeDispatchPlan(initial_slot);
        ErrorHandling::Ensure(callback_budget != 0, "Timer dispatch callback budget is exhausted");
        const TimerHandle handle = MakeHandle(slot_index);
        const TimerMissedTickPolicy policy = initial_slot.missed_tick_policy;
        const bool repeating = initial_slot.repeating;
        Callback callback = std::move(initial_slot.callback);
        initial_slot.state = SlotState::Dispatching;

        u64 invocation_count = 0;
        if (policy == TimerMissedTickPolicy::Coalesce || !repeating)
        {
            callback(MakeEvent(handle, initial_slot, plan, plan.additional_occurrences, repeating));
            invocation_count = 1;
        }
        else
        {
            const u64 dispatch_count =
                plan.additional_occurrences < callback_budget ? plan.additional_occurrences + 1 : callback_budget;
            for (u64 offset = 0; offset != dispatch_count; ++offset)
            {
                callback(MakeEvent(handle, slots[slot_index], plan, offset, false));
                ++invocation_count;
                if (slots[slot_index].state == SlotState::CancelRequested) break;
            }
        }
        DispatchPlan completion_plan = plan;
        Slot& final_slot = slots[slot_index];
        if (policy == TimerMissedTickPolicy::InvokeAll && repeating && invocation_count <= plan.additional_occurrences)
        {
            completion_plan.additional_occurrences = invocation_count - 1;
            if (final_slot.domain == TimerDomain::Time)
            {
                completion_plan.latest_time = AnchoredTime(
                    final_slot.time_anchor,
                    final_slot.time_interval,
                    final_slot.occurrence + completion_plan.additional_occurrences);
                completion_plan.next_time = AnchoredTime(
                    final_slot.time_anchor,
                    final_slot.time_interval,
                    final_slot.occurrence + invocation_count);
            }
            else
            {
                completion_plan.latest_frame =
                    final_slot.frame_deadline + final_slot.frame_interval * completion_plan.additional_occurrences;
                completion_plan.next_frame = final_slot.frame_deadline + final_slot.frame_interval * invocation_count;
            }
            completion_plan.has_next = true;
        }
        FinishDispatch(slot_index, std::move(callback), completion_plan);
        return invocation_count;
    }
};

bool TimerHandle::IsValid() const noexcept
{
    return manager_id_ != 0 && slot_ != kInvalidSlot && generation_ != 0;
}

TimerManager::TimerManager() : state_(std::make_unique<State>(NextManagerId())) {}

TimerManager::~TimerManager() = default;

TimerHandle TimerManager::ScheduleAt(TimerDuration deadline, Callback callback)
{
    ValidateTime(deadline, "Timer deadline", true);
    return state_
        ->Schedule(TimerDomain::Time, deadline, 0, {}, 0, false, TimerMissedTickPolicy::Coalesce, std::move(callback));
}

TimerHandle TimerManager::ScheduleAfter(TimerDuration delay, Callback callback)
{
    ValidateTime(delay, "Timer delay", true);
    return ScheduleAt(AddTime(state_->current_time, delay, "scheduling a relative timer"), std::move(callback));
}

TimerHandle TimerManager::ScheduleAtFrame(u64 frame, Callback callback)
{
    return state_
        ->Schedule(TimerDomain::Frame, {}, frame, {}, 0, false, TimerMissedTickPolicy::Coalesce, std::move(callback));
}

TimerHandle TimerManager::ScheduleAfterFrames(u64 frames, Callback callback)
{
    return ScheduleAtFrame(
        AddFrames(state_->current_frame, frames, "scheduling a relative frame timer"),
        std::move(callback));
}

TimerHandle
TimerManager::ScheduleEvery(TimerDuration interval, Callback callback, TimerMissedTickPolicy missed_tick_policy)
{
    ValidateTime(interval, "Timer interval", false);
    return ScheduleEveryAt(
        AddTime(state_->current_time, interval, "scheduling a repeating timer"),
        interval,
        std::move(callback),
        missed_tick_policy);
}

TimerHandle TimerManager::ScheduleEveryAt(
    TimerDuration first_deadline,
    TimerDuration interval,
    Callback callback,
    TimerMissedTickPolicy missed_tick_policy)
{
    ValidateTime(first_deadline, "First timer deadline", true);
    ValidateTime(interval, "Timer interval", false);
    const TimerDuration second_deadline = AnchoredTime(first_deadline, interval, 1);
    ErrorHandling::Ensure(
        std::isfinite(second_deadline.count()) && second_deadline > first_deadline,
        "Timer interval is too small to advance the first deadline");
    return state_
        ->Schedule(TimerDomain::Time, first_deadline, 0, interval, 0, true, missed_tick_policy, std::move(callback));
}

TimerHandle TimerManager::ScheduleEveryFrames(u64 interval, Callback callback, TimerMissedTickPolicy missed_tick_policy)
{
    ErrorHandling::Ensure(interval != 0, "Frame timer interval must be positive");
    return ScheduleEveryAtFrame(
        AddFrames(state_->current_frame, interval, "scheduling a repeating frame timer"),
        interval,
        std::move(callback),
        missed_tick_policy);
}

TimerHandle TimerManager::ScheduleEveryAtFrame(
    u64 first_frame,
    u64 interval,
    Callback callback,
    TimerMissedTickPolicy missed_tick_policy)
{
    ErrorHandling::Ensure(interval != 0, "Frame timer interval must be positive");
    return state_
        ->Schedule(TimerDomain::Frame, {}, first_frame, {}, interval, true, missed_tick_policy, std::move(callback));
}

bool TimerManager::Cancel(TimerHandle handle) noexcept
{
    if (!state_->IsHandleCurrent(handle)) return false;
    State::Slot& slot = state_->slots[handle.slot_];
    if (slot.state == State::SlotState::Scheduled)
    {
        state_->RemoveFromHeap(handle.slot_);
        state_->ReleaseSlot(handle.slot_);
        return true;
    }
    if (slot.state == State::SlotState::Pending)
    {
        state_->ReleaseSlot(handle.slot_);
        return true;
    }
    if (slot.state == State::SlotState::Dispatching)
    {
        slot.state = State::SlotState::CancelRequested;
        return true;
    }
    return false;
}

void TimerManager::Clear() noexcept
{
    state_->time_heap.clear();
    state_->frame_heap.clear();
    for (u32 slot_index = 0; slot_index != state_->slots.size(); ++slot_index)
    {
        State::Slot& slot = state_->slots[slot_index];
        if (slot.state == State::SlotState::Dispatching)
            slot.state = State::SlotState::CancelRequested;
        else if (slot.state != State::SlotState::Free && slot.state != State::SlotState::CancelRequested)
            state_->ReleaseSlot(slot_index);
    }
}

u64 TimerManager::Advance(TimerDuration elapsed, u64 frame, u64 callback_budget)
{
    ValidateTime(elapsed, "TimerManager elapsed time", true);
    ErrorHandling::Ensure(!state_->advancing, "TimerManager::Advance cannot be called recursively");
    ErrorHandling::Ensure(elapsed >= state_->current_time, "TimerManager time cannot move backwards");
    ErrorHandling::Ensure(frame >= state_->current_frame, "TimerManager frame cannot move backwards");

    if (callback_budget == 0)
    {
        state_->current_time = elapsed;
        state_->current_frame = frame;
        return 0;
    }

    auto& time_due = state_->time_due_scratch;
    auto& frame_due = state_->frame_due_scratch;
    time_due.clear();
    frame_due.clear();
    const bool has_due_time =
        !state_->time_heap.empty() && state_->slots[state_->time_heap.front()].time_deadline <= elapsed;
    const bool has_due_frame =
        !state_->frame_heap.empty() && state_->slots[state_->frame_heap.front()].frame_deadline <= frame;
    if (has_due_time && time_due.capacity() < state_->time_heap.size()) time_due.reserve(state_->time_heap.size());
    if (has_due_frame && frame_due.capacity() < state_->frame_heap.size()) frame_due.reserve(state_->frame_heap.size());
    state_->current_time = elapsed;
    state_->current_frame = frame;
    state_->advancing = true;
    auto reset_advancing = OnScopeLeave([this] { state_->advancing = false; });

    State::DueEntry current_entry;
    u64 invocation_count = 0;
    const auto is_pending = [this](State::DueEntry entry)
    {
        return entry.slot < state_->slots.size() && state_->slots[entry.slot].generation == entry.generation &&
               state_->slots[entry.slot].state == State::SlotState::Pending;
    };
    try
    {
        state_->ExtractDue(TimerDomain::Time, time_due);
        state_->ExtractDue(TimerDomain::Frame, frame_due);
        for (;;)
        {
            while (!time_due.empty() && !is_pending(time_due.front()))
            {
                [[maybe_unused]] const State::DueEntry stale = state_->PopDue(time_due, TimerDomain::Time);
            }
            while (!frame_due.empty() && !is_pending(frame_due.front()))
            {
                [[maybe_unused]] const State::DueEntry stale = state_->PopDue(frame_due, TimerDomain::Frame);
            }
            if (time_due.empty() && frame_due.empty()) break;
            if (invocation_count == callback_budget) break;

            TimerDomain domain = TimerDomain::Time;
            if (time_due.empty())
                domain = TimerDomain::Frame;
            else if (
                !frame_due.empty() && (frame_due.front().ready_order < time_due.front().ready_order ||
                                       (frame_due.front().ready_order == time_due.front().ready_order &&
                                        frame_due.front().timer_sequence < time_due.front().timer_sequence)))
                domain = TimerDomain::Frame;

            if (domain == TimerDomain::Time)
                current_entry = state_->PopDue(time_due, domain);
            else
                current_entry = state_->PopDue(frame_due, domain);

            const u64 remaining_budget = callback_budget - invocation_count;
            const u64 dispatch_budget = time_due.empty() && frame_due.empty() ? remaining_budget : 1;
            const u64 dispatched = state_->Dispatch(current_entry.slot, dispatch_budget);
            ErrorHandling::Ensure(
                dispatched != 0 && dispatched <= dispatch_budget,
                "Timer dispatch consumed an invalid number of callbacks");
            invocation_count += dispatched;

            if (current_entry.slot < state_->slots.size())
            {
                State::Slot& slot = state_->slots[current_entry.slot];
                if (slot.generation == current_entry.generation && slot.state == State::SlotState::Pending)
                {
                    ErrorHandling::Ensure(
                        slot.repeating && slot.missed_tick_policy == TimerMissedTickPolicy::InvokeAll,
                        "Only an overdue catch-up timer may remain pending after dispatch");
                    const bool remains_due = slot.domain == TimerDomain::Time
                                                 ? slot.time_deadline <= state_->current_time
                                                 : slot.frame_deadline <= state_->current_frame;
                    ErrorHandling::Ensure(remains_due, "Pending catch-up timer has a future deadline");
                    if (slot.domain == TimerDomain::Time)
                        state_->PushDue(time_due, state_->MakeDueEntry(current_entry.slot), slot.domain);
                    else
                        state_->PushDue(frame_due, state_->MakeDueEntry(current_entry.slot), slot.domain);
                }
            }
            current_entry = {};
        }
        state_->RequeuePending(time_due);
        state_->RequeuePending(frame_due);
    }
    catch (...)
    {
        const std::exception_ptr original_exception = std::current_exception();
        if (current_entry.slot != kInvalidIndex && current_entry.slot < state_->slots.size())
        {
            State::Slot& slot = state_->slots[current_entry.slot];
            if (slot.generation == current_entry.generation &&
                (slot.state == State::SlotState::Pending || slot.state == State::SlotState::Dispatching ||
                 slot.state == State::SlotState::CancelRequested))
                state_->ReleaseSlot(current_entry.slot);
        }
        try
        {
            state_->RequeuePending(time_due);
            state_->RequeuePending(frame_due);
        }
        catch (...)
        {
            state_->CancelDue(time_due);
            state_->CancelDue(frame_due);
            std::rethrow_exception(original_exception);
        }
        std::rethrow_exception(original_exception);
    }
    return invocation_count;
}

bool TimerManager::IsEmpty() const noexcept
{
    return state_->active_count == 0;
}

size_t TimerManager::GetTimerCount() const noexcept
{
    return state_->active_count;
}

TimerDuration TimerManager::GetCurrentTime() const noexcept
{
    return state_->current_time;
}

u64 TimerManager::GetCurrentFrame() const noexcept
{
    return state_->current_frame;
}

std::optional<TimerDuration> TimerManager::GetNextTimeDeadline() const noexcept
{
    if (state_->time_heap.empty()) return std::nullopt;
    return state_->slots[state_->time_heap.front()].time_deadline;
}

std::optional<u64> TimerManager::GetNextFrameDeadline() const noexcept
{
    if (state_->frame_heap.empty()) return std::nullopt;
    return state_->slots[state_->frame_heap.front()].frame_deadline;
}

}  // namespace klvk
