# Diagnostics

klvk can run the same application interactively, in a hidden native window, or against ordinary offscreen Vulkan
images. A diagnostic configuration combines presentation, an exact logical clock, replayed input, framebuffer or video
output, checkpoint comparison, file-dialog answers, application-specific data, and automatic exit.

Applications opt in by calling `Application::RunWithArguments(argc, argv)`. All bundled examples do.

## Run a configuration

Pass the JSON file after YAE's argument separator:

```sh
yae run klvk_painter2d_example -- --klvk-diagnostics /tmp/painter-run.json
```

Both `--klvk-diagnostics <file>` and `--klvk-diagnostics=<file>` are accepted. Relative output paths in the
configuration resolve against the executable directory (`<build>/bin`), not the process working directory.

This configuration replays input, captures two frames, and exits after the last capture:

```json
{
  "version": 1,
  "presentation": "offscreen",
  "framebuffer_size": [800, 600],
  "clock": {"mode": "fixed", "step_ns": 16666667},
  "input": [
    {"frame": 1, "type": "mouse_move", "position": [400, 300]},
    {"frame": 1, "type": "mouse_button", "button": "left", "action": "press"},
    {"frame": 2, "type": "mouse_button", "button": "left", "action": "release"},
    {"time_ns": 250000000, "type": "key", "key": "w", "action": "press"},
    {"time_ns": 500000000, "type": "key", "key": "w", "action": "release"}
  ],
  "captures": [
    {"frame": 1, "path": "captures/first.ppm", "include_ui": false},
    {"time_ns": 500000000, "path": "captures/half-second.ppm"}
  ],
  "exit": {"after_last_capture": true},
  "application": {"seed": 7}
}
```

## Presentation and framebuffer

`presentation` accepts:

| Value | Behavior |
| --- | --- |
| `visible` | A normal GLFW window, surface, and swapchain. |
| `hidden` | The GLFW surface and swapchain path without a visible desktop window. A display server is still required. |
| `offscreen` | Ordinary color/depth images, no GLFW initialization, native window, surface, swapchain, or display server. |

Presentation defaults to `hidden` when the field is omitted. Offscreen mode requires an explicit `framebuffer_size`.
Offscreen mode provides a `Window` abstraction, so existing applications continue to use the same size, viewport, and
input APIs. Hidden X11 runs briefly realize an undecorated window outside the desktop before hiding it because some
drivers otherwise report a fallback surface extent.

`framebuffer_size` fixes the output dimensions and is required for captures and checkpoints. The diagnostic runner
enforces it even when application initialization requests another window size.

## Exact time and triggers

Triggers use exactly one of:

- `frame`: a one-based rendered frame;
- `time_ns`: an exact unsigned nanosecond deadline;
- `time_seconds`: a hand-authored seconds value rounded to nanoseconds once during parsing.

A fixed clock similarly specifies exactly one of `step_ns` and `step_seconds`. Logical time is the integer product of
frame and step rather than accumulated floating point. Prefer nanoseconds for a configuration that must round-trip
exactly; common rates such as 1/60 second do not have an exact binary floating-point representation.

Time-triggered work occurs on the first rendered frame whose logical time reaches the deadline. Capture, checkpoint,
input, and exit scheduling use a diagnostic `TimerManager` separate from the application's manager, so application
catch-up work cannot delay diagnostic triggers. A fixed clock also controls ImGui time, and configured runs ignore
persisted `imgui.ini` state.

## Input replay

`input` accepts these event shapes:

| Type | Required fields |
| --- | --- |
| `mouse_move` | `position: [x, y]` |
| `mouse_button` | `button`, `action` |
| `mouse_scroll` | `offset: [x, y]` |
| `key` | `key`, `action` |

Every event also needs one trigger. Actions are `press` and `release`; mouse buttons are `left`, `right`, `middle`,
`button4`, and `button5`. Mouse positions use the same framebuffer-pixel coordinates as `Window::GetSize`.

Key names are lowercase. They include `a` through `z`, `0` through `9`, `f1` through `f12`, arrows, `page_up`,
`page_down`, `home`, `end`, `insert`, `delete`, `backspace`, `enter`, `escape`, `space`, `tab`, punctuation names,
`keypad_` names, and side-specific modifiers such as `left_ctrl` and `right_shift`.

Replay input is dispatched before the target frame's application tick. Real mouse and keyboard events are dropped
while configured input is replayed, although ImGui's own GLFW callbacks can still observe a real cursor in visible or
hidden mode. Keep the pointer away from a visible replay if UI hover affects application state.

## Record a session

`--klvk-record-input <file>` records a normal interactive session and writes a complete replayable configuration:

```sh
yae run klvk_falling_sand_example -- --klvk-record-input /tmp/session.json
yae run klvk_falling_sand_example -- --klvk-diagnostics /tmp/session.json
```

The result uses offscreen presentation, the recorded framebuffer size, a fixed clock, frame-triggered input, and an
exit at the session's final frame. File-dialog answers obtained through `Application::OpenFileDialog` and
`SaveFileDialog` are recorded too. Add `captures`, `checkpoints`, or `video` to derive output from the replay.

Events are pinned to the frame on which they arrived rather than wall-clock time. Redundant cursor events collapse to
at most one per frame. This preserves the original input-to-frame association and keeps recordings compact.

File-dialog answers are stored under `dialogs` in request order:

```json
{
  "dialogs": [
    {"frame": 12, "answer": "content/scenes/example.json"},
    {"frame": 40}
  ]
}
```

An omitted `answer` means the user dismissed the dialog. Relative answers resolve against the executable directory.
Applications must use the `Application` dialog methods for these entries to be recorded and replayed.

Replay reproduces input, not every source of nondeterminism. Seed random generators, avoid wall-clock reads, and
control thread scheduling where those influence visible results. Application-specific replay settings belong in the
opaque `application` object and are available during `Initialize` through `GetDiagnosticApplicationConfig()`.

### Watch a replay

Override a configuration's presentation without editing it:

```sh
yae run klvk_falling_sand_example -- \
  --klvk-diagnostics /tmp/session.json \
  --klvk-presentation visible
```

`--klvk-presentation <visible|hidden|offscreen>` requires `--klvk-diagnostics`. A visible fixed-clock replay is paced
to real time so it can be watched; hidden and offscreen runs render as fast as possible.

## Framebuffer captures

`captures` is an array of trigger, path, and optional `include_ui` entries. UI is included by default. Captures are
binary RGB PPM (`P6`). Parent directories are created, completed files atomically replace older files, and shutdown
waits for GPU completion and outstanding writes.

```json
{
  "captures": [
    {"frame": 1, "path": "captures/scene.ppm", "include_ui": false},
    {"time_ns": 1000000000, "path": "/tmp/scene-with-ui.ppm"}
  ],
  "exit": {"after_last_capture": true}
}
```

## Video

`video` records every rendered frame into an MP4 file:

```json
{
  "presentation": "offscreen",
  "framebuffer_size": [1920, 1080],
  "clock": {"mode": "fixed", "step_ns": 16666667},
  "video": {
    "path": "captures/run.mp4",
    "encoding": "h264",
    "encoding_device": "cpu",
    "compression_level": 3,
    "include_ui": true,
    "log_ffmpeg": false
  },
  "exit": {"frame": 600}
}
```

Video requires offscreen presentation, a fixed clock, and even framebuffer dimensions. The clock supplies its frame
rate and exact timebase. `encoding` is `av1` (default), `h264`, or `mpeg4`; `encoding_device` is `cpu` (default) or
`gpu`. MPEG-4 supports CPU only. GPU AV1 and H.264 select FFmpeg's NVIDIA encoders and fail clearly if the hardware or
encoder is unavailable.

`compression_level` ranges from 0 through 10 and defaults to 3. Higher values mean stronger compression and lower
quality. Level 0 is lossless for AV1 and H.264; MPEG-4 uses its highest-quality quantizer because it has no lossless
mode. CPU AV1 selects `libaom-av1` and falls back to `librav1e`; CPU H.264 uses `libx264`.

Pixel conversion and encoding run on a background thread behind a bounded three-frame queue. Rendering waits when the
queue is full, and shutdown drains and joins it. FFmpeg informational logging is enabled by default; set `log_ffmpeg`
to false to silence the `ffmpeg` logger.

## Checkpoints

Checkpoints hash the rendered framebuffer every `every_frames` frames. They locate the first frame at which a replay's
visible output diverges without storing an image for every frame.

```json
{
  "framebuffer_size": [800, 600],
  "clock": {"mode": "fixed", "step_ns": 16666667},
  "checkpoints": {"every_frames": 10, "include_ui": false},
  "exit": {"frame": 120}
}
```

Checkpoints require an explicit framebuffer size and frame-based exit. UI is excluded by default. Bless a file with
the expected hashes, then run it normally:

```sh
yae run klvk_geometry_shader_example -- \
  --klvk-diagnostics run.json \
  --klvk-write-checkpoints blessed.json
yae run klvk_geometry_shader_example -- --klvk-diagnostics blessed.json
```

A mismatch produces a non-zero exit and reports the first divergent frame, expected hash, and actual hash. A hash
covers visible pixels rather than application state; divergence is detected only after it reaches the framebuffer.
Static scenes can also repeat the same hash at every checkpoint, so inspect the blessed hash sequence before treating
it as useful coverage.

## Exit behavior

`exit` contains exactly one of `frame`, `time_ns`, `time_seconds`, or `after_last_capture`. The last form waits for all
requested captures to be submitted. Before `RunWithArguments` returns, klvk finishes framebuffer writes and video
encoding.

Applications that display progress can query a frame-based endpoint with `GetDiagnosticExitFrame`. It is empty for
ordinary runs and for diagnostic runs without a frame exit.

## Profiling

Ordinary interactive applications contain a collapsed `klvk Diagnostics` ImGui window. It can start Linux `perf` on
the current process, pause and resume recording, stop it, and export the result to the Linux perf text format accepted
by [Speedscope](https://www.speedscope.app). Configured diagnostic runs omit this window so it cannot change captures.

Profiles are stored under `perf-recordings/klvk-profile-<process-id>` next to `content`, with a numeric suffix when the
directory already exists. `perf` must be installed and permitted to profile the process.

The same components are public APIs:

- `PerfRecorder` creates raw `perf.data`; call `Start`, `Pause`, `Resume`, `Update`, `Stop`, and `Finish`.
- `SpeedscopeExporter` converts one raw recording, supports cancellation, and reports thread-safe byte progress.

Each exporter handles one operation at a time. Destruction safely finishes an active recorder.

## Regression suite

The [diagnostic smoke-test suite](../diagnostics/smoke/readme.md) captures a versioned set of examples and compares
candidate images against a baseline with exact changed-pixel metrics, hashes, and PPM difference images. Use it for
rendering-sensitive library changes rather than inventing one-off capture commands.
