#version 450

// The quad is generated from gl_VertexIndex - no vertex buffers.

layout(push_constant) uniform PushConstants
{
    vec4 color;
    vec2 scale;
    vec2 translation;
} pc;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_tex_coord;

const vec2 kCorners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main()
{
    vec2 corner = kCorners[gl_VertexIndex];
    gl_Position = vec4(corner * pc.scale + pc.translation, 0.0, 1.0);
    out_color = pc.color;
    out_tex_coord = corner * 0.5 + 0.5;
}
