// Port of points_to_quads_2d.geom. Expands each point into a triangle strip -
// a quad, or a single triangle for type 2.

struct GeometryInput
{
    [[vk::location(0)]] float4 col0 : COL0;
    [[vk::location(1)]] float4 col1 : COL1;
    [[vk::location(2)]] float4 col2 : COL2;
    [[vk::location(3)]] float4 color : COLOR0;
    [[vk::location(4)]] uint type : TYPE;
};

struct GeometryOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] nointerpolation uint type : TYPE;
    [[vk::location(2)]] float2 tex_coord : TEXCOORD0;
};

// mat3(col0.xyz, col1.xyz, col2.xyz) * vec3(corner, 1), as a column combination.
float4 project(GeometryInput o, float2 corner)
{
    const float3 p = o.col0.xyz * corner.x + o.col1.xyz * corner.y + o.col2.xyz;
    return float4(p, 1.0);
}

[shader("geometry")]
[maxvertexcount(4)]
void main(point GeometryInput input[1], inout TriangleStream<GeometryOutput> output)
{
    const GeometryInput o = input[0];

    GeometryOutput vertex;
    vertex.color = o.color;
    vertex.type = o.type;

    // Bottom-left
    vertex.position = project(o, float2(-1, -1));
    vertex.tex_coord = float2(0, 0);
    output.Append(vertex);

    // Bottom-right
    vertex.position = project(o, float2(1, -1));
    vertex.tex_coord = float2(1, 0);
    output.Append(vertex);

    // Top-left
    vertex.position = project(o, float2(-1, 1));
    vertex.tex_coord = float2(0, 1);
    output.Append(vertex);

    if (o.type != 2u)
    {
        // Top-right
        vertex.position = project(o, float2(1, 1));
        vertex.tex_coord = float2(1, 1);
        output.Append(vertex);
    }

    output.RestartStrip();
}
