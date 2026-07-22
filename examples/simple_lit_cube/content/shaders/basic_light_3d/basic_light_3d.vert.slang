// Port of basic_light_3d.vert. The Slang session compiles with column-major
// matrix layout, so ConstantBuffer/push-constant matrices are read exactly as
// the GLSL std140 blocks laid them out, and mul(M, v) equals GLSL's M * v.

struct Scene
{
    float4x4 view_projection;
    float4 view_position;
    float4 light_position;
    float4 light_color;
    float4 object_color;
    float ambient;
    float specular;
};

[[vk::binding(0, 0)]] ConstantBuffer<Scene> scene;

struct Model
{
    float4x4 transform;
};

[[vk::push_constant]] Model model;

struct VertexInput
{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world_normal : NORMAL;
    [[vk::location(1)]] float3 world_position : POSITION;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    const float4 position_world = mul(model.transform, float4(input.position, 1.0));

    VertexOutput output;
    output.position = mul(scene.view_projection, position_world);
    output.world_position = position_world.xyz;
    output.world_normal = normalize(mul((float3x3)model.transform, input.normal));
    return output;
}
