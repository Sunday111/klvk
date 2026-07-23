// Port of particles.comp: gravitates each particle toward two bodies.

struct Particle
{
    float4 position;
    float4 velocity;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<Particle> particles;

struct Simulation
{
    float4 body_a;
    float4 body_b;
    float delta_time;
    uint particle_count;
};

[[vk::push_constant]] Simulation simulation;

float3 force_by_body(float3 position, float3 body)
{
    const float3 offset = body - position;
    const float distance_to_body = max(length(offset), 0.0001);
    return (1000.0 / distance_to_body) * (offset / distance_to_body);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 thread_id: SV_DispatchThreadID)
{
    const uint index = thread_id.x;
    if (index >= simulation.particle_count) return;

    Particle particle = particles[index];
    const float3 position = particle.position.xyz;
    const float3 acceleration =
        (force_by_body(position, simulation.body_a.xyz) + force_by_body(position, simulation.body_b.xyz)) * 10.0;
    const float dt = simulation.delta_time;
    const float3 new_position = position + particle.velocity.xyz * dt + 0.5 * acceleration * dt * dt;

    // Only .xyz is updated in the GLSL; keep .w so the round trip is exact.
    particle.velocity.xyz = particle.velocity.xyz + acceleration * dt;
    particle.position.xyz = dot(position, position) > 45.0 * 45.0 ? float3(0.0) : new_position;
    particles[index] = particle;
}
