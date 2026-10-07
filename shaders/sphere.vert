#version 450

layout(set = 0, binding = 0) uniform ObjectData {
	mat4 model;
	mat4 view;
	mat4 projection;
	vec3 color;
	float use_vertex_colors;
} ubo;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

void main() {
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(in_position, 1.0);

	vec3 base_color = mix(vec3(1.0), in_color, ubo.use_vertex_colors);
	out_color = base_color * ubo.color;
}
