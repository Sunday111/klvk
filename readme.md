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

## Diagnostic runs and framebuffer capture

`Application::RunWithArguments(argc, argv)` recognizes `--klvk-diagnostics <file>` and
`--klvk-diagnostics=<file>`. The JSON file
controls presentation, deterministic timing, any number of framebuffer captures, and automatic exit. All klvk examples
use this entry point.

```json
{
  "version": 1,
  "presentation": "hidden",
  "framebuffer_size": [800, 600],
  "clock": {"mode": "fixed", "step_seconds": 0.016666666666666666},
  "captures": [
    {"frame": 1, "path": "captures/first.ppm", "include_ui": false},
    {"time_seconds": 0.25, "path": "captures/quarter-second.ppm"},
    {"time_seconds": 0.5, "path": "captures/half-second.ppm"}
  ],
  "exit": {"after_last_capture": true},
  "application": {"seed": 7}
}
```

- `presentation` is `visible` or `hidden`. Hidden presentation still uses a GLFW window, Vulkan surface, and swapchain,
  so it requires a display server. It is not the future display-independent `offscreen` backend. On X11, klvk briefly
  realizes the undecorated window outside the desktop before hiding it because some Vulkan drivers otherwise report a
  fallback surface extent.
- `framebuffer_size` is required when captures are present and is enforced exactly, including after an example changes
  its window size during initialization.
- `captures` is an array, so a run may contain any number of frame and time points. Each entry contains exactly one of
  one-based `frame` or non-negative `time_seconds`. A time capture occurs on the first rendered frame whose diagnostic
  time reaches the requested point. `include_ui` defaults to `true`.
- Capture files are binary RGB PPM (`P6`). Relative paths are resolved against the executable directory, not the process
  working directory. Parent directories are created and completed files atomically replace older captures.
- `exit` contains exactly one of `frame`, `time_seconds`, or `after_last_capture`. The last form waits for every requested
  capture to be submitted; klvk waits for GPU completion and finishes writing the files before `Run` returns.
- A fixed clock controls klvk and ImGui frame time, and diagnostic runs ignore persisted `imgui.ini` state. Applications
  can read their optional object through `GetDiagnosticApplicationConfig()`.

With yae, pass application arguments after `--`:

```sh
yae run klvk_painter2d_example -- --klvk-diagnostics /tmp/painter-capture.json
```

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
