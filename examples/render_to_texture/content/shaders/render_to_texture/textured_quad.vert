#version 450

layout(location = 0) out vec2 texture_coordinates;

const vec2 corners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main()
{
    vec2 position = corners[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    texture_coordinates = position * 0.5 + 0.5;
}
