// Port of counting_draw.frag: colors each pixel by the visit count the compute
// pass accumulated for it.

[[vk::constant_id(0)]] const int MAX_ITERATIONS = 200;

[[vk::binding(0, 0)]] StructuredBuffer<uint> visit_counts;
// MAX_ITERATIONS + 1 entries.
[[vk::binding(1, 0)]] StructuredBuffer<float4> u_color_table;

struct PushConstants
{
    float2 u_resolution;
};

[[vk::push_constant]] PushConstants pc;

[shader("fragment")]
float4 main(float4 frag_coord: SV_Position): SV_Target
{
    const int2 pixel = int2(frag_coord.xy);
    const int index = pixel.y * int(pc.u_resolution.x) + pixel.x;
    const uint count = visit_counts[index];
    return float4(u_color_table[count].rgb, 1);
}
