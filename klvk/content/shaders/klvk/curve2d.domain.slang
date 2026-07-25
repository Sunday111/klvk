struct PushConstants
{
    float4 transform_x;
    float4 transform_y;
    float2 viewport_size;
    float thickness;
    float segment_pixel_length;
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
    [[vk::location(1)]] float4 join_normals : JOIN_NORMALS;
    [[vk::location(2)]] float4 join_vertices : JOIN_VERTICES;
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

float segment_count(float4 p1, float4 p2)
{
    const float2 screen1 = (p1.xy / p1.w * 0.5 + 0.5) * pc.viewport_size;
    const float2 screen2 = (p2.xy / p2.w * 0.5 + 0.5) * pc.viewport_size;
    const float requested = clamp(length(screen2 - screen1) / pc.segment_pixel_length, 1.0, 64.0);
    return 2.0 * ceil(requested * 0.5);
}

void calculate_join(float2 previous, float2 current, float2 next, out float4 normals, out float4 vertices)
{
    const float2 screen_scale = pc.viewport_size * 0.5;
    const float2 previous_delta = (current - previous) * screen_scale;
    const float2 current_delta = (next - current) * screen_scale;
    const float previous_length_sq = dot(previous_delta, previous_delta);
    const float current_length_sq = dot(current_delta, current_delta);
    const float2 outgoing = current_length_sq < 1e-6
        ? (previous_length_sq < 1e-6 ? float2(1.0, 0.0) : normalize(previous_delta))
        : normalize(current_delta);
    const float2 incoming = previous_length_sq < 1e-6 ? outgoing : normalize(previous_delta);
    const float half_width = pc.thickness * 0.5;
    const float2 k1 = float2(-incoming.y, incoming.x);
    const float2 k2 = float2(-outgoing.y, outgoing.x);
    const float2 n1 = half_width * k1;
    const float2 n2 = half_width * k2;
    const float2 sum = k1 + k2;
    const float sum_sq = dot(sum, sum);
    const float denominator = dot(sum, k1);
    float2 miter;
    float2 bisector;
    if (sum_sq < 1e-6 || abs(denominator) < 1e-3)
    {
        miter = n1;
        bisector = n1;
    }
    else
    {
        miter = half_width * sum / denominator;
        bisector = half_width * sum * rsqrt(sum_sq);
        const float maximum = half_width * 4.0;
        const float miter_sq = dot(miter, miter);
        if (miter_sq > maximum * maximum) miter *= maximum * rsqrt(miter_sq);
    }
    const float turn = incoming.x * outgoing.y - incoming.y * outgoing.x;
    const float2 v1 = turn < 0.0 ? -miter : -bisector;
    const float2 v2 = turn < 0.0 ? bisector : miter;
    const float2 clip_scale = 2.0 / pc.viewport_size;
    normals = float4(n1, n2) * float4(clip_scale, clip_scale);
    vertices = float4(v1, v2) * float4(clip_scale, clip_scale);
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
    // Slang's standalone hull SPIR-V currently gives user patch constants
    // colliding locations. Recompute these three cheap values from the shared
    // patch; the hull uses this same function for the central tessellation factor.
    const float previous_segments = segment_count(patch[1].position, patch[2].position);
    const float segments = segment_count(patch[2].position, patch[3].position);
    const float next_segments = segment_count(patch[3].position, patch[4].position);
    DomainOutput output;
    output.color = catmull_rom(patch[1].color, patch[2].color, patch[3].color, patch[4].color, t);
    const float2 current = catmull_rom(
        patch[1].position.xy, patch[2].position.xy, patch[3].position.xy, patch[4].position.xy, t);
    output.position = float4(current, 0.0, 1.0);
    const float2 previous = t <= 0.0
        ? catmull_rom(patch[0].position.xy, patch[1].position.xy, patch[2].position.xy, patch[3].position.xy,
            1.0 - 1.0 / previous_segments)
        : catmull_rom(patch[1].position.xy, patch[2].position.xy, patch[3].position.xy, patch[4].position.xy,
            max(t - 1.0 / segments, 0.0));
    const float2 next = t >= 1.0
        ? catmull_rom(patch[2].position.xy, patch[3].position.xy, patch[4].position.xy, patch[5].position.xy,
            1.0 / next_segments)
        : catmull_rom(patch[1].position.xy, patch[2].position.xy, patch[3].position.xy, patch[4].position.xy,
            min(t + 1.0 / segments, 1.0));
    calculate_join(previous, current, next, output.join_normals, output.join_vertices);
    return output;
}
