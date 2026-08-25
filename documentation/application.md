# Application Model

`klvk::Application` owns the long-lived runtime state and presents one stable place for application code to configure
the window, create GPU resources, record a frame, and access input, events, timers, and diagnostics.

## Lifecycle

Override only the stages an application needs and call the corresponding base implementation:

| Stage | Purpose |
| --- | --- |
| `Initialize` | Create the window backend, Vulkan device, render target, frame resources, shader cache, and ImGui; then create application resources. |
| `PreTick` | Begin the frame and its dynamic-rendering pass. |
| `BeforeSwapchainRender` | Record work immediately before the presentation target pass. Useful for transfers or offscreen passes. |
| `Tick` | Record application drawing commands inside the active presentation pass. |
| `PostTick` | Finish UI and frame recording. |
| `MainLoop` | Drive frames until `WantsToClose`; rarely overridden. |

`RunWithArguments` parses klvk's diagnostic options and delegates to the normal run path. A derived `Run` can customize
startup, but most applications only override `Initialize` and `Tick`.

## Recording a frame

During the interval from `PreTick` through `PostTick`:

- `GetCurrentCommandBuffer()` returns the primary command buffer currently being recorded.
- `GetFrameInFlightIndex()` selects the current copy of per-frame resources.
- `Tick` runs inside a dynamic-rendering pass targeting the current presentation image.
- The viewport and scissor cover the current framebuffer unless application code changes them.

klvk permits two CPU-recorded frames in flight (`Application::kFramesInFlight`). Dynamic uniform or staging data that
can still be consumed by the GPU therefore needs one copy per frame-in-flight slot. Persistent scene resources do not.

Use `BeforeSwapchainRender` for commands that cannot execute inside the presentation pass—for example, rendering to an
offscreen target, image transitions, or texture uploads. End any custom pass before returning so klvk can begin its own
pass.

## Window and presentation

`GetWindow()` exposes framebuffer size, aspect ratio, cursor position, title, and resize control. klvk converts native
cursor positions into framebuffer pixels, so `GetSize`, resize events, and pointer coordinates share one space even on
scaled displays. `GetFramebufferSize` reads that size directly from the platform when a resize event may not yet have
arrived.

Interactive runs create a GLFW window and swapchain. Diagnostic runs can instead choose a hidden window or a fully
offscreen render target. Application code uses the same `Window` and frame APIs in all three modes.

Presentation behavior is configured with:

- `SetSwapchainPresentMode` for low-latency or synchronized presentation preferences.
- `SetTargetFramerate` for optional CPU frame pacing.
- `WantsToClose` and `OnApplicationQuitRequested` events for termination.

## Frame state

The application reports time relative to startup:

- `GetTimeSeconds()` is the current logical time.
- `GetCurrentFrameStartTime()` is the logical start of the active frame.
- `GetLastFrameDurationSeconds()` and `GetFramerate()` report recent frame behavior.

These floating-point accessors are convenient for animation. Use exact `TimerDuration` values and diagnostic
nanoseconds when a deadline must round-trip precisely.

`SetClearColor` controls the presentation color clear. Depth and stencil attachment use is opt-in through
`SetDepthBufferEnabled` and `SetStencilBufferEnabled`; see [depth and stencil](rendering.md#depth-and-stencil).

## Runtime paths

`GetExecutableDir()` is independent of the process working directory. By default:

- `GetContentDir()` returns `<executable-dir>/content`.
- `GetShaderDir()` returns `<content-dir>/shaders`.
- the persistent shader cache is a sibling named `shader_cache` beside `content`.

Override the content or shader accessor when an embedding application uses another asset layout. Prefer these APIs to
constructing paths from the current working directory.

## Device access

`GetDeviceContext()` exposes the Vulkan instance, physical and logical devices, graphics queue, queue family, and VMA
allocator. It also owns shader compilation and one-time command submission. `GetSwapchainFormat()` returns the current
presentation color format, including the fixed offscreen format in offscreen mode; `GetDepthFormat()` returns the
chosen depth-stencil format.

Application code owns every resource it creates and must keep it alive for as long as submitted commands can use it.
Member declaration order is useful here: derived members are destroyed before the base tears down the device.

## File dialogs and deterministic runs

Use `Application::OpenFileDialog` and `SaveFileDialog` instead of constructing `FileDialog` directly. Ordinary runs
show the platform dialog, input recordings preserve the answer, and diagnostic replays return the recorded answer
without opening a native UI. This keeps workflows that load or save data replayable.
