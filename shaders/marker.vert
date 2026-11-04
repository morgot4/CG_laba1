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
layout(push_constant) uniform Marker { mat4 model; vec4 color; } marker;
layout(location = 0) in vec3 in_position;
void main() { gl_Position = scene.view_projection * marker.model * vec4(in_position, 1.0); }
