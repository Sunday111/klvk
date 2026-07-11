#version 450

// klgl compiled the border width in as a shader define and recompiled on change;
// here it is a push constant, so the slider needs no pipeline rebuild.
layout(push_constant) uniform PushConstants
{
    layout(offset = 0) float figure_border;
} pc;

layout(location = 0) in vec4 gs_color;
layout(location = 1) flat in uint gs_type;
layout(location = 2) in vec2 gs_tex_coord;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 color = vec4(0);

    float border_left = pc.figure_border;
    float border_right = 1.f - border_left;

    bool is_border = gs_tex_coord.x < border_left || gs_tex_coord.x > border_right || gs_tex_coord.y < border_left ||
                     gs_tex_coord.y > border_right;

    switch (gs_type)
    {
    case 0u:
        color = gs_color;
        break;

    case 1u:
        vec2 v = gs_tex_coord * 2 - 1;
        float dist_sq = dot(v, v);
        bool in_circle = dist_sq <= 1.f;
        is_border = in_circle && dist_sq >= border_right * border_right;
        color = in_circle ? gs_color : vec4(0);
        break;

    case 2u:
        color = gs_color;
        break;

    default:
        break;
    }

    color.xyz = is_border ? 1 - color.xyz : color.xyz;
    out_color = color;
}
