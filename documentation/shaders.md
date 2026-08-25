# Shaders

klvk compiles self-contained Slang stage files to SPIR-V at runtime. Compilation is content-addressed, reflected, and
cached, so application pipelines can validate their host-side layouts against the shader interface.

## Source layout

Put shader files below the application's shader directory, which defaults to `<content>/shaders`. Stage is inferred
from the filename suffix:

| Suffix | Stage |
| --- | --- |
| `.vert.slang` | Vertex |
| `.tesc.slang` | Tessellation control |
| `.tese.slang` | Tessellation evaluation |
| `.geom.slang` | Geometry |
| `.frag.slang` | Fragment |
| `.comp.slang` | Compute |

Shader files must currently be self-contained. Imports and includes fail explicitly because a persistent cache entry
does not yet track transitive source dependencies.

`Application::Initialize` initializes the device shader cache with `GetShaderDir()` as its source root. Paths passed to
pipeline builders and `DeviceContext::LoadShaderModule` must remain below that root.

## Compilation and caching

`ShaderCacheManager` serializes Slang work on one background thread. Concurrent requests for the same source are
coalesced, compiled SPIR-V remains available in process memory, and validated content-addressed entries are flushed to
disk periodically. By default the persistent `shader_cache` directory sits beside the executable's `content`
directory. Embedders that initialize a `DeviceContext` themselves can provide another cache root.

Changing a staged shader requires rebuilding the executable target so YAE copies the new content, then restarting the
application. It does not require reconfiguring CMake.

## Reflection and validation

Every compiled stage produces a `ShaderInterface` describing entry-point inputs and outputs, descriptors, memory
layouts, push constants, and specialization constants. `ShaderStages::MergeInterfaces` validates stage-to-stage
compatibility and produces a program interface.

Reflected pipeline construction validates:

- vertex attributes against vertex-stage inputs;
- stage outputs against the next stage's inputs;
- descriptor set, binding, type, count, and stage visibility;
- buffer member offsets, sizes, scalar types, vectors, matrices, and matrix layout;
- push-constant ranges and specialization constants.

Use reflected `PipelineLayout` and `GraphicsPipelineBuilder::Stages` wherever possible. The `Unchecked` APIs exist for
foreign SPIR-V or manually described interfaces and intentionally give up these checks.

## Loading individual stages

The direct path is useful when stage filenames are unrelated:

```cpp
klvk::DeviceContext& context = GetDeviceContext();
klvk::ShaderModule vertex = context.LoadShaderModule(GetShaderDir() / "scene/scene.vert.slang");
klvk::ShaderModule fragment = context.LoadShaderModule(GetShaderDir() / "scene/scene.frag.slang");

klvk::ShaderStages stages(vk::ShaderStageFlagBits::eVertex, vertex);
stages.Append(klvk::ShaderStages(vk::ShaderStageFlagBits::eFragment, fragment));
```

`ShaderStages` owns its Vulkan create-info and specialization backing storage but borrows module handles. Keep the
`ShaderModule` objects alive until pipeline creation completes.

For the common one-off graphics pipeline, the `VertexShaderFile`, `FragmentShaderFile`, and other stage helpers on
`GraphicsPipelineBuilder` own the temporary modules through `Build`.

## Named shader programs and definitions

`klvk::Shader` loads all present `<name>.<stage>.slang` files with a shared base name and an optional
`<name>.shader.json`. Set `Shader::shaders_dir_` before constructing one. The configuration maps reflected Vulkan
specialization-constant names to initial overrides, changing values at pipeline creation without recompiling SPIR-V.

```json
{
    "specialization_constants": {
        "USE_GRID": true,
        "ITERATION_COUNT": 200
    }
}
```

Use `GetDefine` or `FindDefine`, then `SetDefineValue` with `bool`, `i32`, `u32`, or `float`. A changed override
increments `GetVersion`; a renderer can compare that version with the one used for its current pipeline and rebuild
from `MakeStages` when needed. The shader object must outlive pipelines being constructed from its stages.

See the `fractal` example for named programs, specialization-driven pipeline rebuilding, graphics stages, and compute
stages in one application.
