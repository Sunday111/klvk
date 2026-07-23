// Port of blur.frag: separable-free box-weighted Gaussian blend with the scene.

[[vk::binding(0, 0)]] Sampler2D scene_texture;

struct Settings
{
    float4 data;
};

[[vk::push_constant]] Settings settings;

struct FragmentInput
{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input): SV_Target
{
    uint width;
    uint height;
    scene_texture.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(width, height);

    float3 sum = float3(0.0, 0.0, 0.0);
    float total = 0.0;
    const int radius = int(settings.data.x);
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float weight = exp(-float(x * x + y * y) / max(settings.data.y, 0.1));
            sum += scene_texture.Sample(input.uv + float2(x, y) * texel).rgb * weight;
            total += weight;
        }
    }
    const float3 original = scene_texture.Sample(input.uv).rgb;
    return float4(lerp(original, sum / total, settings.data.z), 1.0);
}
