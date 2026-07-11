# klvk

Vulkan rendering library. A Vulkan counterpart of [klgl](https://github.com/Sunday111/klgl) with the same high-level
API (`Application`, `Window`, events, camera) built on Vulkan 1.3 with dynamic rendering.

- Function loading via [volk](https://github.com/zeux/volk).
- Memory management via [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).
- Shaders are compiled to SPIR-V at build time with `glslc`.
- ImGui with the GLFW + Vulkan backends.

This is a [yae](https://github.com/Sunday111/yae) package: add it as a package dependency and link the `klvk` module.
