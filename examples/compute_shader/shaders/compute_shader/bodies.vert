#version 450
layout(push_constant) uniform Graphics
{
    mat4 mvp;
    vec4 color;
    vec4 body_a; vec4 body_b;
} graphics;
layout(location = 0) out vec4 vertex_color;
void main()
{
    vec3 position = gl_VertexIndex == 0 ? graphics.body_a.xyz : graphics.body_b.xyz;
    gl_Position = graphics.mvp * vec4(position, 1.0);
    gl_PointSize = 15.0;
    vertex_color = graphics.color;
}
