#version 450

// One quad per instance. Vertices are generated from gl_VertexIndex,
// per-instance data is pulled from a storage buffer.

struct Instance
{
    vec2 translation;
    vec2 scale;
    uint color;  // packed rgba8
    float rotation_radians;
};

layout(std430, set = 0, binding = 1) readonly buffer Instances
{
    Instance instances[];
};

layout(push_constant) uniform PushConstants
{
    vec4 col0;  // columns of the world-to-view matrix
    vec4 col1;
    vec4 col2;
} pc;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_tex_coord;

const vec2 kCorners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main()
{
    mat3 world_to_view = mat3(pc.col0.xyz, pc.col1.xyz, pc.col2.xyz);
    Instance instance = instances[gl_InstanceIndex];

    vec2 unit_corner = kCorners[gl_VertexIndex];
    vec2 corner = unit_corner * instance.scale;
    float sine = sin(instance.rotation_radians);
    float cosine = cos(instance.rotation_radians);
    corner = mat2(cosine, sine, -sine, cosine) * corner;
    vec2 view_position = (world_to_view * vec3(instance.translation + corner, 1)).xy;
    gl_Position = vec4(view_position, 0.0, 1.0);

    out_color = unpackUnorm4x8(instance.color);
    // Texture coordinates come from the unrotated unit quad so the whole
    // texture always maps onto the sprite regardless of scale and rotation.
    out_tex_coord = unit_corner * 0.5 + 0.5;
}
