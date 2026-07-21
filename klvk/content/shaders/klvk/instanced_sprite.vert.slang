// Port of instanced_sprite.vert. One quad per instance, vertices from the vertex
// index, per-instance data from a storage buffer. Bindings and the push-constant
// layout match the GLSL exactly so the C++ pipeline is unchanged.

struct Instance
{
    float2 translation;
    float2 scale;
    uint color;  // packed rgba8
    float rotation_radians;
};

[[vk::binding(1, 0)]] StructuredBuffer<Instance> instances;

struct PushConstants
{
    float4 col0;  // columns of the world-to-view matrix
    float4 col1;
    float4 col2;
};

[[vk::push_constant]] PushConstants pc;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 tex_coord : TEXCOORD0;
};

static const float2 kCorners[6] = {
    float2(-1, -1), float2(1, -1), float2(1, 1),
    float2(-1, -1), float2(1, 1), float2(-1, 1),
};

// GLSL unpackUnorm4x8: byte 0 -> x, byte 3 -> w, each scaled by 1/255.
float4 unpack_unorm4x8(uint packed)
{
    const uint4 bytes = uint4(packed, packed >> 8, packed >> 16, packed >> 24) & 0xffu;
    return float4(bytes) / 255.0;
}

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID, uint instance_index: SV_InstanceID)
{
    const Instance instance = instances[instance_index];

    const float2 unit_corner = kCorners[vertex_index];
    float2 corner = unit_corner * instance.scale;
    const float sine = sin(instance.rotation_radians);
    const float cosine = cos(instance.rotation_radians);
    // mat2(cosine, sine, -sine, cosine) * corner, written out to avoid matrix
    // convention ambiguity: columns are (cosine, sine) and (-sine, cosine).
    corner = float2(cosine * corner.x - sine * corner.y, sine * corner.x + cosine * corner.y);

    const float2 world = instance.translation + corner;
    // world_to_view = mat3(col0.xyz, col1.xyz, col2.xyz); (world_to_view * vec3(world, 1)).xy
    // is exactly this column combination.
    const float2 view_position = pc.col0.xy * world.x + pc.col1.xy * world.y + pc.col2.xy;

    VertexOutput output;
    output.position = float4(view_position, 0.0, 1.0);
    output.color = unpack_unorm4x8(instance.color);
    // Texture coordinates come from the unrotated unit quad so the whole texture
    // always maps onto the sprite regardless of scale and rotation.
    output.tex_coord = unit_corner * 0.5 + 0.5;
    return output;
}
