// Port of two_textures_2d.frag: modulates the vertex color by two textures' red.

[[vk::binding(0, 0)]] Sampler2D u_texture_a;
[[vk::binding(1, 0)]] Sampler2D u_texture_b;

struct FragmentInput
{
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 tex_coord : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    return input.color * u_texture_a.Sample(input.tex_coord).r * u_texture_b.Sample(input.tex_coord).r;
}
