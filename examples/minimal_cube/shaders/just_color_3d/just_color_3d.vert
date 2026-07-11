#version 450

layout(location = 0) in vec3 position;

layout(push_constant) uniform PushConstants
{
    vec4 transform_col0;
    vec4 transform_col1;
    vec4 transform_col2;
    vec4 transform_col3;
    vec4 color;
} pc;

layout(location = 0) out vec4 out_color;

void main()
{
    mat4 transform = mat4(
        pc.transform_col0,
        pc.transform_col1,
        pc.transform_col2,
        pc.transform_col3);
    gl_Position = transform * vec4(position, 1.0);
    out_color = pc.color;
}
