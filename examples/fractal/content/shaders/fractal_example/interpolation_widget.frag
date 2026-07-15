#version 450

layout(constant_id = 0) const int COLORS_COUNT = 201;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

layout(std430, set = 0, binding = 0) readonly buffer ColorTable
{
    vec4 uColorTable[];
};

void main()
{
    FragColor = vec4(uColorTable[int(clamp(uv.x * (COLORS_COUNT - 1), 0, COLORS_COUNT - 1))].rgb, 1);
}
