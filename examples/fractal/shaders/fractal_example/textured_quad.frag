#version 450

layout(set = 0, binding = 0) uniform sampler2D u_texture;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = vec4(texture(u_texture, uv).rgb, 1);
}
