// Port of bodies.vert: draws the two attracting bodies as large points.

struct Graphics
{
    float4x4 mvp;
    float4 color;
    float4 body_a;
    float4 body_b;
};

[[vk::push_constant]] Graphics graphics;

struct VertexOutput
{
    float4 position : SV_Position;
    float point_size : SV_PointSize;
    [[vk::location(0)]] float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID)
{
    const float3 position = vertex_index == 0 ? graphics.body_a.xyz : graphics.body_b.xyz;
    VertexOutput output;
    output.position = mul(graphics.mvp, float4(position, 1.0));
    output.point_size = 15.0;
    output.color = graphics.color;
    return output;
}
