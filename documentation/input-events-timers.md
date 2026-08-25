# Input, Events, and Timers

klvk translates platform input into a small typed vocabulary, distributes it through `EventManager`, and provides an
engine-thread scheduler for work tied to logical time or rendered frames. Diagnostic recording and replay use the same
event entry points, so interactive and automated runs reach application code through the same path.

## Input vocabulary

`klvk/input.hpp` defines:

- `InputAction`: `Press` or `Release`.
- `MouseButton`: left, right, middle, button 4, or button 5.
- `Key`: letters, digits, function keys, arrows, navigation/editing keys, punctuation, keypad keys, and left/right
  modifier keys.

Window callbacks emit these event types from `klvk::events`:

| Event | Data |
| --- | --- |
| `OnWindowResize` | Previous and current framebuffer size. |
| `OnMouseMove` | Previous and current cursor position in framebuffer pixels. |
| `OnMouseScroll` | Two-dimensional scroll offset. |
| `OnMouseButton` | Button and action. |
| `OnKey` | Key and action. |
| `OnApplicationQuitRequested` | A request for the application loop to stop. |

Mouse positions, `Window::GetSize`, and resize events use framebuffer pixels. Native window coordinates are converted
at the platform boundary on scaled displays.

## Subscribe with functions

`EventListener<EventTypes...>` binds one function per event type. Keep the returned `EventSubscription` alive for as
long as events should be delivered:

```cpp
using MouseListener = klvk::events::EventListener<klvk::events::OnMouseMove>;

listener_ = MouseListener::FromFunctions(
    [this](const klvk::events::OnMouseMove& event)
    {
        cursor_ = event.current;
    });
subscription_ = GetEventManager().AddEventListener(listener_);
```

The listener must outlive a subscription created from a reference. Alternatively, pass the result of
`PtrFromFunctions`; the event manager then owns the listener.

`EventSubscription` is move-only RAII. Destroying it or calling `Reset` unsubscribes, including when that happens
during event dispatch. Adding, removing, and updating listeners during nested dispatch is supported and applied at a
safe boundary.

## Subscribe object methods

`EventListenerMethodCallbacks` deduces event types from member-function signatures:

```cpp
void OnKey(const klvk::events::OnKey& event);
void OnResize(const klvk::events::OnWindowResize& event);

listener_ = klvk::events::EventListenerMethodCallbacks<
    &ExampleApp::OnKey,
    &ExampleApp::OnResize>::CreatePtr(this);
subscription_ = GetEventManager().AddEventListener(*listener_);
```

Store the listener before its subscription so destruction unsubscribes before destroying the listener. An owned
listener can instead be moved directly into `AddEventListener`.

Applications can define and emit their own reflected event types with `EventManager::Emit`. The type's reflection
identity is the routing key; there is no string-based event lookup.

## Timers

`TimerManager` schedules callbacks in two monotonic domains:

- time deadlines use `TimerDuration`, an exact unsigned nanosecond duration;
- frame deadlines use one-based rendered frame numbers.

Every `Application` exposes its manager through `GetTimerManager`. The main loop advances it after `PreTick` and
immediately before `Tick`, using application logical time and the frame about to be rendered. Application code should
schedule, cancel, and inspect timers but must not call `Advance` on this manager.

```cpp
using namespace std::chrono_literals;

quit_timer_ = GetTimerManager().ScheduleAfter(
    std::chrono::duration_cast<klvk::TimerDuration>(2s),
    [this](const klvk::TimerEvent&)
    {
        GetEventManager().Emit(klvk::events::OnApplicationQuitRequested{});
    });
```

Use `TimerDurationFromSeconds` where floating-point seconds enter the exact timing domain. It rejects negative,
non-finite, out-of-range, and non-zero values that round to zero. `TimerDurationToSeconds` is for display and is lossy.

### Scheduling choices

| API | Meaning |
| --- | --- |
| `ScheduleAt` / `ScheduleAtFrame` | One callback at an absolute logical deadline. |
| `ScheduleAfter` / `ScheduleAfterFrames` | One callback relative to the manager's current position. |
| `ScheduleEvery` / `ScheduleEveryFrames` | Fixed-rate repetition after one interval. |
| `ScheduleEveryAt` / `ScheduleEveryAtFrame` | Fixed-rate repetition with an explicit first deadline. |

A repeating timer chooses a missed-tick policy:

- `Coalesce` invokes once for the latest due occurrence and reports how many earlier occurrences were missed.
- `InvokeAll` invokes every due occurrence, interleaved chronologically with other work in that domain.

Callbacks may cancel themselves or other timers, clear the manager, and schedule new work. Newly scheduled due work
waits until the next `Advance`, preventing recursive starvation. Equal deadlines are FIFO. A callback budget bounds
each advance, and readiness rotates between time and frame domains so neither monopolizes a small budget. If a callback
throws, that timer is cancelled, other due timers remain scheduled, and the exception propagates.

`TimerManager` is intentionally not thread-safe. Own and mutate it on the engine thread. Its indexed min-heaps make
the next deadline O(1) and scheduling or immediate cancellation O(log n), without cancelled tombstones.
