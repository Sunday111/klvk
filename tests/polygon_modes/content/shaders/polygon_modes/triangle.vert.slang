[shader("vertex")]
float4 main(uint vertex_index : SV_VertexID) : SV_Position
{
    const float2 positions[3] = {float2(-0.5, -0.5), float2(0.5, -0.5), float2(0, 0.5)};
    return float4(positions[vertex_index], 0, 1);
}
