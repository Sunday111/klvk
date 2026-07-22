// Port of points_to_quads_2d.frag. Draws each shape with an inverted-color
// border; the border width is a push constant so the slider needs no rebuild.

struct PushConstants
{
    float figure_border;
};

[[vk::push_constant]] PushConstants pc;

struct FragmentInput
{
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] nointerpolation uint type : TYPE;
    [[vk::location(2)]] float2 tex_coord : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    float4 color = float4(0, 0, 0, 0);

    const float border_left = pc.figure_border;
    const float border_right = 1.0 - border_left;

    bool is_border = input.tex_coord.x < border_left || input.tex_coord.x > border_right ||
                     input.tex_coord.y < border_left || input.tex_coord.y > border_right;

    switch (input.type)
    {
    case 0u:
        color = input.color;
        break;

    case 1u:
    {
        const float2 v = input.tex_coord * 2 - 1;
        const float dist_sq = dot(v, v);
        const bool in_circle = dist_sq <= 1.0;
        is_border = in_circle && dist_sq >= border_right * border_right;
        color = in_circle ? input.color : float4(0, 0, 0, 0);
        break;
    }

    case 2u:
        color = input.color;
        break;

    default:
        break;
    }

    color.xyz = is_border ? 1 - color.xyz : color.xyz;
    return color;
}
