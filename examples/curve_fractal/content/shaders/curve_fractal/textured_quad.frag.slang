// Port of curve_fractal/textured_quad.frag: samples the rendered texture.

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
