// Port of points_to_quads_2d.vert. One point per object, object data pulled from
// a storage buffer by the vertex index. Writes no position - the geometry stage
// expands each point into a shape and computes clip positions.

struct Object
{
    float4 col0;  // columns of the transform matrix
    float4 col1;
    float4 col2;
    uint color;  // packed rgba8
    uint type;
    uint padding0;
    uint padding1;
};

[[vk::binding(0, 0)]] StructuredBuffer<Object> objects;

struct VertexOutput
{
    [[vk::location(0)]] float4 col0 : COL0;
    [[vk::location(1)]] float4 col1 : COL1;
    [[vk::location(2)]] float4 col2 : COL2;
    [[vk::location(3)]] float4 color : COLOR0;
    [[vk::location(4)]] uint type : TYPE;
};

float4 unpack_unorm4x8(uint packed)
{
    const uint4 bytes = uint4(packed, packed >> 8, packed >> 16, packed >> 24) & 0xffu;
    return float4(bytes) / 255.0;
}

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID)
{
    const Object object = objects[vertex_index];
    VertexOutput output;
    output.col0 = object.col0;
    output.col1 = object.col1;
    output.col2 = object.col2;
    output.color = unpack_unorm4x8(object.color);
    output.type = object.type;
    return output;
}
