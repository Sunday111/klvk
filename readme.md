# klvk

Vulkan rendering library. A Vulkan counterpart of [klgl](https://github.com/Sunday111/klgl) with the same high-level
API (`Application`, `Window`, events, camera) built on Vulkan 1.3 with dynamic rendering.

- Function loading via [volk](https://github.com/zeux/volk).
- Memory management via [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).
- Shaders are compiled to SPIR-V at build time with `glslc`.
- ImGui with the GLFW + Vulkan backends.

This is a [yae](https://github.com/Sunday111/yae) package: add it as a package dependency and link the `klvk` module.

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
