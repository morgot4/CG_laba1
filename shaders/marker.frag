#version 450
layout(push_constant) uniform Marker { mat4 model; vec4 color; } marker;
layout(location = 0) out vec4 out_color;
void main() { out_color = marker.color; }
