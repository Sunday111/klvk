// A quad over the star's bounds. The stencil test decides which of its fragments
// survive, so the quad only has to be large enough to contain the shape.

struct PushConstants
{
    float2 translation;
    float scale;
    float padding;
    float4 color;
};

[[vk::push_constant]] PushConstants pc;

static const float2 kCorners[6] = {
    float2(-1, -1),
    float2(1, -1),
    float2(1, 1),
    float2(-1, -1),
    float2(1, 1),
    float2(-1, 1),
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID)
{
    VertexOutput output;
    output.position = float4(pc.translation + (kCorners[vertex_index] * pc.scale), 0.0, 1.0);
    output.color = pc.color;
    return output;
}
