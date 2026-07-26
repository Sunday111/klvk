// Distance-field coverage: signed pixel distance to the segment -> antialiased
// coverage. Output is premultiplied so the MAX blend in the pipeline unions
// overlapping strokes instead of alpha-over double-blending them.

struct FragmentInput
{
    float4 position : SV_Position;                      // window pixels
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] nointerpolation float4 seg : SEG;     // (a.xy, b.xy)
    [[vk::location(2)]] nointerpolation float half_width : HALF_WIDTH;
    [[vk::location(3)]] nointerpolation float aa : AA;        // AA half-width in px; 0 = sharp
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target
{
    const float2 p = input.position.xy;
    const float2 a = input.seg.xy;
    const float2 b = input.seg.zw;
    const float2 ba = b - a;
    const float bb = max(dot(ba, ba), 1e-6);
    const float h = saturate(dot(p - a, ba) / bb);      // clamp -> round caps
    const float dist = length(p - (a + ba * h));
    // aa == 0 (accumulate/sharp) collapses the smoothstep to a hard edge; the 4K
    // supersampled target antialiases it. aa == 1 (union) gives a 1px edge fade.
    const float w = max(input.aa, 1e-3);
    const float coverage = 1.0 - smoothstep(input.half_width - w, input.half_width + w, dist);
    if (coverage <= 0.0) discard;
    const float alpha = coverage * input.color.a;
    return float4(input.color.rgb * alpha, alpha);      // premultiplied
}
