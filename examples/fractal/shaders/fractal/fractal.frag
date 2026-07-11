#version 450

layout(location = 0) in vec2 coordinate;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Settings
{
    vec4 view;   // center.xy, scale, aspect
    vec4 julia;  // constant.xy, iteration count, palette phase
} settings;

vec3 palette(float value)
{
    vec3 phase = vec3(0.0, 0.33, 0.67) + settings.julia.w;
    return 0.5 + 0.5 * cos(6.2831853 * (value + phase));
}

void main()
{
    vec2 c = settings.view.xy + coordinate * settings.view.z * vec2(settings.view.w, 1.0);
    vec2 z = c;
    // A non-zero Julia constant switches the same renderer to Julia mode.
    if (dot(settings.julia.xy, settings.julia.xy) > 0.000001)
    {
        z = c;
        c = settings.julia.xy;
    }

    int limit = int(settings.julia.z);
    int iteration = 0;
    for (; iteration != limit; ++iteration)
    {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (dot(z, z) > 256.0) break;
    }
    if (iteration == limit)
    {
        out_color = vec4(0.005, 0.005, 0.01, 1.0);
        return;
    }

    float smooth_iteration = float(iteration) + 1.0 - log2(log2(length(z)));
    out_color = vec4(palette(smooth_iteration * 0.035), 1.0);
}
