#version 450

layout(constant_id = 0) const int MAX_ITERATIONS = 200;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

layout(std430, set = 0, binding = 0) readonly buffer PixelBuffer {
    uint visitCounts[];
};

// MAX_ITERATIONS + 1 entries.
layout(std430, set = 0, binding = 1) readonly buffer ColorTable {
    vec4 u_color_table[];
};

layout(push_constant) uniform PushConstants
{
    vec2 u_resolution;
} pc;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    int index = pixel.y * int(pc.u_resolution.x) + pixel.x;
    uint count = visitCounts[index];
    FragColor = vec4(u_color_table[count].rgb, 1);
}
