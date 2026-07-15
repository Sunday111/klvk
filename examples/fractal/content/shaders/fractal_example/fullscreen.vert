#version 450

// Fullscreen quad generated from gl_VertexIndex - no vertex buffers.

layout(location = 0) out vec2 uv;

const vec2 kCorners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main()
{
    vec2 corner = kCorners[gl_VertexIndex];
    gl_Position = vec4(corner, 1, 1.0);
    uv = (corner + 1) / 2;
}
