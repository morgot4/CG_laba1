#version 450

layout(set = 0, binding = 0) uniform Scene {
    mat4 view_projection;
    vec4 camera_position;
    vec4 camera_direction;
    vec4 ambient;
    vec4 directional_direction;
    vec4 directional_color;
    ivec4 light_counts;
} scene;

layout(std140, set = 1, binding = 0) uniform Object {
    mat4 model;
    vec4 albedo;
    vec4 specular_shininess;
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(location = 2) out vec2 texture_uv;

void main() {
    vec4 world = object.model * vec4(in_position, 1.0);
    gl_Position = scene.view_projection * world;
    world_position = world.xyz;
    world_normal = transpose(inverse(mat3(object.model))) * in_normal;
    texture_uv = in_uv;
}
