# Examples

Every directory under `examples/` is a YAE executable module. A project consuming the klvk package discovers these
targets automatically; run `yae list --all` from that project to see the exact set available in its checkout.

## Fundamentals

| Target | Demonstrates |
| --- | --- |
| `klvk_minimal_quad_example` | The shortest application, push constants, a pipeline layout, and a direct draw. |
| `klvk_textured_quad_example` | Procedural `R8` texture creation, descriptors, sampling, and alpha blending. |
| `klvk_two_textures_example` | Multiple sampled images and updating descriptor sets. |
| `klvk_minimal_cube_example` | Vertex/index buffers, depth testing, push constants, and `Camera3d`. |
| `klvk_simple_lit_cube_example` | Normals, simple lighting, multiple objects, camera control, and ImGui settings. |

Start with `minimal_quad`, then choose `textured_quad` for resource binding or `minimal_cube` for 3D.

## Vulkan techniques

| Target | Demonstrates |
| --- | --- |
| `klvk_compute_shader_example` | Compute and graphics pipelines sharing buffers, dispatch barriers, and mouse input. |
| `klvk_geometry_shader_example` | Optional geometry-shader feature detection and point expansion. |
| `klvk_render_to_texture_example` | A custom offscreen color image rendered before and sampled by the presentation pass. |
| `klvk_post_processing_example` | Multiple offscreen passes and a blur/composite chain. |
| `klvk_stencil_example` | Independent stencil attachment use and non-zero/even-odd stencil-then-cover filling. |

## 2D rendering and applications

| Target | Demonstrates |
| --- | --- |
| `klvk_curve_example` | Curve control points, thickness, subdivision, colors, and composition modes. |
| `klvk_curve_fractal_example` | Large curve generation, accumulation, offscreen rendering, and compositing. |
| `klvk_painter2d_example` | Instanced sprites, procedural masks, shapes, and 2D composition. |
| `klvk_falling_sand_example` | Grid simulation, `Camera2d`, pointer interaction, and instanced rendering. |
| `klvk_pendulum_example` | A small simulation combining sprites and curve trails. |
| `klvk_tetris_example` | A complete keyboard-driven application using instanced sprites and timers. |
| `klvk_text_example` | FreeType glyphs, per-size atlases, incremental texture updates, and atlas inspection. |
| `klvk_fractal_example` | CPU, graphics, and compute implementations, shader definitions, UI, and diagnostics. |

## Running examples

Build and run from a consumer project:

```sh
yae run klvk_minimal_quad_example
```

Arguments after `--` go to the executable. This is how diagnostic configurations are passed:

```sh
yae run klvk_text_example -- --klvk-diagnostics /absolute/path/to/run.json
```

Runtime assets are staged according to what the selected target links. Rebuild after changing a shader, font, image,
or other file under `content/`; no CMake regeneration is needed for content-only changes.

For automated rendering coverage, use the repository's [diagnostic smoke suite](../diagnostics/smoke/readme.md). Its
suite definition intentionally excludes examples whose output is not deterministic.
