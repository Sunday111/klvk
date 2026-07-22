// Port of basic_light_3d.frag: Blinn-style diffuse + specular. Reads the same
// Scene constant buffer as the vertex stage (set 0, binding 0).

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

struct FragmentInput
{
    [[vk::location(0)]] float3 world_normal : NORMAL;
    [[vk::location(1)]] float3 world_position : POSITION;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    const float3 normal = normalize(input.world_normal);
    const float3 light_direction = normalize(scene.light_position.xyz - input.world_position);
    const float diffuse_strength = max(dot(normal, light_direction), 0.0);

    const float3 view_direction = normalize(scene.view_position.xyz - input.world_position);
    const float3 reflection_direction = reflect(-light_direction, normal);
    const float highlight = pow(max(dot(view_direction, reflection_direction), 0.0), 64.0);

    float3 lighting = (scene.ambient + diffuse_strength) * scene.light_color.xyz;
    lighting += scene.specular * highlight * scene.light_color.xyz;
    return float4(lighting, 1.0) * scene.object_color;
}
