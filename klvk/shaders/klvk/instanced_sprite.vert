#version 450

// One quad per instance. Vertices are generated from gl_VertexIndex,
// per-instance data is pulled from a storage buffer.

struct Instance
{
    vec2 translation;
    vec2 scale;
    uint color;  // packed rgba8
    uint padding;
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

    vec2 corner = kCorners[gl_VertexIndex];
    vec2 view_pos = (world_to_view * vec3(instance.translation, 1)).xy;
    vec2 view_size = (world_to_view * vec3(instance.scale, 0)).xy;
    gl_Position = vec4(corner * view_size + view_pos, 0.0, 1.0);

    out_color = unpackUnorm4x8(instance.color);
    out_tex_coord = corner * 0.5 + 0.5;
}
