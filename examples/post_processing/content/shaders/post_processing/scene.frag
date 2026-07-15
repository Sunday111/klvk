#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(push_constant) uniform Settings { vec4 data; } settings;
void main()
{
    vec2 p = uv * 2.0 - 1.0;
    float c = cos(settings.data.x), s = sin(settings.data.x);
    p = mat2(c, -s, s, c) * p;
    float box = 1.0 - smoothstep(0.42, 0.44, max(abs(p.x), abs(p.y)));
    vec3 rainbow = 0.5 + 0.5 * cos(settings.data.x + vec3(0.0, 2.1, 4.2));
    color = vec4(rainbow * box, 1.0);
}
