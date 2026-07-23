// Port of just_color_3d.frag: passes the interpolated color through.

struct FragmentInput
{
    [[vk::location(0)]] float4 color : COLOR0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    return input.color;
}
