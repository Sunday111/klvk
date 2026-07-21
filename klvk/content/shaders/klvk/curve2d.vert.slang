// Port of curve2d.vert. Vertex attributes come from a vertex buffer; locations
// match the C++ vertex input layout exactly.

struct VertexInput
{
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float4 color;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}
