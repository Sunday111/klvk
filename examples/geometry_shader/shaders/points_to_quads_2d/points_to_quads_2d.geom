#version 450

// Input from the vertex shader (one point)
layout(points) in;

// Output to the fragment shader (a quad made of 2 triangles, or a single triangle)
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec4 vs_col0[1];
layout(location = 1) in vec4 vs_col1[1];
layout(location = 2) in vec4 vs_col2[1];
layout(location = 3) in vec4 vs_color[1];
layout(location = 4) in uint vs_type[1];

layout(location = 0) out vec4 gs_color;
layout(location = 1) flat out uint gs_type;
layout(location = 2) out vec2 gs_tex_coord;

void main()
{
    mat3 transform = mat3(vs_col0[0].xyz, vs_col1[0].xyz, vs_col2[0].xyz);

    // Create the quad by emitting four vertices in a triangle strip
    // Bottom-left
    gs_color = vs_color[0];
    gs_type = vs_type[0];
    gl_Position = vec4(transform * vec3(-1, -1, 1), 1.0);
    gs_tex_coord = vec2(0, 0);
    EmitVertex();

    // Bottom-right
    gs_color = vs_color[0];
    gs_type = vs_type[0];
    gl_Position = vec4(transform * vec3(1, -1, 1), 1.0);
    gs_tex_coord = vec2(1, 0);
    EmitVertex();

    // Top-left
    gs_color = vs_color[0];
    gs_type = vs_type[0];
    gl_Position = vec4(transform * vec3(-1, 1, 1), 1.0);
    gs_tex_coord = vec2(0, 1);
    EmitVertex();

    if (vs_type[0] != 2u)
    {
        // Top-right
        gs_color = vs_color[0];
        gs_type = vs_type[0];
        gl_Position = vec4(transform * vec3(1, 1, 1), 1.0);
        gs_tex_coord = vec2(1, 1);
        EmitVertex();
    }

    // End primitive to complete the quad
    EndPrimitive();
}
