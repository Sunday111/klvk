#version 450

layout(push_constant) uniform PushConstants
{
    vec4 transform_col0;
    vec4 transform_col1;
    vec4 transform_col2;
    vec4 color;
} pc;

layout(location = 0) out vec4 out_color;

const vec2 corners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main()
{
    mat3 transform = mat3(pc.transform_col0.xyz, pc.transform_col1.xyz, pc.transform_col2.xyz);
    vec2 position = (transform * vec3(corners[gl_VertexIndex], 1.0)).xy;
    gl_Position = vec4(position, 0.0, 1.0);
    out_color = pc.color;
}
