#version 450

layout(location = 0) out vec2 coordinate;

const vec2 corners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main()
{
    coordinate = corners[gl_VertexIndex];
    gl_Position = vec4(coordinate, 0.0, 1.0);
}
