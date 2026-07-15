#version 450

// The quad is generated from gl_VertexIndex - no vertex buffers.

layout(push_constant) uniform PushConstants
{
    vec4 col0;  // columns of the transform matrix
    vec4 col1;
    vec4 col2;
    vec4 color;
} pc;

layout(location = 0) out vec4 out_color;

const vec2 kCorners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main()
{
    mat3 transform = mat3(pc.col0.xyz, pc.col1.xyz, pc.col2.xyz);
    vec2 corner = kCorners[gl_VertexIndex];
    gl_Position = vec4((transform * vec3(corner, 1)).xy, 0.0, 1.0);
    out_color = pc.color;
}
