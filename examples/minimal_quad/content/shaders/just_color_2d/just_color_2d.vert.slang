// Port of just_color_2d.vert. The quad is generated from the vertex index - no
// vertex buffers. Bindings match the GLSL exactly so the C++ pipeline is unchanged.

struct PushConstants
{
    float4 col0;  // columns of the transform matrix
    float4 col1;
    float4 col2;
    float4 color;
};

[[vk::push_constant]] PushConstants pc;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

static const float2 kCorners[6] = {
    float2(-1, -1), float2(1, -1), float2(1, 1),
    float2(-1, -1), float2(1, 1), float2(-1, 1),
};

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID)
{
    const float2 corner = kCorners[vertex_index];
    // Written as an explicit column combination rather than a matrix multiply, so
    // the result is identical to the GLSL regardless of matrix-layout conventions:
    // GLSL's mat3(col0,col1,col2) * vec3(corner,1) is exactly this sum.
    const float3 transformed = pc.col0.xyz * corner.x + pc.col1.xyz * corner.y + pc.col2.xyz;

    VertexOutput output;
    output.position = float4(transformed.xy, 0.0, 1.0);
    output.color = pc.color;
    return output;
}
