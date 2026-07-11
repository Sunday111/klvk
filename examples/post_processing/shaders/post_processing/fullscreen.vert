#version 450
layout(location = 0) out vec2 uv;
const vec2 corners[6] = vec2[](
    vec2(-1,-1), vec2(1,-1), vec2(1,1),
    vec2(-1,-1), vec2(1,1), vec2(-1,1));
void main()
{
    vec2 p = corners[gl_VertexIndex];
    gl_Position = vec4(p, 0, 1);
    uv = p * 0.5 + 0.5;
}
