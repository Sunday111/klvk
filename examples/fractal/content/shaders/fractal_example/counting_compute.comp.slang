// Port of counting_compute.comp. Accumulates a per-pixel Julia iteration count
// into a storage buffer with an atomic add.

[[vk::constant_id(0)]] const int MAX_ITERATIONS = 200;
[[vk::constant_id(1)]] const int INSIDE_OUT_SPACE = 1;

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> visit_counts;

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
    return pc.s2w_col0.xy * screen.x + pc.s2w_col1.xy * screen.y + pc.s2w_col2.xy;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 thread_id: SV_DispatchThreadID)
{
    const int2 start_pixel = int2(thread_id.xy);
    if (start_pixel.x >= pc.u_resolution.x || start_pixel.y >= pc.u_resolution.y) return;

    const float2 c = pc.u_julia_constant;

    // The counter buffer is indexed with the y-down pixel (like gl_FragCoord in the
    // draw shader), so the world position comes from the y-flipped coordinate.
    const float2 frag_coord = float2(
        float(start_pixel.x) + 0.5f,
        pc.u_resolution.y - (float(start_pixel.y) + 0.5f));
    const float2 world = screen_point_to_world(frag_coord);

    float2 z = world;
    if (INSIDE_OUT_SPACE != 0)
    {
        const float2 world_center = screen_point_to_world(pc.u_resolution / 2);
        z = inside_out_warp(world, world_center, 1);
    }

    uint i = 0;
    for (; i != uint(MAX_ITERATIONS); ++i)
    {
        const float2 p = c + float2(z.x * z.x - z.y * z.y, 2 * z.x * z.y);
        z = p;
        if (dot(p, p) > 4) break;
    }

    const int index = int(start_pixel.y * pc.u_resolution.x + start_pixel.x);
    InterlockedAdd(visit_counts[index], i);
}
