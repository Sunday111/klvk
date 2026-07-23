// Port of scene.frag: a rotating rounded box in a rainbow color.

struct Settings
{
    float4 data;
};

[[vk::push_constant]] Settings settings;

struct FragmentInput
{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    float2 p = input.uv * 2.0 - 1.0;
    const float c = cos(settings.data.x);
    const float s = sin(settings.data.x);
    // mat2(c, -s, s, c) * p: columns (c, -s) and (s, c).
    p = float2(c * p.x + s * p.y, -s * p.x + c * p.y);
    const float box = 1.0 - smoothstep(0.42, 0.44, max(abs(p.x), abs(p.y)));
    const float3 rainbow = 0.5 + 0.5 * cos(settings.data.x + float3(0.0, 2.1, 4.2));
    return float4(rainbow * box, 1.0);
}
