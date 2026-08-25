# Vulkan Conventions

klvk uses Vulkan-Hpp with dynamic dispatch, enhanced-mode results, unique handles, and no Vulkan prototypes. Include
`klvk/vulkan/vulkan.hpp` before using Vulkan-Hpp directly so every translation unit receives the same configuration.

## Handles and ownership

Long-lived Vulkan objects use `vk::Unique*` handles or klvk move-only wrappers. Borrowed raw handles are returned from
accessors for command recording; ownership stays with the wrapper. In particular:

- `DeviceContext` owns the instance, optional surface, physical/logical device state, queue, command pool, and VMA
  allocator.
- `GpuBuffer` and `Texture` pair Vulkan resources with their VMA allocations.
- `DescriptorSets` owns its layout and pool; allocated sets live with that pool.
- `PipelineLayout` owns the Vulkan pipeline layout and its host-side validation description.
- `ShaderModule` owns the module and shares immutable reflection metadata.

Destroy resources only after submitted commands that reference them have completed. `Application` waits for its device
at shutdown, but replacing resources while it runs still requires the caller to respect frame fences or keep retired
resources alive.

## Dispatch and errors

Enhanced Vulkan-Hpp calls throw exceptions on failing `vk::Result` values. klvk's configured exceptions retain the
result code and capture a cpptrace stack trace. `VulkanCheck` applies the same policy at raw C API boundaries.

Do not pass ordinary multi-outcome operations through `VulkanCheck`. Acquire and present, for example, can validly
report suboptimal or out-of-date results and are handled from their returned Vulkan-Hpp result values.

`ErrorHandling::InvokeAndCatchAll` is the intended executable boundary. It reports traced runtime errors, standard
exceptions, and unknown failures consistently and returns a non-zero result.

## Dynamic rendering

klvk requires Vulkan 1.3 and records presentation with dynamic rendering rather than render passes and framebuffers.
The application controls the current color, depth, and stencil attachment formats, and pipeline creation declares
matching formats through `vk::PipelineRenderingCreateInfo`.

`GetCurrentCommandBuffer` is valid only during the active frame. Inside `Tick`, it is already inside the application's
presentation pass. Record custom passes and the transitions around them in `BeforeSwapchainRender`.

## Synchronization responsibilities

The application loop owns swapchain acquisition, presentation, per-frame command-pool reset, submit fences, and the
transitions around its presentation target. Resource helpers may record the transitions needed by their own operation,
but they do not infer all producer/consumer dependencies in application code.

The caller remains responsible for:

- barriers between compute, transfer, attachment, and sampling uses it records;
- not overwriting buffers or image regions still read by an unfinished frame;
- matching custom attachment layouts and pipeline formats;
- keeping borrowed handles and pointed-to create-info storage alive through the consuming Vulkan call.

`DeviceContext::SubmitOneTimeCommands` is intended for initialization work. It allocates, submits to the graphics
queue, and waits for completion, making it simple but inappropriate for per-frame asynchronous work.

## Optional device features

`DeviceContext` enables and reports optional geometry shaders, tessellation shaders, and external-memory file
descriptors when the selected physical device supports them. Check `IsGeometryShaderEnabled`,
`IsTessellationShaderEnabled`, or `IsExternalMemoryFdEnabled` before creating resources or pipelines that require
those features.
