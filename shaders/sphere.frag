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

struct PointLight { vec4 position; vec4 color_intensity; };
struct SpotLight { vec4 position; vec4 direction_inner; vec4 color_intensity; vec4 outer; };
layout(std430, set = 0, binding = 1) readonly buffer PointLights { PointLight lights[]; } points;
layout(std430, set = 0, binding = 2) readonly buffer SpotLights { SpotLight lights[]; } spots;
layout(std140, set = 1, binding = 0) uniform Object {
    mat4 model;
    vec4 albedo;
    vec4 specular_shininess;
} object;
layout(set = 1, binding = 1) uniform sampler2D albedo_map;
layout(set = 1, binding = 2) uniform sampler2D specular_map;
layout(set = 1, binding = 3) uniform sampler2D emissive_map;

layout(push_constant) uniform Sampling {
    vec2 uv_scale;
    float time;
    float strength;
    int enabled;
    float emission_strength;
    vec2 padding;
} sampling;

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec2 texture_uv;
layout(location = 0) out vec4 out_color;

vec3 contribution(vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 albedo, vec3 specular_color) {
    float diffuse = max(dot(n, l), 0.0);
    vec3 halfway_vector = l + v;
    float specular = 0.0;
    if (diffuse > 0.0 && dot(halfway_vector, halfway_vector) > 0.000001) {
        vec3 h = normalize(halfway_vector);
        specular = pow(max(dot(n, h), 0.0), object.specular_shininess.w);
    }
    return radiance * (albedo * diffuse + specular_color * specular);
}

struct Surface { vec3 albedo; vec3 specular; vec3 emissive; };

Surface sampleSurface() {
    vec2 first_uv = texture_uv * sampling.uv_scale;
    vec2 second_uv = first_uv;
    float blend = 0.0;
    vec3 modulation = vec3(1.0);
    if (sampling.enabled != 0) {
        float t = sampling.time;
        first_uv += sampling.strength * vec2(
            0.035 * sin(6.2831853 * texture_uv.y + t),
            0.025 * cos(6.2831853 * texture_uv.x - t));
        second_uv = texture_uv * sampling.uv_scale * 2.0 + vec2(0.08, -0.04) * t;
        blend = 0.5 * sampling.strength * (0.5 + 0.5 * sin(t));
        modulation = mix(vec3(1.0), 0.75 + 0.25 * sin(t + vec3(0.0, 2.0943951, 4.1887902)), sampling.strength);
    }
    Surface surface;
    surface.albedo = texture(albedo_map, first_uv).rgb;
    surface.specular = texture(specular_map, first_uv).rgb;
    surface.emissive = texture(emissive_map, first_uv).rgb;
    if (sampling.enabled != 0) {
        surface.albedo = mix(surface.albedo, texture(albedo_map, second_uv).rgb, blend);
        surface.specular = mix(surface.specular, texture(specular_map, second_uv).rgb, blend);
        surface.emissive = mix(surface.emissive, texture(emissive_map, second_uv).rgb, blend);
    }
    surface.albedo *= object.albedo.rgb * modulation;
    surface.specular *= object.specular_shininess.rgb;
    surface.emissive *= sampling.emission_strength;
    return surface;
}

void main() {
    vec3 n = normalize(world_normal);
    vec3 to_camera = scene.camera_position.xyz - world_position;
    vec3 v = scene.camera_position.w > 0.5
        ? (length(to_camera) > 0.0001 ? normalize(to_camera) : n)
        : scene.camera_direction.xyz;
    Surface surface = sampleSurface();
    vec3 albedo = surface.albedo;
    vec3 result = scene.ambient.rgb * scene.ambient.w * albedo;
    vec3 direction = scene.directional_direction.xyz;
    if (length(direction) > 0.0001) {
        result += contribution(n, v, -normalize(direction), scene.directional_color.rgb * scene.directional_color.w, albedo, surface.specular);
    }
    for (int i = 0; i < scene.light_counts.x; ++i) {
        vec3 delta = points.lights[i].position.xyz - world_position;
        float distance_squared = dot(delta, delta);
        if (distance_squared < 0.00000001) continue;
        vec3 l = delta * inversesqrt(distance_squared);
        distance_squared = max(distance_squared, 0.01);
        result += contribution(n, v, l, points.lights[i].color_intensity.rgb * points.lights[i].color_intensity.w / distance_squared, albedo, surface.specular);
    }
    for (int i = 0; i < scene.light_counts.y; ++i) {
        vec3 delta = spots.lights[i].position.xyz - world_position;
        float distance_squared = dot(delta, delta);
        if (distance_squared < 0.00000001) continue;
        vec3 l = delta * inversesqrt(distance_squared);
        distance_squared = max(distance_squared, 0.01);
        vec3 direction = spots.lights[i].direction_inner.xyz;
        if (length(direction) < 0.0001) continue;
        float cosine = dot(normalize(direction), -l);
        float inner = spots.lights[i].direction_inner.w;
        float outer = spots.lights[i].outer.x;
        float cone = inner > outer + 0.000001 ? smoothstep(outer, inner, cosine) : float(cosine >= inner);
        result += contribution(n, v, l, spots.lights[i].color_intensity.rgb * spots.lights[i].color_intensity.w * cone / distance_squared, albedo, surface.specular);
    }
    out_color = vec4(result + surface.emissive, 1.0);
}
