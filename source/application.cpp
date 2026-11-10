#include "application.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstddef>

#include <imgui.h>

#include "graphics.hpp"
#include "math3d.hpp"
#include "mesh.hpp"
#include "materials.hpp"

namespace application {

namespace {

const int max_objects = 5;
const int sphere_stacks = 32;
const int sphere_slices = 32;

const float default_positions[max_objects][3] = {
	{ -2.2f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 2.2f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, -3.0f },
	{ 0.0f, 0.0f, 3.0f },
};

struct SceneObject {
	float position[3];
	float rotation[3];
	float scale[3];
	float color[3];
	float specular[3];
	float shininess;
	int material;
	float uv_scale[2];
	bool sampling_enabled;
	bool sampling_playing;
	float sampling_time;
	float sampling_speed;
	float sampling_strength;
	float emission_strength;
	bool animation_playing;
	float animation_time;
	float animation_speed;
	float trajectory_radius;
	float trajectory_height;
	int trajectory_frequency[3];
	float spin_speed;
};

struct alignas(16) SceneUniforms {
	Mat4 view_projection;
	float camera_position[4];
	float camera_direction[4];
	float ambient[4];
	float directional_direction[4];
	float directional_color[4];
	int32_t light_counts[4];
};

struct alignas(16) ObjectUniforms {
	Mat4 model;
	float albedo[4];
	float specular_shininess[4];
};

struct SamplingPush {
	float uv_scale[2];
	float time;
	float strength;
	int32_t enabled;
	float emission_strength;
	float padding[2];
};

struct alignas(16) PointLight {
	float position[4];
	float color_intensity[4];
};

struct alignas(16) SpotLight {
	float position[4];
	float direction_inner[4];
	float color_intensity[4];
	float outer[4];
};

struct MarkerPush {
	Mat4 model;
	float color[4];
};

static_assert(sizeof(SceneUniforms) == 160);
static_assert(sizeof(ObjectUniforms) == 96);
static_assert(sizeof(SamplingPush) == 32);
static_assert(offsetof(SamplingPush, enabled) == 16);
static_assert(offsetof(SamplingPush, emission_strength) == 20);
static_assert(sizeof(PointLight) == 32);
static_assert(sizeof(SpotLight) == 64);

SceneObject objects[max_objects];
ObjectUniforms object_uniforms[max_objects];
int object_count = 3;
int selected_object = 0;

bool use_perspective = true;
bool wireframe = false;
float field_of_view = 60.0f;
float orthographic_size = 3.0f;
float near_plane = 0.1f;
float far_plane = 100.0f;

float camera_position[3] = { 0.0f, 0.0f, 7.5f };
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;
float ambient_color[3] = { 1.0f, 1.0f, 1.0f };
float ambient_intensity = 0.15f;
float directional_direction[3] = { -0.5f, -1.0f, -0.5f };
float directional_color[3] = { 1.0f, 1.0f, 1.0f };
float directional_intensity = 0.8f;
bool show_lights = true;
std::vector<PointLight> point_lights;
std::vector<SpotLight> spot_lights;
SceneUniforms scene_uniforms = {};


double previous_time = 0.0;

graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
uint32_t vertex_count;
uint32_t index_count;
graphics::Buffer model_buffer;
VkDeviceSize model_stride;

graphics::Buffer scene_buffer;
graphics::Buffer point_buffer;
graphics::Buffer spot_buffer;
graphics::Buffer cone_buffer;
graphics::Buffer cone_staging;
size_t point_capacity = 0;
size_t spot_capacity = 0;
size_t cone_capacity = 0;
size_t cone_staging_capacity = 0;
uint32_t cone_vertex_count = 0;
VkDescriptorSetLayout scene_set_layout;
VkDescriptorSetLayout descriptor_set_layout;
VkDescriptorSet scene_set;
VkDescriptorPool descriptor_pool;

VkPipelineLayout pipeline_layout;
VkPipelineLayout marker_pipeline_layout;
VkPipeline fill_pipeline;
VkPipeline wireframe_pipeline;
VkPipeline marker_pipeline;
VkPipeline cone_pipeline;

void resetObject(int index) {
	SceneObject& object = objects[index];
	for (int k = 0; k < 3; k++) {
		object.position[k] = default_positions[index][k];
		object.rotation[k] = 0.0f;
		object.scale[k] = 1.0f;
		object.color[k] = 1.0f;
		object.specular[k] = 1.0f;
	}
	object.shininess = 32.0f;
	object.material = index % materials::count;
	object.uv_scale[0] = object.uv_scale[1] = 1.0f;
	object.sampling_enabled = index == 2;
	object.sampling_playing = true;
	object.sampling_time = 0.0f;
	object.sampling_speed = 1.0f;
	object.sampling_strength = 1.0f;
	object.emission_strength = 1.5f;
	object.animation_playing = false;
	object.animation_time = 0.0f;
	object.animation_speed = 1.0f;
	object.trajectory_radius = 2.0f;
	object.trajectory_height = 0.5f;
	object.trajectory_frequency[0] = 1;
	object.trajectory_frequency[1] = 3;
	object.trajectory_frequency[2] = 2;
	object.spin_speed = 90.0f;
}

bool createSphereBuffers() {
	Mesh sphere = createSphereMesh(sphere_stacks, sphere_slices);
	vertex_count = (uint32_t)sphere.vertices.size();
	index_count = (uint32_t)sphere.indices.size();

	VkDeviceSize vertices_size = sphere.vertices.size() * sizeof(Vertex);
	VkDeviceSize indices_size = sphere.indices.size() * sizeof(uint32_t);

	vertex_buffer = graphics::createDeviceBuffer(sphere.vertices.data(), vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	index_buffer = graphics::createDeviceBuffer(sphere.indices.data(), indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	if (vertex_buffer.buffer == VK_NULL_HANDLE || index_buffer.buffer == VK_NULL_HANDLE) {
		return false;
	}

	return true;
}

bool createUniformBuffers() {
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device, &properties);
	VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
	model_stride = (sizeof(ObjectUniforms) + alignment - 1) / alignment * alignment;
	model_buffer = graphics::createBuffer(model_stride * max_objects, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	std::cout << "Dynamic model UBO: record=" << sizeof(ObjectUniforms) << ", alignment=" << alignment
	          << ", stride=" << model_stride << ", total=" << model_stride * max_objects << '\n';
	scene_buffer = graphics::createBuffer(sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	point_buffer = graphics::createBuffer(sizeof(PointLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	spot_buffer = graphics::createBuffer(sizeof(SpotLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	point_capacity = spot_capacity = 1;
	return model_buffer.buffer && scene_buffer.buffer && point_buffer.buffer && spot_buffer.buffer;
}

bool createDescriptorSetLayout() {
	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	const VkDescriptorSetLayoutCreateInfo scene_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3, .pBindings = scene_bindings,
	};
	VkDevice device = graphics::internal::context.device;
	if (vkCreateDescriptorSetLayout(device, &scene_info, nullptr, &scene_set_layout) != VK_SUCCESS) return false;
	const VkDescriptorSetLayoutBinding object_bindings[] = {
		{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	const VkDescriptorSetLayoutCreateInfo object_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 4, .pBindings = object_bindings,
	};
	return vkCreateDescriptorSetLayout(device, &object_info, nullptr, &descriptor_set_layout) == VK_SUCCESS;
}

bool createDescriptorPool() {
	const VkDescriptorPoolSize sizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, materials::count },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * materials::count },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 },
	};
	const VkDescriptorPoolCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = materials::count + 1, .poolSizeCount = 4, .pPoolSizes = sizes,
	};
	return vkCreateDescriptorPool(graphics::internal::context.device, &info, nullptr, &descriptor_pool) == VK_SUCCESS;
}

void writeBufferDescriptor(VkDescriptorSet set, uint32_t binding, graphics::Buffer buffer,
                           VkDeviceSize size, VkDescriptorType type) {
	const VkDescriptorBufferInfo buffer_info = { buffer.buffer, 0, size };
	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = binding,
		.descriptorCount = 1, .descriptorType = type, .pBufferInfo = &buffer_info,
	};
	vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
}

bool allocateSet(VkDescriptorSetLayout layout, VkDescriptorSet* set) {
	const VkDescriptorSetAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &layout,
	};
	return vkAllocateDescriptorSets(graphics::internal::context.device, &info, set) == VK_SUCCESS;
}

bool createDescriptorSets() {
	if (!allocateSet(scene_set_layout, &scene_set)) return false;
	writeBufferDescriptor(scene_set, 0, scene_buffer, sizeof(SceneUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writeBufferDescriptor(scene_set, 1, point_buffer, sizeof(PointLight), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	writeBufferDescriptor(scene_set, 2, spot_buffer, sizeof(SpotLight), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	return materials::createDescriptorSets(descriptor_pool, descriptor_set_layout, model_buffer, sizeof(ObjectUniforms));
}

bool createPipelines() {
	VkDevice device = graphics::internal::context.device;
	const VkDescriptorSetLayout layouts[] = { scene_set_layout, descriptor_set_layout };
	const VkPushConstantRange push_range = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SamplingPush) };
	const VkPipelineLayoutCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2, .pSetLayouts = layouts, .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
	};
	if (vkCreatePipelineLayout(device, &info, nullptr, &pipeline_layout) != VK_SUCCESS) return false;
	const VkPushConstantRange marker_range = {
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MarkerPush),
	};
	const VkPipelineLayoutCreateInfo marker_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1, .pSetLayouts = &scene_set_layout,
		.pushConstantRangeCount = 1, .pPushConstantRanges = &marker_range,
	};
	if (vkCreatePipelineLayout(device, &marker_info, nullptr, &marker_pipeline_layout) != VK_SUCCESS) return false;
	VkShaderModule vertex = graphics::createShaderModule("shaders/sphere.vert.spv");
	VkShaderModule fragment = graphics::createShaderModule("shaders/sphere.frag.spv");
	VkShaderModule marker_vertex = graphics::createShaderModule("shaders/marker.vert.spv");
	VkShaderModule marker_fragment = graphics::createShaderModule("shaders/marker.frag.spv");
	if (vertex && fragment) {
		fill_pipeline = graphics::createPipeline(pipeline_layout, vertex, fragment, VK_POLYGON_MODE_FILL);
		wireframe_pipeline = graphics::createPipeline(pipeline_layout, vertex, fragment, VK_POLYGON_MODE_LINE);
	}
	if (marker_vertex && marker_fragment) {
		marker_pipeline = graphics::createPipeline(marker_pipeline_layout, marker_vertex, marker_fragment, VK_POLYGON_MODE_FILL,
		                                           VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true);
		cone_pipeline = graphics::createPipeline(marker_pipeline_layout, marker_vertex, marker_fragment, VK_POLYGON_MODE_FILL,
		                                         VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true);
	}
	vkDestroyShaderModule(device, vertex, nullptr);
	vkDestroyShaderModule(device, fragment, nullptr);
	vkDestroyShaderModule(device, marker_vertex, nullptr);
	vkDestroyShaderModule(device, marker_fragment, nullptr);
	return fill_pipeline && wireframe_pipeline && marker_pipeline && cone_pipeline;
}

Mat4 computeModelMatrix(SceneObject object) {
	float t = object.animation_time;
	float offset_x = object.trajectory_radius * std::sin(object.trajectory_frequency[0] * t);
	float offset_y = object.trajectory_height * std::sin(object.trajectory_frequency[1] * t);
	float offset_z = object.trajectory_radius * std::sin(object.trajectory_frequency[2] * t);
	float spin = object.spin_speed * t;

	Mat4 translation = translationMatrix(object.position[0] + offset_x,
	                                     object.position[1] + offset_y,
	                                     object.position[2] + offset_z);
	Mat4 rotation_x = rotationXMatrix(radians(object.rotation[0]));
	Mat4 rotation_y = rotationYMatrix(radians(object.rotation[1] + spin));
	Mat4 rotation_z = rotationZMatrix(radians(object.rotation[2]));
	Mat4 scale = scaleMatrix(object.scale[0], object.scale[1], object.scale[2]);

	Mat4 rotation = multiply(rotation_z, multiply(rotation_y, rotation_x));
	return multiply(translation, multiply(rotation, scale));
}

Mat4 computeViewMatrix() {
	Mat4 translation = translationMatrix(-camera_position[0], -camera_position[1], -camera_position[2]);
	Mat4 pitch = rotationXMatrix(radians(-camera_pitch));
	Mat4 yaw = rotationYMatrix(radians(-camera_yaw));
	return multiply(pitch, multiply(yaw, translation));
}

Mat4 computeProjectionMatrix() {
	VkExtent2D extent = graphics::internal::context.swapchain_extent;
	float aspect = (float)extent.width / (float)extent.height;

	if (use_perspective) {
		return perspectiveMatrix(radians(field_of_view), aspect, near_plane, far_plane);
	}

	float half_height = orthographic_size;
	float half_width = orthographic_size * aspect;
	return orthographicMatrix(-half_width, half_width, -half_height, half_height, near_plane, far_plane);
}

void handleMouseInput() {
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse) {
		return;
	}

	if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		camera_yaw += io.MouseDelta.x * 0.3f;
		camera_pitch += io.MouseDelta.y * 0.3f;
	}
	camera_position[2] -= io.MouseWheel * 0.5f;

	if (camera_yaw > 180.0f) {
		camera_yaw -= 360.0f;
	}
	if (camera_yaw < -180.0f) {
		camera_yaw += 360.0f;
	}
	camera_pitch = std::clamp(camera_pitch, -89.0f, 89.0f);
}

void drawProjectionSettings() {
	ImGui::SeparatorText("Projection");
	if (ImGui::RadioButton("Perspective", use_perspective)) {
		use_perspective = true;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", !use_perspective)) {
		use_perspective = false;
	}

	if (use_perspective) {
		ImGui::SliderFloat("Field of view", &field_of_view, 20.0f, 120.0f, "%.0f deg");
	} else {
		ImGui::SliderFloat("Ortho size", &orthographic_size, 0.5f, 10.0f);
	}
	ImGui::SliderFloat("Near plane", &near_plane, 0.1f, 10.0f);
	ImGui::SliderFloat("Far plane", &far_plane, 11.0f, 100.0f);
	ImGui::Checkbox("Wireframe", &wireframe);
}

void drawCameraSettings() {
	ImGui::SeparatorText("Camera");
	ImGui::DragFloat3("Camera position", camera_position, 0.05f);
	ImGui::SliderFloat("Yaw", &camera_yaw, -180.0f, 180.0f, "%.0f deg");
	ImGui::SliderFloat("Pitch", &camera_pitch, -89.0f, 89.0f, "%.0f deg");
	ImGui::TextDisabled("Mouse: drag to rotate, wheel moves along world Z");
}

void drawTransformSettings(SceneObject& object) {
	ImGui::SeparatorText("Transform");
	ImGui::DragFloat3("Position", object.position, 0.05f, -10.0f, 10.0f);
	ImGui::DragFloat3("Rotation", object.rotation, 1.0f, -180.0f, 180.0f, "%.0f deg");
	ImGui::DragFloat3("Scale", object.scale, 0.01f, 0.1f, 5.0f);
	if (ImGui::Button("Reset object")) {
		resetObject(selected_object);
	}
}

void drawColorSettings(SceneObject& object) {
	ImGui::SeparatorText("Texture material");
	if (ImGui::BeginCombo("Material", materials::name(object.material))) {
		for (int i = 0; i < materials::count; ++i) {
			bool selected = i == object.material;
			if (ImGui::Selectable(materials::name(i), selected)) object.material = i;
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::ColorEdit3("Albedo tint", object.color);
	ImGui::SliderFloat2("UV tiling", object.uv_scale, 0.25f, 8.0f);
	ImGui::Text("Specular map: %s", materials::hasSpecular(object.material) ? "loaded" : "black fallback");
	ImGui::Text("Emissive map: %s", materials::hasEmissive(object.material) ? "loaded" : "black fallback");
	ImGui::ColorEdit3("Specular color", object.specular);
	ImGui::SliderFloat("Shininess", &object.shininess, 1.0f, 256.0f);
	ImGui::SliderFloat("Emission strength", &object.emission_strength, 0.0f, 4.0f);
	ImGui::Checkbox("Animated sampling (push)", &object.sampling_enabled);
	if (object.sampling_enabled) {
		ImGui::Checkbox("Run texture animation", &object.sampling_playing);
		ImGui::SliderFloat("Texture speed", &object.sampling_speed, 0.0f, 3.0f);
		ImGui::SliderFloat("Effect strength", &object.sampling_strength, 0.0f, 1.0f);
		if (ImGui::Button("Reset texture time")) object.sampling_time = 0.0f;
	}
}

void drawAnimationSettings(SceneObject& object) {
	ImGui::SeparatorText("Animation");
	if (object.animation_playing) {
		if (ImGui::Button("Pause")) {
			object.animation_playing = false;
		}
	} else {
		if (ImGui::Button("Play")) {
			object.animation_playing = true;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart")) {
		object.animation_time = 0.0f;
	}

	ImGui::SliderFloat("Speed", &object.animation_speed, 0.0f, 5.0f);
	ImGui::SliderFloat("Radius", &object.trajectory_radius, 0.0f, 5.0f);
	ImGui::SliderFloat("Height", &object.trajectory_height, 0.0f, 3.0f);
	ImGui::SliderInt3("Frequency x y z", object.trajectory_frequency, 1, 5);
	ImGui::SliderFloat("Spin speed", &object.spin_speed, -360.0f, 360.0f, "%.0f deg/s");
}

void drawLightSettings() {
	ImGui::SeparatorText("Lighting");
	ImGui::ColorEdit3("Ambient color", ambient_color);
	ImGui::SliderFloat("Ambient intensity", &ambient_intensity, 0.0f, 2.0f);
	ImGui::DragFloat3("Directional direction", directional_direction, 0.02f);
	ImGui::ColorEdit3("Directional color", directional_color);
	ImGui::SliderFloat("Directional intensity", &directional_intensity, 0.0f, 5.0f);
	ImGui::Checkbox("Show light markers and cones", &show_lights);
	if (ImGui::Button("Add point light")) {
		point_lights.push_back({ { 2, 2, 2, 0 }, { 1, 0.8f, 0.5f, 12 } });
	}
	int remove_point = -1;
	for (size_t i = 0; i < point_lights.size(); i++) {
		ImGui::PushID((int)i);
		if (ImGui::TreeNode("Point light", "Point light %zu", i)) {
			ImGui::DragFloat3("Position", point_lights[i].position, 0.05f);
			ImGui::ColorEdit3("Color", point_lights[i].color_intensity);
			ImGui::DragFloat("Intensity", &point_lights[i].color_intensity[3], 0.1f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (ImGui::Button("Remove")) remove_point = (int)i;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (remove_point >= 0) point_lights.erase(point_lights.begin() + remove_point);
	if (ImGui::Button("Add spotlight")) {
		spot_lights.push_back({ { 0, 3, 3, 0 }, { 0, -0.7f, -0.7f, 20 }, { 0.5f, 0.7f, 1, 20 }, { 30, 0, 0, 0 } });
	}
	int remove_spot = -1;
	for (size_t i = 0; i < spot_lights.size(); i++) {
		ImGui::PushID((int)i + 1000000);
		if (ImGui::TreeNode("Spotlight", "Spotlight %zu", i)) {
			auto& light = spot_lights[i];
			ImGui::DragFloat3("Position", light.position, 0.05f);
			ImGui::DragFloat3("Direction", light.direction_inner, 0.02f);
			ImGui::ColorEdit3("Color", light.color_intensity);
			ImGui::DragFloat("Intensity", &light.color_intensity[3], 0.1f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Inner angle", &light.direction_inner[3], 0.0f, 89.0f, "%.1f deg");
			ImGui::SliderFloat("Outer angle", &light.outer[0], 0.0f, 89.0f, "%.1f deg");
			light.direction_inner[3] = std::min(light.direction_inner[3], light.outer[0]);
			if (ImGui::Button("Remove")) remove_spot = (int)i;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (remove_spot >= 0) spot_lights.erase(spot_lights.begin() + remove_spot);
}

void drawSceneWindow() {
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(330.0f, 700.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Scene");
	ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);
	ImGui::Text("Sphere mesh: %u vertices, %u triangles", vertex_count, index_count / 3);
	ImGui::Text("Lab 3: Texture mapping");

	drawProjectionSettings();
	drawCameraSettings();
	drawLightSettings();

	ImGui::SeparatorText("Objects");
	ImGui::SliderInt("Object count", &object_count, 1, max_objects);
	if (ImGui::Button("All lights off (emission demo)")) {
		ambient_intensity = directional_intensity = 0.0f;
		for (auto& light : point_lights) light.color_intensity[3] = 0.0f;
		for (auto& light : spot_lights) light.color_intensity[3] = 0.0f;
	}
	if (ImGui::Button("Restore default lighting")) {
		ambient_intensity = 0.15f;
		directional_intensity = 0.8f;
	}

	ImGui::End();
}

void drawObjectWindow() {
	float right_edge = ImGui::GetIO().DisplaySize.x - 10.0f;
	ImGui::SetNextWindowPos(ImVec2(right_edge, 10.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(340.0f, 800.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Object");

	if (selected_object >= object_count) {
		selected_object = object_count - 1;
	}
	ImGui::SliderInt("Selected object", &selected_object, 0, object_count - 1);

	SceneObject& object = objects[selected_object];
	drawTransformSettings(object);
	drawColorSettings(object);
	drawAnimationSettings(object);

	ImGui::End();
}

void beginRenderPass(const graphics::internal::FrameData& fd) {
	graphics::internal::Context& context = graphics::internal::context;

	VkClearValue clear_values[2] = {};
	clear_values[0].color.float32[0] = 0.02f;
	clear_values[0].color.float32[1] = 0.02f;
	clear_values[0].color.float32[2] = 0.03f;
	clear_values[0].color.float32[3] = 1.0f;
	clear_values[1].depthStencil.depth = 1.0f;

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = 2,
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
}

void drawObjects(VkCommandBuffer command_buffer) {
	VkExtent2D extent = graphics::internal::context.swapchain_extent;

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)extent.width,
		.height = (float)extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	const VkRect2D scissor = { .extent = extent };

	vkCmdSetViewport(command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);

	if (wireframe) {
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_pipeline);
	} else {
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fill_pipeline);
	}

	const VkDeviceSize vertex_offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer.buffer, &vertex_offset);
	vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &scene_set, 0, nullptr);
	for (int i = 0; i < object_count; i++) {
		VkDescriptorSet material = materials::descriptorSet(objects[i].material);
		uint32_t dynamic_offset = (uint32_t)(i * model_stride);
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		                        1, 1, &material, 1, &dynamic_offset);
		SamplingPush sampling = {
			{ objects[i].uv_scale[0], objects[i].uv_scale[1] },
			objects[i].sampling_time, objects[i].sampling_strength,
			objects[i].sampling_enabled ? 1 : 0, objects[i].emission_strength, { 0, 0 },
		};
		vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
		                   0, sizeof(sampling), &sampling);
		vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
	}
	if (!show_lights) return;
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, marker_pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, marker_pipeline_layout,
	                        0, 1, &scene_set, 0, nullptr);
	auto draw_marker = [&](const float* position, const float* color) {
		MarkerPush marker = { multiply(translationMatrix(position[0], position[1], position[2]), scaleMatrix(0.12f, 0.12f, 0.12f)),
		                      { color[0], color[1], color[2], 1.0f } };
		vkCmdPushConstants(command_buffer, marker_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(marker), &marker);
		vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
	};
	for (const auto& light : point_lights) draw_marker(light.position, light.color_intensity);
	for (const auto& light : spot_lights) draw_marker(light.position, light.color_intensity);
	if (cone_vertex_count) {
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cone_pipeline);
		vkCmdBindVertexBuffers(command_buffer, 0, 1, &cone_buffer.buffer, &vertex_offset);
		MarkerPush marker = { identityMatrix(), { 1, 1, 1, 1 } };
		vkCmdPushConstants(command_buffer, marker_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(marker), &marker);
		vkCmdDraw(command_buffer, cone_vertex_count, 1, 0, 0);
	}
}

bool createResources() {
	if (!materials::initialize()) return false;
	if (!createSphereBuffers()) {
		return false;
	}
	if (!createUniformBuffers()) {
		return false;
	}
	if (!createDescriptorSetLayout()) {
		return false;
	}
	if (!createDescriptorPool()) {
		return false;
	}
	if (!createDescriptorSets()) {
		return false;
	}
	if (!createPipelines()) {
		return false;
	}
	return true;
}

}

bool initialize() {
	for (int i = 0; i < max_objects; i++) {
		resetObject(i);
	}

	if (!createResources()) {
		shutdown();
		return false;
	}
	return true;
}

void shutdown() {
	graphics::internal::Context& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, cone_pipeline, nullptr);
	vkDestroyPipeline(context.device, marker_pipeline, nullptr);
	vkDestroyPipeline(context.device, wireframe_pipeline, nullptr);
	vkDestroyPipeline(context.device, fill_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyPipelineLayout(context.device, marker_pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);

	materials::shutdown();
	graphics::destroyBuffer(model_buffer);
	graphics::destroyBuffer(cone_staging);
	graphics::destroyBuffer(cone_buffer);
	graphics::destroyBuffer(spot_buffer);
	graphics::destroyBuffer(point_buffer);
	graphics::destroyBuffer(scene_buffer);
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void update(double time) {
	float delta_time = (float)(time - previous_time);
	previous_time = time;

	handleMouseInput();
	drawSceneWindow();
	drawObjectWindow();

	scene_uniforms.view_projection = multiply(computeProjectionMatrix(), computeViewMatrix());
	for (int k = 0; k < 3; k++) {
		scene_uniforms.camera_position[k] = camera_position[k];
		scene_uniforms.ambient[k] = ambient_color[k];
		scene_uniforms.directional_direction[k] = directional_direction[k];
		scene_uniforms.directional_color[k] = directional_color[k];
	}
	scene_uniforms.camera_position[3] = use_perspective ? 1.0f : 0.0f;
	float yaw = radians(camera_yaw);
	float pitch = radians(camera_pitch);
	scene_uniforms.camera_direction[0] = std::sin(yaw) * std::cos(pitch);
	scene_uniforms.camera_direction[1] = -std::sin(pitch);
	scene_uniforms.camera_direction[2] = std::cos(yaw) * std::cos(pitch);
	scene_uniforms.ambient[3] = ambient_intensity;
	scene_uniforms.directional_color[3] = directional_intensity;
	for (int i = 0; i < object_count; i++) {
		if (objects[i].animation_playing) objects[i].animation_time += delta_time * objects[i].animation_speed;
		if (objects[i].sampling_enabled && objects[i].sampling_playing) {
			objects[i].sampling_time += delta_time * objects[i].sampling_speed;
		}
		object_uniforms[i].model = computeModelMatrix(objects[i]);
		for (int k = 0; k < 3; k++) {
			object_uniforms[i].albedo[k] = objects[i].color[k];
			object_uniforms[i].specular_shininess[k] = objects[i].specular[k];
		}
		object_uniforms[i].specular_shininess[3] = objects[i].shininess;
	}
}

bool ensureBuffer(graphics::Buffer& buffer, size_t& capacity, size_t count, size_t element_size,
                  VkBufferUsageFlags usage, int descriptor_binding) {
	if (count <= capacity) return true;
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device, &properties);
	if (usage == VK_BUFFER_USAGE_STORAGE_BUFFER_BIT && count * element_size > properties.limits.maxStorageBufferRange) {
		std::cerr << "Too many lights for this GPU storage buffer\n";
		return false;
	}
	bool device_local = (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0;
	VkBufferUsageFlags flags = usage | (device_local ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
	graphics::Buffer replacement = graphics::createBuffer(count * element_size, flags, device_local);
	if (!replacement.buffer) return false;
	if (descriptor_binding >= 0) {
		writeBufferDescriptor(scene_set, descriptor_binding, replacement, count * element_size, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	}
	graphics::destroyBuffer(buffer);
	buffer = replacement;
	capacity = count;
	return true;
}

struct Vec3 { float x, y, z; };
Vec3 add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 mul(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
Vec3 cross(Vec3 a, Vec3 b) { return { a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x }; }
Vec3 normalizeVec(Vec3 v) {
	float length = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
	return length > 0.0001f ? mul(v, 1.0f / length) : Vec3{ 0, 0, 0 };
}
void addLine(std::vector<Vertex>& vertices, Vec3 a, Vec3 b) {
	Vertex first = {}; first.position[0] = a.x; first.position[1] = a.y; first.position[2] = a.z;
	Vertex second = {}; second.position[0] = b.x; second.position[1] = b.y; second.position[2] = b.z;
	vertices.push_back(first); vertices.push_back(second);
}
void appendCone(std::vector<Vertex>& vertices, const SpotLight& light) {
	Vec3 origin = { light.position[0], light.position[1], light.position[2] };
	Vec3 direction = normalizeVec({ light.direction_inner[0], light.direction_inner[1], light.direction_inner[2] });
	if (direction.x == 0 && direction.y == 0 && direction.z == 0) return;
	Vec3 reference = std::abs(direction.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
	Vec3 u = normalizeVec(cross(direction, reference));
	Vec3 v = cross(direction, u);
	float length = 1.5f;
	float radius = length * std::tan(radians(light.outer[0]));
	Vec3 center = add(origin, mul(direction, length));
	const int segments = 32;
	for (int i = 0; i < segments; i++) {
		float a = 2.0f * pi * i / segments;
		float b = 2.0f * pi * (i + 1) / segments;
		Vec3 p = add(center, add(mul(u, radius * std::cos(a)), mul(v, radius * std::sin(a))));
		Vec3 q = add(center, add(mul(u, radius * std::cos(b)), mul(v, radius * std::sin(b))));
		addLine(vertices, p, q);
		if (i % 8 == 0) addLine(vertices, origin, p);
	}
	addLine(vertices, origin, center);
}

void render(const graphics::internal::FrameData& fd) {
	bool points_ok = ensureBuffer(point_buffer, point_capacity, point_lights.size(), sizeof(PointLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1);
	bool spots_ok = ensureBuffer(spot_buffer, spot_capacity, spot_lights.size(), sizeof(SpotLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 2);
	scene_uniforms.light_counts[0] = points_ok ? (int32_t)point_lights.size() : 0;
	scene_uniforms.light_counts[1] = spots_ok ? (int32_t)spot_lights.size() : 0;
	if (points_ok && !point_lights.empty()) graphics::uploadToBuffer(point_buffer, point_lights.data(), point_lights.size() * sizeof(PointLight));
	if (spots_ok && !spot_lights.empty()) {
		std::vector<SpotLight> gpu_spots = spot_lights;
		for (auto& light : gpu_spots) {
			light.direction_inner[3] = std::cos(radians(light.direction_inner[3]));
			light.outer[0] = std::cos(radians(light.outer[0]));
		}
		graphics::uploadToBuffer(spot_buffer, gpu_spots.data(), gpu_spots.size() * sizeof(SpotLight));
	}
	std::vector<Vertex> cone_vertices;
	if (show_lights) for (const auto& light : spot_lights) appendCone(cone_vertices, light);
	bool cone_ok = ensureBuffer(cone_buffer, cone_capacity, cone_vertices.size(), sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, -1);
	cone_ok = cone_ok && ensureBuffer(cone_staging, cone_staging_capacity, cone_vertices.size(), sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, -1);
	cone_vertex_count = cone_ok ? (uint32_t)cone_vertices.size() : 0;
	if (cone_vertex_count && !graphics::uploadToBuffer(cone_staging, cone_vertices.data(), cone_vertices.size() * sizeof(Vertex))) {
		cone_vertex_count = 0;
	}
	graphics::uploadToBuffer(scene_buffer, &scene_uniforms, sizeof(scene_uniforms));
	for (int i = 0; i < object_count; i++) {
		graphics::uploadToBuffer(model_buffer, &object_uniforms[i], sizeof(ObjectUniforms), i * model_stride);
	}
	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(fd.command_buffer, &begin_info);
	if (cone_vertex_count) {
		graphics::copyBuffer(fd.command_buffer, cone_staging, cone_buffer, cone_vertices.size() * sizeof(Vertex),
		                     VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
	}
	beginRenderPass(fd);
	drawObjects(fd.command_buffer);
	vkCmdEndRenderPass(fd.command_buffer);
	vkEndCommandBuffer(fd.command_buffer);
}

}
