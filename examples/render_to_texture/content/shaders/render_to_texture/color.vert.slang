// Port of color.vert. Fullscreen-style quad from the vertex index; the transform
// is passed as three push-constant columns.

struct PushConstants
{
    float4 transform_col0;
    float4 transform_col1;
    float4 transform_col2;
    float4 color;
};

[[vk::push_constant]] PushConstants pc;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

static const float2 corners[6] = {
    float2(-1, -1), float2(1, -1), float2(1, 1),
    float2(-1, -1), float2(1, 1), float2(-1, 1),
};

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID)
{
    const float2 corner = corners[vertex_index];
    // mat3(col0,col1,col2) * vec3(corner, 1), then .xy - written as a column
    // combination so it is identical regardless of matrix layout.
    const float2 position = pc.transform_col0.xy * corner.x + pc.transform_col1.xy * corner.y + pc.transform_col2.xy;

    VertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.color = pc.color;
    return output;
}
