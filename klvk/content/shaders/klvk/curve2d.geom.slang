struct PushConstants
{
    float4 transform_x;
    float4 transform_y;
    float2 viewport_size;
    float thickness;
    float segment_pixel_length;
    float antialias;
};

[[vk::push_constant]] PushConstants pc;

struct GeometryInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

struct GeometryOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] nointerpolation float4 seg : SEG;
    [[vk::location(2)]] nointerpolation float half_width : HALF_WIDTH;
    [[vk::location(3)]] nointerpolation float aa : AA;
};

[shader("geometry")]
[maxvertexcount(4)]
void main(line GeometryInput input[2], inout TriangleStream<GeometryOutput> stream)
{
    const float2 vp = pc.viewport_size;
    const float2 a = float2(
        input[0].position.x * 0.5 + 0.5,
        0.5 - 0.5 * input[0].position.y) * vp;
    const float2 b = float2(
        input[1].position.x * 0.5 + 0.5,
        0.5 - 0.5 * input[1].position.y) * vp;

    const float2 dir = b - a;
    const float len = length(dir);
    const float2 t = len > 1e-4 ? dir / len : float2(1.0, 0.0);
    const float2 n = float2(-t.y, t.x);

    const float half_width = pc.thickness * 0.5;
    const float aa = pc.antialias;
    // Transverse extension always leaves room for the edge AA fade. Longitudinal
    // extension gives round caps (aa > 0) but is dropped for the sharp/accumulate
    // path (aa == 0 -> butt caps), so adjacent segments' quads stop overlapping.
    const float ext_n = half_width + aa;
    // Longitudinal cap extension. Union (aa > 0) uses full round caps + AA margin
    // so overlapping strokes union cleanly. Accumulate (aa == 0) uses a half
    // extension: full round caps would overlap neighbouring segments and
    // over-brighten under alpha-over, butt caps leave joint wedges unfilled and
    // under-brighten; half approximates the old miter joins.
    const float ext_t = aa > 0.5 ? half_width + aa : half_width * 0.5;
    const float4 seg = float4(a, b);

    float2 corners[4];
    corners[0] = a - t * ext_t + n * ext_n;
    corners[1] = a - t * ext_t - n * ext_n;
    corners[2] = b + t * ext_t + n * ext_n;
    corners[3] = b + t * ext_t - n * ext_n;

    float4 colors[4];
    colors[0] = input[0].color;
    colors[1] = input[0].color;
    colors[2] = input[1].color;
    colors[3] = input[1].color;

    for (int i = 0; i < 4; ++i)
    {
        const float2 px = corners[i];
        const float2 ndc = float2(px.x / vp.x * 2.0 - 1.0, 1.0 - px.y / vp.y * 2.0);
        GeometryOutput output;
        output.position = float4(ndc, 0.0, 1.0);
        output.color = colors[i];
        output.seg = seg;
        output.half_width = half_width;
        output.aa = aa;
        stream.Append(output);
    }
    stream.RestartStrip();
}
