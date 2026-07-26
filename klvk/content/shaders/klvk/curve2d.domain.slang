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

struct DomainOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

float4 catmull_rom(float4 p0, float4 p1, float4 p2, float4 p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5 * (2.0 * p1 + (-p0 + p2) * t
        + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
        + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

float2 catmull_rom(float2 p0, float2 p1, float2 p2, float2 p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5 * (2.0 * p1 + (-p0 + p2) * t
        + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
        + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

[shader("domain")]
[domain("isoline")]
[partitioning("fractional_even")]
DomainOutput main(
    PatchConstants constants,
    float2 tess_coord : SV_DomainLocation,
    const OutputPatch<ControlPoint, 6> patch)
{
    const float t = tess_coord.x;
    DomainOutput output;
    output.color = catmull_rom(patch[1].color, patch[2].color, patch[3].color, patch[4].color, t);
    const float2 current = catmull_rom(
        patch[1].position.xy, patch[2].position.xy, patch[3].position.xy, patch[4].position.xy, t);
    output.position = float4(current, 0.0, 1.0);
    return output;
}
