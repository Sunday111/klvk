struct PushConstants
{
    float4 transform_x;
    float4 transform_y;
    float2 viewport_size;
    float thickness;
    float segment_pixel_length;
};

[[vk::push_constant]] PushConstants pc;

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
    output.position = float4(
        dot(pc.transform_x.xyz, float3(input.position, 1.0)),
        dot(pc.transform_y.xyz, float3(input.position, 1.0)),
        0.0,
        1.0);
    output.color = input.color;
    return output;
}
