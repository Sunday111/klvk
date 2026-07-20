# klvk

Vulkan rendering library. A Vulkan counterpart of [klgl](https://github.com/Sunday111/klgl) with the same high-level
API (`Application`, `Window`, events, camera) built on Vulkan 1.3 with dynamic rendering.

- Function loading via [volk](https://github.com/zeux/volk).
- Memory management via [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).
- GLSL is staged at build time and compiled to SPIR-V on demand with `shaderc`.
- `ShaderCacheManager` coalesces concurrent requests, retains SPIR-V in memory, and periodically persists validated,
  content-addressed entries from its worker thread. By default the persistent `shader_cache` directory is created next
  to the executable's `content` directory; embedders may provide an explicit path.
- ImGui with the GLFW + Vulkan backends.

This is a [yae](https://github.com/Sunday111/yae) package: add it as a package dependency and link the `klvk` module.

## Integral aliases

Public headers use the exact-width aliases from `klvk/integral_aliases.hpp`: `u8`, `u16`, `u32`, and `u64`.
`klvk/signed_integral_aliases.hpp` additionally provides `i8`, `i16`, `i32`, and `i64`. Include the signed header only for
domains that can meaningfully be negative, or when matching an explicitly signed external ABI. Sizes, counts,
dimensions, indices, masks, and identifiers should remain unsigned.

## Timers

`klvk/timing/timer_manager.hpp` provides render-thread scheduling in elapsed-time and frame domains. It uses indexed
binary min-heaps and generational handles: finding the next timer is O(1), while scheduling and immediate cancellation
are O(log n) without cancelled tombstones. Equal deadlines are FIFO.

Timers may be one-shot or fixed-rate repeating. Repeating timers explicitly choose whether missed occurrences are all
invoked or coalesced into the latest logical occurrence. Callbacks may schedule timers, cancel themselves or other
timers, and clear the manager. Work scheduled by a callback is deliberately deferred until the next `Advance`, which
prevents recursive starvation. `Advance` also has a callback budget. `InvokeAll` occurrences are merged chronologically
with other due work in the same domain rather than dispatched as one timer-sized batch; a rotating readiness order
between the otherwise incomparable time and frame domains prevents either from monopolizing a small budget. Remaining
catch-up work resumes on the next advance without skipping occurrences. A callback that throws cancels that timer,
preserves all other due timers, and propagates the exception. `TimerManager` is intentionally not synchronized; advance
it and mutate it from its owning engine thread. Supply logical elapsed time to `Advance` rather than letting the manager
read a wall clock, so fixed-step and deterministic runtimes use the same scheduler.

Every `Application` owns a `TimerManager`, exposed through `GetTimerManager()`. The main loop advances it after
`PreTick` and immediately before the application's `Tick`, using the application's logical elapsed time and one-based
rendered-frame number. Applications therefore receive timer callbacks at a stable point after frame setup and before
their per-frame logic. Diagnostics use a separate manager so application catch-up work cannot delay deterministic
captures. The main loop owns `Advance`; application code uses the returned manager only to schedule, cancel, and inspect
timers.

## Diagnostic runs, framebuffer capture, and video

`Application::RunWithArguments(argc, argv)` recognizes `--klvk-diagnostics <file>` and
`--klvk-diagnostics=<file>`. The JSON file
controls presentation, deterministic timing, framebuffer and video capture, and automatic exit. All klvk examples use
this entry point.

```json
{
  "version": 1,
  "presentation": "offscreen",
  "framebuffer_size": [800, 600],
  "clock": {"mode": "fixed", "step_seconds": 0.016666666666666666},
  "input": [
    {"frame": 1, "type": "mouse_move", "position": [400, 300]},
    {"frame": 1, "type": "mouse_button", "button": "left", "action": "press"},
    {"frame": 2, "type": "mouse_button", "button": "left", "action": "release"},
    {"time_seconds": 0.25, "type": "mouse_scroll", "offset": [0, 1]},
    {"time_seconds": 0.25, "type": "key", "key": "w", "action": "press"},
    {"time_seconds": 0.5, "type": "key", "key": "w", "action": "release"}
  ],
  "captures": [
    {"frame": 1, "path": "captures/first.ppm", "include_ui": false},
    {"time_seconds": 0.25, "path": "captures/quarter-second.ppm"},
    {"time_seconds": 0.5, "path": "captures/half-second.ppm"}
  ],
  "video": {
    "path": "captures/run.mp4",
    "encoding": "h264",
    "encoding_device": "gpu",
    "compression_level": 3,
    "include_ui": true
  },
  "exit": {"after_last_capture": true},
  "application": {"seed": 7}
}
```

- `presentation` is `visible`, `hidden`, or `offscreen`. Offscreen presentation uses ordinary Vulkan color and depth
  images, does not initialize GLFW, and requires neither a native window nor a display server. It provides a logical
  window with the configured framebuffer size so existing applications keep the same viewport API. Hidden presentation
  retains the GLFW window, Vulkan surface, and swapchain path. On X11, klvk briefly realizes the undecorated hidden
  window outside the desktop before hiding it because some Vulkan drivers otherwise report a fallback surface extent.
- `framebuffer_size` is required when captures are present and is enforced exactly, including after an example changes
  its window size during initialization.
- `input` schedules mouse and keyboard events before the target frame's application tick. Each event contains exactly
  one of one-based `frame` or non-negative `time_seconds`. Supported types are `mouse_move` with `position`,
  `mouse_button` with `button` and `action`, `mouse_scroll` with `offset`, and `key` with `key` and `action`. Actions are
  `press` and `release`; mouse buttons are `left`, `right`, `middle`, `button4`, and `button5`. Mouse positions use
  logical window coordinates, matching native cursor events.
- Key names are lowercase: `a` through `z`, `0` through `9`, `f1` through `f12`, arrows (`left`, `right`, `up`, `down`),
  navigation and editing keys (`page_up`, `page_down`, `home`, `end`, `insert`, `delete`, `backspace`, `enter`, `escape`,
  `space`, `tab`), punctuation names, keypad names prefixed with `keypad_`, and left/right modifiers such as
  `left_ctrl`, `right_shift`, `left_alt`, and `right_super`.
- `captures` is an array, so a run may contain any number of frame and time points. Each entry contains exactly one of
  one-based `frame` or non-negative `time_seconds`. A time capture occurs on the first rendered frame whose diagnostic
  time reaches the requested point. `include_ui` defaults to `true`.
- Capture files are binary RGB PPM (`P6`). Relative paths are resolved against the executable directory, not the process
  working directory. Parent directories are created and completed files atomically replace older captures.
- `video` records every rendered frame in an MP4 file. `encoding` is `av1` (the default), `h264`, or `mpeg4`.
  `encoding_device` is `cpu` (the default) or `gpu`. CPU AV1 uses the system FFmpeg's `libaom-av1` encoder and falls
  back to `librav1e`; CPU H.264 uses `libx264`. GPU AV1 and H.264 use FFmpeg's `av1_nvenc` and `h264_nvenc` encoders,
  respectively, and require matching NVIDIA hardware support. Requesting unavailable GPU support fails during
  initialization with an explicit error. MPEG-4 supports only CPU encoding. `compression_level` is an integer from 0
  through 10 and defaults to 3. Higher values produce stronger compression and lower quality. Level 0 selects lossless
  AV1 or H.264; MPEG-4 does not support lossless output, so level 0 selects its highest-quality quantizer. Video is
  currently available only with `offscreen` presentation and requires a fixed clock and even framebuffer dimensions.
  The fixed clock sets the output frame rate; `include_ui` defaults to `true`. The system FFmpeg development packages
  must provide `libavformat`, `libavcodec`, `libavutil`, and `libswscale` through `pkg-config`. klvk links their shared
  libraries and does not require a custom FFmpeg build. Pixel conversion and encoding run on a background thread behind
  a bounded three-frame queue; rendering waits when that queue is full, and shutdown drains and joins the encoder.
  Informational FFmpeg and encoder output is enabled by default through spdlog's `ffmpeg` logger; set `log_ffmpeg` to
  `false` in the `video` object to silence it.
- `exit` contains exactly one of `frame`, `time_seconds`, or `after_last_capture`. The last form waits for every requested
  capture to be submitted; klvk waits for GPU completion and finishes writing capture and video files before `Run`
  returns.
- Capture and exit points are one-shot `TimerManager` jobs. Their callbacks emit typed capture/application-quit events;
  an interactive run creates no diagnostic timers and does no trigger-list polling.
- A fixed clock controls klvk and ImGui frame time, and diagnostic runs ignore persisted `imgui.ini` state. Applications
  can read their optional object through `GetDiagnosticApplicationConfig()`.

With yae, pass application arguments after `--`:

```sh
yae run klvk_painter2d_example -- --klvk-diagnostics /tmp/painter-capture.json
```

For repeatable main-versus-branch rendering checks, see the
[diagnostic smoke-test suite](diagnostics/smoke/readme.md).

## Vulkan API wrappers

`klvk::Vulkan` provides three forms for Vulkan calls that return `VkResult`:

- `NE` invokes Vulkan without error handling and returns the raw result plus any typed output.
- `CE` returns failures as `VulkanError` through `tl::expected` or `std::optional`, without throwing.
- The unsuffixed form delegates to `CE` and throws the same `VulkanError`.

`VulkanError` preserves the `VkResult`, call context, and captured stack trace. Expected runtime conditions are typed
outcomes instead of exceptions—for example, acquire, present, and fence-wait statuses include suboptimal, out-of-date,
not-ready, and timeout states where applicable.

Wrappers are compiled into `klvk` by default. Configure with `-DKLVK_INLINE_VULKAN_WRAPPERS=ON` to include their
implementations inline from the public header instead. The CMake option applies the corresponding definition to the
library and its consumers consistently; do not override it per translation unit.
