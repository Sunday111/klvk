# Rendering and Resources

klvk exposes Vulkan-Hpp handles and command-buffer operations. Its rendering layer concentrates on ownership, common
construction patterns, shader-interface validation, and integration with the application frame.

## Graphics pipelines

`GraphicsPipelineBuilder` starts with practical dynamic-rendering defaults: triangle-list topology, filled polygons,
no culling, blending, depth, or stencil, dynamic viewport and scissor, and one presentation-format color attachment.
Configure only the differences and call `Build`:

```cpp
pipeline_ = klvk::GraphicsPipelineBuilder(*this)
                .Layout(pipeline_layout_)
                .VertexShaderFile(GetShaderDir() / "scene/scene.vert.slang")
                .FragmentShaderFile(GetShaderDir() / "scene/scene.frag.slang")
                .VertexBinding(0, sizeof(Vertex), vk::VertexInputRate::eVertex)
                .VertexAttribute(0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, position))
                .AlphaBlend()
                .Build();
```

The application constructor supplies presentation and depth formats. Construct the builder from `DeviceContext`
instead when building for a custom target, then specify `ColorFormat`, `DepthFormat`, or `StencilFormat` as needed.

Shader-file helpers load reflected stages through the device cache. `Stages` accepts externally owned reflected stages;
`UncheckedStages` and `UncheckedLayout` are explicit escape hatches for raw Vulkan structures that cannot be validated.

## Pipeline layouts and descriptor sets

`DescriptorSets::Builder` creates one descriptor set layout, its pool, and any number of identical sets. One set is
enough for immutable resources; use `Application::kFramesInFlight` sets when each points at frame-local buffers.

```cpp
descriptors_ = klvk::DescriptorSets::Builder(GetDeviceContext())
                   .Binding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex)
                   .Binding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
                   .Build(klvk::Application::kFramesInFlight);

const auto layout_view = descriptors_.GetLayoutView();
pipeline_layout_ = klvk::PipelineLayout(GetDeviceContext(), std::span{&layout_view, 1});
```

`WriteBuffer` and `WriteImage` update bindings using the type declared by the builder. Image arrays select their slot
with `array_element`. `PipelineLayout::Validate`, pipeline creation with reflected stages, and
`DescriptorSets::ValidateAgainst` catch mismatched descriptor types, stage visibility, array sizes, push constants,
and shader interfaces before the driver sees an invalid combination.

Push constants are passed as ordinary `vk::PushConstantRange` values when constructing `PipelineLayout` and are
recorded with `vk::CommandBuffer::pushConstants`.

## Buffers

`GpuBuffer` owns a `vk::Buffer` and its VMA allocation. Choose access according to the data flow:

| Host access | Use |
| --- | --- |
| `None` | Device-local data populated by GPU operations. |
| `SequentialWrite` | Persistently mapped data written in order, such as per-frame uniforms or staging. |
| `Random` | Persistently mapped data requiring arbitrary host reads or writes. |

`Write` and `Read` validate the range and operate on host-visible storage. They do not insert Vulkan synchronization;
the caller still controls when the GPU may read or write the buffer. Keep dynamic buffers per frame in flight or wait
for the relevant fence before overwriting them.

## Textures

`Texture` owns a sampled 2D image, VMA allocation, image view, and sampler. Factory functions create single-channel
`R8`, linear four-channel `RGBA8`, encoded-image, or initially empty `R8` textures. Encoded images can be PNG, JPEG,
BMP, TGA, GIF, PSD, HDR, PIC, or PNM; decoding failure returns null.

`RecordRegionUpdates` batches rectangular writes from a staging buffer under one transition pair. It is safe only when
the updated regions are not still sampled by unfinished frames. Append-only allocation, as used by `GlyphAtlas`, meets
that condition; overwriting a live region requires explicit cross-frame synchronization.

## Custom render targets

`RenderTarget` is the image/view interface used by the application pass. `Swapchain` implements it for presentation,
and `OffscreenRenderTarget` implements it with ordinary VMA-backed images. The latter uses
`vk::Format::eR8G8B8A8Unorm` and is the basis of headless diagnostic rendering.

Examples that render into their own texture create the image and attachments, begin a separate dynamic-rendering pass
before the application pass, transition the result for sampling, and then draw it to the presentation target. See
[`render_to_texture`](../examples/render_to_texture/code/private/render_to_texture_example.cpp) for a single pass and
[`post_processing`](../examples/post_processing/code/private/post_processing_example.cpp) for a multi-pass chain.

## Depth and stencil

The render target chooses an optimally tiled depth-stencil format supported by the device, preferring
`D32_SFLOAT_S8_UINT`, then `D24_UNORM_S8_UINT`, then depth-only `D32_SFLOAT`. `GetDepthFormat` reports that choice;
helpers in `klvk/vulkan/depth_stencil_format.hpp` report whether it has stencil and which image aspects it uses.

Depth and stencil are enabled independently:

- `Application::SetDepthBufferEnabled` attaches and clears depth.
- `GraphicsPipelineBuilder::DepthTest` enables depth testing and writing.
- `Application::SetStencilBufferEnabled` attaches and clears stencil without enabling depth.
- `GraphicsPipelineBuilder::StencilTest` configures front and back operations.
- `DynamicStencilMasks` makes compare mask, write mask, and reference command-buffer state.
- `ColorWriteMask({})` creates a pass that changes stencil without changing color.

The [`stencil`](../examples/stencil/code/private/stencil_example.cpp) example implements non-zero and even-odd fills
with stencil-then-cover passes.

## Higher-level renderers

`CurveRenderer2d` batches filled or stroked 2D curve geometry, while `InstancedSpriteRenderer2d` draws many textured
quads from per-instance data. Both integrate with `Application` formats and frame resources but still expose the
command-buffer boundary. Their corresponding `curve`, `painter2d`, `curve_fractal`, and `falling_sand` examples show
the intended data flow.
