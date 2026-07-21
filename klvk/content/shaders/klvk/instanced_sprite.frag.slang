// Port of instanced_sprite.frag. u_texture is a combined image sampler at
// set 0, binding 0 - matching the GLSL sampler2D descriptor exactly.

[[vk::binding(0, 0)]] Sampler2D u_texture;

struct FragmentInput
{
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 tex_coord : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    return input.color * u_texture.Sample(input.tex_coord).r;
}
