// Port of interpolation_widget.frag: draws the color table as a horizontal strip.

[[vk::constant_id(0)]] const int COLORS_COUNT = 201;

[[vk::binding(0, 0)]] StructuredBuffer<float4> u_color_table;

struct FragmentInput
{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    const int index = int(clamp(input.uv.x * (COLORS_COUNT - 1), 0, COLORS_COUNT - 1));
    return float4(u_color_table[index].rgb, 1);
}
