# Text and Utilities

klvk includes small rendering-oriented utilities that are shared by its examples and useful in compact applications.
They use the same vector, resource, and frame conventions as the core rendering APIs.

## Font faces

`FontFace::FromFile` opens a font through FreeType. It can return a glyph in two forms:

- an outline in font units, suitable for arbitrary scaling, filling, stroking, and transformation;
- a coverage bitmap rasterized at a requested pixel size.

Outlines are sequences of move, line, quadratic, cubic, and close commands. They can be fed into custom tessellation or
the 2D curve path. Rasterized glyphs include bitmap dimensions, offsets, and advance metrics for layout.

## Glyph atlases

`GlyphAtlas` packs rasterized glyphs for one font face and pixel size into one `R8` coverage texture. `Add` rasterizes
and packs requested codepoints, `Find` returns packed metrics and texture coordinates, and `RecordPendingUploads`
records all new region copies and their sampling barrier before text drawing.

Packing is append-only. Earlier frames cannot be sampling a region that did not exist when they were recorded, so new
glyphs can be uploaded while those frames remain in flight. Staging storage is kept per frame-in-flight slot for the
same reason.

An atlas does not evict or grow. If it fills, `Add` returns false and leaves the glyph absent. The caller can create a
larger or second atlas, or choose a fallback glyph. Precache known text to avoid rasterization during a frame; otherwise
new glyphs can be added on first use.

The [`text`](../examples/text/code/private/text_example.cpp) example demonstrates per-size atlases, precaching,
incremental uploads, glyph metrics, and viewing the atlas texture.

## Cameras and viewports

`Camera2d` and `RenderTransforms2d` produce world/view/screen transforms for a `Viewport`. The aspect-ratio policy can
fit either axis, and the inverse transforms support cursor picking in world space. `Viewport` represents position and
size as vectors and can match a window or a subregion.

`Camera3d` provides right-handed look-at and Vulkan-compatible perspective transforms, cached forward/right/up basis
vectors, eye and rotation control, near/far planes, and an ImGui editing widget.

Window size and pointer positions are both expressed in framebuffer pixels, so they can be used together for viewport
and picking transforms. Keeping coordinates and dimensions in their vector types avoids accidentally swapping axes.

## Procedural data

`ProceduralMeshGenerator` creates common 2D and 3D vertex/index data, including quads and cubes. Generated positions,
normals, colors, and texture coordinates are ordinary vectors ready for upload or further transformation.

`ProceduralTextureGenerator` creates single-channel masks such as circles and triangles and can mirror them in place.
`VectorIndices2d` iterates a rectangular extent as vector coordinates, which is useful for image and grid algorithms.

## ImGui integration

`Application` initializes ImGui and opens a frame before `Tick`, so derived applications can call ImGui directly there.
The utility layer includes:

- `RegisteredImGuiTexture` and `ImGuiTextureViewer` for displaying Vulkan images;
- `ImGuiEnumCombo`, `ImGuiValueCombo`, and `ImGuiCombo` for selections;
- `SimpleTypeWidget` and `TransformWidget` for reflected values and transforms;
- `ImGuiHelper` for finite sliders, integral sliders, and formatted text.

Registered image handles are RAII objects. Keep the sampled image, view, and sampler alive while ImGui may draw them.

## Numeric aliases

`klvk/integral_aliases.hpp` exposes `u8` through `u64` and `i8` through `i64`; `klvk/float_aliases.hpp` exposes `f32`
and `f64`. The aliases are defined by the math/utility dependency and hoisted for consistency with Vulkan-facing code.

Sizes, counts, dimensions, indices, masks, and identifiers are unsigned. Use a signed type only for a domain that can
meaningfully be negative or to match an explicitly signed external ABI.
