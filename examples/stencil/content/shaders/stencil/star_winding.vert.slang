// Fans the star outline from its first vertex. The fan sweeps the exterior as
// well as the interior, which is exactly what a winding stencil counts: the
// pass writes no color and only accumulates coverage.

struct PushConstants
{
    float2 translation;
    float scale;
    float padding;
    float4 color;
};

[[vk::push_constant]] PushConstants pc;

static const uint kPointCount = 5;

// Every second vertex of a regular pentagon, so consecutive edges cross and the
// core of the star is wound twice.
static float2 StarPoint(uint index)
{
    const float angle = 1.57079633 + (float(index) * 2.51327412);
    return float2(cos(angle), sin(angle));
}

[shader("vertex")]
float4 main(uint vertex_index: SV_VertexID) : SV_Position
{
    const uint triangle_index = vertex_index / 3;
    const uint corner = vertex_index % 3;
    const uint point_index = corner == 0 ? 0 : (triangle_index + corner);
    const float2 position = pc.translation + (StarPoint(point_index % kPointCount) * pc.scale);
    return float4(position, 0.0, 1.0);
}
