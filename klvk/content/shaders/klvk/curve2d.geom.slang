struct GeometryInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float4 join_normals : JOIN_NORMALS;
    [[vk::location(2)]] float4 join_vertices : JOIN_VERTICES;
};

struct GeometryOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

void emit(inout TriangleStream<GeometryOutput> stream, float2 position, float4 color)
{
    GeometryOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.color = color;
    stream.Append(output);
}

[shader("geometry")]
[maxvertexcount(6)]
void main(line GeometryInput input[2], inout TriangleStream<GeometryOutput> stream)
{
    const float2 start_normal = input[0].join_normals.zw;
    const float2 start_v1 = input[0].join_vertices.xy;
    const float2 start_v2 = input[0].join_vertices.zw;
    const float start_turn = input[0].join_normals.x * start_normal.y - input[0].join_normals.y * start_normal.x;
    const float2 start_plus = start_turn < 0.0 ? start_normal : start_v2;
    const float2 start_minus = start_turn < 0.0 ? start_v1 : -start_normal;
    const float2 end_normal = input[1].join_normals.xy;
    const float2 next_normal = input[1].join_normals.zw;
    const float2 v1 = input[1].join_vertices.xy;
    const float2 v2 = input[1].join_vertices.zw;
    const float end_turn = end_normal.x * next_normal.y - end_normal.y * next_normal.x;
    const float2 end_plus = end_turn < 0.0 ? end_normal : v2;
    const float2 end_minus = end_turn < 0.0 ? v1 : -end_normal;
    const float2 next_corner = end_turn < 0.0 ? next_normal : -next_normal;
    if (end_turn < 0.0)
    {
        emit(stream, input[0].position.xy + start_plus, input[0].color);
        emit(stream, input[0].position.xy + start_minus, input[0].color);
        emit(stream, input[1].position.xy + end_plus, input[1].color);
        emit(stream, input[1].position.xy + end_minus, input[1].color);
        emit(stream, input[1].position.xy + v2, input[1].color);
        emit(stream, input[1].position.xy + next_corner, input[1].color);
    }
    else if (end_turn > 0.0)
    {
        emit(stream, input[0].position.xy + start_minus, input[0].color);
        emit(stream, input[0].position.xy + start_plus, input[0].color);
        emit(stream, input[1].position.xy + end_minus, input[1].color);
        emit(stream, input[1].position.xy + end_plus, input[1].color);
        emit(stream, input[1].position.xy + v1, input[1].color);
        emit(stream, input[1].position.xy + next_corner, input[1].color);
    }
    else
    {
        emit(stream, input[0].position.xy + start_minus, input[0].color);
        emit(stream, input[0].position.xy + start_plus, input[0].color);
        emit(stream, input[1].position.xy + end_minus, input[1].color);
        emit(stream, input[1].position.xy + end_plus, input[1].color);
    }
    stream.RestartStrip();
}
