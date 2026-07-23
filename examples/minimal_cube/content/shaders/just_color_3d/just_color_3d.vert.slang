// Port of just_color_3d.vert. The transform arrives as four push-constant
// columns; transform * vec4(position, 1) is written as their combination so it
// is identical regardless of matrix layout.

struct PushConstants
{
    float4 transform_col0;
    float4 transform_col1;
    float4 transform_col2;
    float4 transform_col3;
    float4 color;
};

[[vk::push_constant]] PushConstants pc;

struct VertexInput
{
    [[vk::location(0)]] float3 position;
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
    output.position = pc.transform_col0 * input.position.x + pc.transform_col1 * input.position.y +
                      pc.transform_col2 * input.position.z + pc.transform_col3;
    output.color = pc.color;
    return output;
}
