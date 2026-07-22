// Port of textured_quad.frag: samples the rendered texture (combined image
// sampler at set 0, binding 0).

[[vk::binding(0, 0)]] Sampler2D rendered_texture;

struct FragmentInput
{
    [[vk::location(0)]] float2 texture_coordinates : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    return rendered_texture.Sample(input.texture_coordinates);
}
