struct PushConstants
{
    float4 transform_x;
    float4 transform_y;
    float2 viewport_size;
    float thickness;
    float segment_pixel_length;
    float antialias;
};

[[vk::push_constant]] PushConstants pc;

struct ControlPoint
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

struct PatchConstants
{
    float tess_factors[2] : SV_TessFactor;
};

float segment_count(float4 p1, float4 p2)
{
    const float2 screen1 = (p1.xy / p1.w * 0.5 + 0.5) * pc.viewport_size;
    const float2 screen2 = (p2.xy / p2.w * 0.5 + 0.5) * pc.viewport_size;
    const float requested = clamp(length(screen2 - screen1) / pc.segment_pixel_length, 1.0, 64.0);
    return 2.0 * ceil(requested * 0.5);
}

PatchConstants patch_constants(InputPatch<ControlPoint, 6> patch)
{
    PatchConstants output;
    const float segments = segment_count(patch[2].position, patch[3].position);
    output.tess_factors[0] = 1.0;
    output.tess_factors[1] = segments;
    return output;
}

[shader("hull")]
[domain("isoline")]
[partitioning("fractional_even")]
[outputtopology("line")]
[outputcontrolpoints(6)]
[patchconstantfunc("patch_constants")]
ControlPoint main(InputPatch<ControlPoint, 6> patch, uint invocation_id : SV_OutputControlPointID)
{
    return patch[invocation_id];
}
