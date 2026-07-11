#version 450

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

layout(location = 0) in vec3 world_normal;
layout(location = 1) in vec3 world_position;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 normal = normalize(world_normal);
    vec3 light_direction = normalize(scene.light_position.xyz - world_position);
    float diffuse_strength = max(dot(normal, light_direction), 0.0);

    vec3 view_direction = normalize(scene.view_position.xyz - world_position);
    vec3 reflection_direction = reflect(-light_direction, normal);
    float highlight = pow(max(dot(view_direction, reflection_direction), 0.0), 64.0);

    vec3 lighting = (scene.ambient + diffuse_strength) * scene.light_color.xyz;
    lighting += scene.specular * highlight * scene.light_color.xyz;
    out_color = vec4(lighting, 1.0) * scene.object_color;
}
