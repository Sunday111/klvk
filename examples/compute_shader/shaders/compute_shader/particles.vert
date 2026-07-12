#version 450

// Same color functions as klgl's particle shader. klgl compiles the mode in as
// a define and recompiles at runtime; here it is a specialization constant.
layout(constant_id = 0) const int COLOR_FUNCTION = 0;

struct Particle { vec4 position; vec4 velocity; };
layout(std430, set = 0, binding = 0) readonly buffer Particles { Particle particles[]; };

layout(push_constant) uniform Graphics
{
    mat4 mvp;
    vec4 color;
    vec4 body_a; vec4 body_b;
} graphics;

layout(location = 0) out vec4 vertex_color;

void main()
{
    Particle particle = particles[gl_VertexIndex];
    gl_Position = graphics.mvp * vec4(particle.position.xyz, 1.0);
    gl_PointSize = 2.0;

    if (COLOR_FUNCTION == 1)
    {
        vec3 v = abs(normalize(particle.velocity.xyz));
        float mc = max(max(v.x, max(v.y, v.z)), 0.1);
        vertex_color = vec4(v / mc, graphics.color.a);
    }
    else if (COLOR_FUNCTION == 2)
    {
        vec4 slow = vec4(1, 0, 0, graphics.color.a);
        vec4 fast = vec4(1, 1, 1, graphics.color.a);
        float speed = length(particle.velocity.xyz);
        vertex_color = mix(slow, fast, clamp(speed / 350.f, 0.f, 1.f));
    }
    else
    {
        vertex_color = graphics.color;
    }
}
