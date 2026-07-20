// Port of just_color_2d.frag: passes the interpolated vertex color straight out.

struct FragmentInput
{
    [[vk::location(0)]] float4 color : COLOR0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    return input.color;
}
