// Port: quad from the vertex index, transformed by push-constant scale/translation.

struct PushConstants
{
    float4 color;
    float2 scale;
    float2 translation;
};

[[vk::push_constant]] PushConstants pc;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 tex_coord : TEXCOORD0;
};

static const float2 kCorners[6] = {
    float2(-1, -1), float2(1, -1), float2(1, 1),
    float2(-1, -1), float2(1, 1), float2(-1, 1),
};

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID)
{
    const float2 corner = kCorners[vertex_index];
    VertexOutput output;
    output.position = float4(corner * pc.scale + pc.translation, 0.0, 1.0);
    output.color = pc.color;
    output.tex_coord = corner * 0.5 + 0.5;
    return output;
}
