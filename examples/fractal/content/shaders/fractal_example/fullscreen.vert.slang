// Port of fullscreen.vert. Fullscreen quad from the vertex index; note z = 1.

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
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
    output.position = float4(corner, 1, 1.0);
    output.uv = (corner + 1) / 2;
    return output;
}
