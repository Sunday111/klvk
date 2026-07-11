#version 450

layout(set = 0, binding = 0) uniform sampler2D u_texture_a;
layout(set = 0, binding = 1) uniform sampler2D u_texture_b;

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_tex_coord;

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = in_color * texture(u_texture_a, in_tex_coord).r * texture(u_texture_b, in_tex_coord).r;
}
