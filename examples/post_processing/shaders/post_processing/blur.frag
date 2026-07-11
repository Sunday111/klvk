#version 450
layout(set = 0, binding = 0) uniform sampler2D scene_texture;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(push_constant) uniform Settings { vec4 data; } settings;
void main()
{
    vec2 texel = 1.0 / vec2(textureSize(scene_texture, 0));
    vec3 sum = vec3(0.0);
    float total = 0.0;
    int radius = int(settings.data.x);
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            float weight = exp(-float(x*x + y*y) / max(settings.data.y, 0.1));
            sum += texture(scene_texture, uv + vec2(x, y) * texel).rgb * weight;
            total += weight;
        }
    }
    vec3 original = texture(scene_texture, uv).rgb;
    color = vec4(mix(original, sum / total, settings.data.z), 1.0);
}
