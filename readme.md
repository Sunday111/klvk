# klvk

klvk is a C++23 Vulkan rendering library for compact interactive applications, visualizations, and experiments. It
provides an application loop, window and input events, dynamic rendering, resource ownership, shader compilation and
reflection, ImGui integration, cameras, text, and focused 2D renderers without hiding Vulkan command recording.

## Highlights

- **A useful application layer.** `Application` owns the window or offscreen target, Vulkan device, frame resources,
  dynamic-rendering pass, ImGui frame, event manager, and engine-thread timer manager.
- **Vulkan with less ceremony.** Move-only buffers, textures, pipeline layouts, descriptor sets, render targets, and a
  fluent graphics-pipeline builder remove repetitive ownership and setup code while retaining Vulkan-Hpp types.
- **Reflected Slang shaders.** Shader stages compile to SPIR-V on demand, cache in memory and on disk, and expose their
  reflected interfaces for descriptor, push-constant, and stage validation.
- **Deterministic diagnostics.** Applications can replay recorded input with a fixed clock, render without a display
  server, capture framebuffers and video, and compare periodic framebuffer checkpoints.
- **Ready-made rendering tools.** Cameras, procedural meshes and textures, curve and sprite renderers, font outlines,
  an incrementally updated glyph atlas, and ImGui helpers cover common small-application needs.
- **Traceable failures.** Vulkan-Hpp exceptions retain their `vk::Result` and carry cpptrace stack traces.

klvk is distributed as a [YAE](https://github.com/Sunday111/yae) package. Applications declare the package and link
the `klvk` module; the repository's examples are available to any YAE project that consumes the package.

## Documentation

| Topic | What's in it |
| --- | --- |
| [Getting started](documentation/getting-started.md) | Requirements, adding the package, building, and running examples. |
| [Application model](documentation/application.md) | Lifecycle, frame recording, windows, content paths, and frame pacing. |
| [Rendering and resources](documentation/rendering.md) | Pipelines, descriptors, buffers, textures, render targets, depth, and stencil. |
| [Shaders](documentation/shaders.md) | Slang source layout, compilation cache, reflection, and specialization constants. |
| [Input, events, and timers](documentation/input-events-timers.md) | Typed events, subscription lifetime, input vocabulary, and scheduling. |
| [Text and utilities](documentation/text-and-utilities.md) | Fonts, glyph atlases, cameras, procedural data, ImGui, and numeric aliases. |
| [Diagnostics](documentation/diagnostics.md) | Deterministic runs, input recording and replay, captures, video, checkpoints, and profiling. |
| [Examples](documentation/examples.md) | What each example demonstrates and how to choose a starting point. |
| [Vulkan conventions](documentation/vulkan.md) | Vulkan-Hpp configuration, ownership, errors, and synchronization boundaries. |

The rendering regression suite has its own [smoke-test guide](diagnostics/smoke/readme.md).
