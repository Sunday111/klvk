#version 450
layout(push_constant) uniform Graphics
{
    vec4 mvp_col0; vec4 mvp_col1; vec4 mvp_col2; vec4 mvp_col3;
    vec4 color;
    vec4 body_a; vec4 body_b;
} graphics;
layout(location = 0) out vec4 vertex_color;
void main()
{
    mat4 mvp = mat4(graphics.mvp_col0, graphics.mvp_col1, graphics.mvp_col2, graphics.mvp_col3);
    vec3 position = gl_VertexIndex == 0 ? graphics.body_a.xyz : graphics.body_b.xyz;
    gl_Position = mvp * vec4(position, 1.0);
    gl_PointSize = 15.0;
    vertex_color = graphics.color;
}
