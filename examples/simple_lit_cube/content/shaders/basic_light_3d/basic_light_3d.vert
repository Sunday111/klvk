#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(set = 0, binding = 0, std140) uniform Scene
{
    mat4 view_projection;
    vec4 view_position;
    vec4 light_position;
    vec4 light_color;
    vec4 object_color;
    float ambient;
    float specular;
} scene;

layout(push_constant) uniform Model
{
    mat4 transform;
} model;

layout(location = 0) out vec3 world_normal;
layout(location = 1) out vec3 world_position;

void main()
{
    vec4 position_world = model.transform * vec4(position, 1.0);
    gl_Position = scene.view_projection * position_world;
    world_position = position_world.xyz;
    world_normal = normalize(mat3(model.transform) * normal);
}
