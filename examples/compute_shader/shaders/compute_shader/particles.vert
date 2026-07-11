#version 450
struct Particle { vec4 position; vec4 velocity; };
layout(std430, set = 0, binding = 0) readonly buffer Particles { Particle particles[]; };
layout(push_constant) uniform Graphics
{
    vec4 mvp_col0; vec4 mvp_col1; vec4 mvp_col2; vec4 mvp_col3;
    vec4 color;
    vec4 body_a; vec4 body_b;
} graphics;
layout(location = 0) out vec4 vertex_color;
void main()
{
    Particle particle = particles[gl_VertexIndex];
    mat4 mvp = mat4(graphics.mvp_col0, graphics.mvp_col1, graphics.mvp_col2, graphics.mvp_col3);
    gl_Position = mvp * vec4(particle.position.xyz, 1.0);
    gl_PointSize = 2.0;
    vec3 velocity_color = abs(normalize(particle.velocity.xyz));
    float maximum = max(max(velocity_color.x, velocity_color.y), max(velocity_color.z, 0.1));
    vertex_color = vec4(velocity_color / maximum, graphics.color.a);
}
