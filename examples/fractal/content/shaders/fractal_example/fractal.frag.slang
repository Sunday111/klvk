// Port of fractal.frag. Specialization constants (constant_id 0/1/2) match the
// order in fractal.shader.json; the push-constant layout matches the GLSL.

[[vk::constant_id(0)]] const int MAX_ITERATIONS = 200;
[[vk::constant_id(1)]] const int INSIDE_OUT_SPACE = 1;
[[vk::constant_id(2)]] const int COLOR_MODE = 0;

// MAX_ITERATIONS + 1 entries, refreshed by ApplySettings.
[[vk::binding(0, 0)]] StructuredBuffer<float4> u_color_table;

struct PushConstants
{
    float4 s2w_col0;  // columns of the screen-to-world matrix
    float4 s2w_col1;
    float4 s2w_col2;
    float2 u_resolution;
    float2 u_julia_constant;
    float2 u_fractal_power;
};

[[vk::push_constant]] PushConstants pc;

float2 inside_out_warp(float2 pos, float2 center, float strength)
{
    const float2 dir = pos - center;
    const float dist = length(dir);
    if (dist < 0.001) return pos;
    const float inverted_dist = strength / dist;
    return center + normalize(dir) * inverted_dist;
}

float2 screen_point_to_world(float2 screen)
{
    // mat3(s2w_col0.xyz, s2w_col1.xyz, s2w_col2.xyz) * vec3(screen, 1), then .xy.
    return pc.s2w_col0.xy * screen.x + pc.s2w_col1.xy * screen.y + pc.s2w_col2.xy;
}

float2 complex_mult(float2 a, float2 b)
{
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float2 complex_exp(float2 z)
{
    return exp(z.x) * float2(cos(z.y), sin(z.y));
}

float2 complex_power(float2 base, float2 power)
{
    const float r = length(base);
    const float theta = atan2(base.x, base.y);  // GLSL atan(y, x)
    const float2 log_z = float2(log(r), theta);
    const float2 exponent = complex_mult(power, log_z);
    return complex_exp(exponent);
}

float3 get_color(float smooth_iteration)
{
    const float index = clamp(smooth_iteration, 0.0, float(MAX_ITERATIONS));
    const int lower = int(floor(index));
    const int upper = min(lower + 1, MAX_ITERATIONS);
    const float t = index - float(lower);
    const float3 color_lower = u_color_table[lower].rgb;
    const float3 color_upper = u_color_table[upper].rgb;
    return lerp(color_lower, color_upper, t);
}

[shader("fragment")]
float4 main(float4 frag_coord: SV_Position): SV_Target
{
    const float2 c = pc.u_julia_constant;

    // gl_FragCoord is y-down in Vulkan; the transforms are built for y-up screens.
    const float2 screen = float2(frag_coord.x, pc.u_resolution.y - frag_coord.y);
    const float2 world = screen_point_to_world(screen);

    float2 z = world;
    if (INSIDE_OUT_SPACE != 0)
    {
        const float2 world_center = screen_point_to_world(pc.u_resolution / 2);
        z = inside_out_warp(world, world_center, 1);
    }

    int i = 0;
    while (i != MAX_ITERATIONS)
    {
        const float2 p = complex_power(z, pc.u_fractal_power) + c;
        if (dot(p, p) > 4) break;
        z = p;
        ++i;
    }

    if (COLOR_MODE == 1)
    {
        const float pp = MAX_ITERATIONS * dot(normalize(world), float2(1, 0));
        return float4(u_color_table[(int(pp) + i) % MAX_ITERATIONS].rgb, 1.0);
    }
    if (COLOR_MODE == 2)
    {
        return float4(u_color_table[(int(MAX_ITERATIONS * (world.x + world.y)) + i) % MAX_ITERATIONS].rgb, 1.0);
    }
    if (COLOR_MODE == 3)
    {
        const float smooth_iteration = float(i) - log2(max(1.0f, log2(sqrt(length(z)))));
        return float4(get_color(smooth_iteration), 1.0);
    }
    if (COLOR_MODE == 4)
    {
        const float smooth_iteration = float(i) - log2(log2(length(z)));
        return float4(get_color(smooth_iteration), 1.0);
    }
    return float4(u_color_table[i].rgb, 1.0);
}
