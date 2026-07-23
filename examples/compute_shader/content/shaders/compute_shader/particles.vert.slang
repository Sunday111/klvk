// Port of particles.vert. COLOR_FUNCTION is a specialization constant
// (constant_id 0, matching particles.shader.json).

[[vk::constant_id(0)]] const int COLOR_FUNCTION = 0;

struct Particle
{
    float4 position;
    float4 velocity;
};

[[vk::binding(0, 0)]] StructuredBuffer<Particle> particles;

struct Graphics
{
    float4x4 mvp;
    float4 color;
    float4 body_a;
    float4 body_b;
};

[[vk::push_constant]] Graphics graphics;

struct VertexOutput
{
    float4 position : SV_Position;
    float point_size : SV_PointSize;
    [[vk::location(0)]] float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput main(uint vertex_index: SV_VertexID)
{
    const Particle particle = particles[vertex_index];

    VertexOutput output;
    output.position = mul(graphics.mvp, float4(particle.position.xyz, 1.0));
    output.point_size = 2.0;

    if (COLOR_FUNCTION == 1)
    {
        const float3 v = abs(normalize(particle.velocity.xyz));
        const float mc = max(max(v.x, max(v.y, v.z)), 0.1);
        output.color = float4(v / mc, graphics.color.a);
    }
    else if (COLOR_FUNCTION == 2)
    {
        const float4 slow = float4(1, 0, 0, graphics.color.a);
        const float4 fast = float4(1, 1, 1, graphics.color.a);
        const float speed = length(particle.velocity.xyz);
        output.color = lerp(slow, fast, clamp(speed / 350.0f, 0.0f, 1.0f));
    }
    else
    {
        output.color = graphics.color;
    }
    return output;
}
