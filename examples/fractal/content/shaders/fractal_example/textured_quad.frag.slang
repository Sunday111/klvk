// Port of fractal_example/textured_quad.frag: samples a texture, forces alpha 1.

[[vk::binding(0, 0)]] Sampler2D u_texture;

struct FragmentInput
{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    return float4(u_texture.Sample(input.uv).rgb, 1);
}
