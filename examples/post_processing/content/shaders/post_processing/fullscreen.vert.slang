// Port of post_processing/fullscreen.vert: fullscreen quad from the vertex index.

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

static const float2 corners[6] = {
    float2(-1, -1), float2(1, -1), float2(1, 1),
    float2(-1, -1), float2(1, 1), float2(-1, 1),
};

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID)
{
    const float2 p = corners[vertex_index];
    VertexOutput output;
    output.position = float4(p, 0, 1);
    output.uv = p * 0.5 + 0.5;
    return output;
}
