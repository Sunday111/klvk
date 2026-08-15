// The atlas holds coverage, not colour: the red channel says how much of the
// pixel the glyph covers and the colour comes from the push constant.

struct PushConstants
{
    float2 inverse_half_extent;
    float2 padding;
    float4 color;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] Sampler2D atlas;

struct FragmentInput
{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target
{
    const float coverage = atlas.Sample(input.uv).r;
    return float4(pc.color.rgb, pc.color.a * coverage);
}
