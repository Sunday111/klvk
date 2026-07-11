#version 450

struct Particle
{
    vec4 position;
    vec4 velocity;
};

layout(std430, set = 0, binding = 0) readonly buffer Particles
{
    Particle particles[];
};

layout(location = 0) out vec4 color;

void main()
{
    Particle particle = particles[gl_VertexIndex];
    gl_Position = vec4(particle.position.xy, 0.0, 1.0);
    gl_PointSize = 2.0;
    float speed = length(particle.velocity.xy);
    color = vec4(0.2 + speed * 3.0, 0.6, 1.0, 0.35);
}
