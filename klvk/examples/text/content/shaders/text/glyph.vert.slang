// One quad per glyph, from a vertex buffer the application fills each frame.

struct PushConstants
{
    float2 inverse_half_extent;  // pixels to clip space
    float2 padding;
    float4 color;
};

[[vk::push_constant]] PushConstants pc;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;  // pixels, y down from the top left
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    VertexOutput output;
    // The viewport is y-up, so the y term is negated on the way to clip space.
    const float2 clip = (input.position * pc.inverse_half_extent) - 1.0;
    output.position = float4(clip.x, -clip.y, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}
