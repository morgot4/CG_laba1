#include "application.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <imgui.h>

#include "graphics.hpp"
#include "math3d.hpp"
#include "mesh.hpp"

namespace application {

namespace {

const int max_objects = 5;
const int sphere_stacks = 9;
const int sphere_slices = 9;

const float default_positions[max_objects][3] = {
	{ 0.0f, 0.0f, 0.0f },
	{ 3.0f, 0.0f, 0.0f },
	{ -3.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, -3.0f },
	{ 0.0f, 0.0f, 3.0f },
};

const float default_colors[max_objects][3] = {
	{ 1.0f, 1.0f, 1.0f },
	{ 1.0f, 0.5f, 0.5f },
	{ 0.5f, 1.0f, 0.5f },
	{ 0.5f, 0.6f, 1.0f },
	{ 1.0f, 0.9f, 0.4f },
};

struct SceneObject {
	float position[3];
	float rotation[3];
	float scale[3];
	float color[3];
	bool use_vertex_colors;
	bool animation_playing;
	float animation_time;
	float animation_speed;
	float trajectory_radius;
	float trajectory_height;
	int trajectory_frequency[3];
	float spin_speed;
};

struct ObjectUniforms {
	Mat4 model;
	Mat4 view;
	Mat4 projection;
	float color[3];
	float use_vertex_colors;
};

SceneObject objects[max_objects];
ObjectUniforms object_uniforms[max_objects];
int object_count = 1;
int selected_object = 0;

bool use_perspective = true;
bool wireframe = false;
float field_of_view = 60.0f;
float orthographic_size = 3.0f;
float near_plane = 0.1f;
float far_plane = 100.0f;

float camera_distance = 6.0f;
float camera_yaw = 0.0f;
float camera_pitch = 20.0f;

double previous_time = 0.0;

graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
uint32_t vertex_count;
uint32_t index_count;
graphics::Buffer uniform_buffers[max_objects];

VkDescriptorSetLayout descriptor_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet descriptor_sets[max_objects];

VkPipelineLayout pipeline_layout;
VkPipeline fill_pipeline;
VkPipeline wireframe_pipeline;

void resetObject(int index) {
	SceneObject& object = objects[index];
	for (int k = 0; k < 3; k++) {
		object.position[k] = default_positions[index][k];
		object.rotation[k] = 0.0f;
		object.scale[k] = 1.0f;
		object.color[k] = default_colors[index][k];
	}
	object.use_vertex_colors = true;
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

	vertex_buffer = graphics::createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	index_buffer = graphics::createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	if (vertex_buffer.buffer == VK_NULL_HANDLE || index_buffer.buffer == VK_NULL_HANDLE) {
		return false;
	}

	graphics::uploadToBuffer(vertex_buffer, sphere.vertices.data(), vertices_size);
	graphics::uploadToBuffer(index_buffer, sphere.indices.data(), indices_size);
	return true;
}

bool createUniformBuffers() {
	for (int i = 0; i < max_objects; i++) {
		uniform_buffers[i] = graphics::createBuffer(sizeof(ObjectUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		if (uniform_buffers[i].buffer == VK_NULL_HANDLE) {
			return false;
		}
	}
	return true;
}

bool createDescriptorSetLayout() {
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(graphics::internal::context.device, &layout_info, nullptr,
	                                &descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
		return false;
	}
	return true;
}

bool createDescriptorPool() {
	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = max_objects,
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = max_objects,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(graphics::internal::context.device, &pool_info, nullptr,
	                           &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}
	return true;
}

bool createDescriptorSets() {
	VkDevice device = graphics::internal::context.device;

	for (int i = 0; i < max_objects; i++) {
		const VkDescriptorSetAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &descriptor_set_layout,
		};

		if (vkAllocateDescriptorSets(device, &allocate_info, &descriptor_sets[i]) != VK_SUCCESS) {
			std::cerr << "Failed to allocate Vulkan descriptor set #" << i << '\n';
			return false;
		}

		const VkDescriptorBufferInfo buffer_info = {
			.buffer = uniform_buffers[i].buffer,
			.offset = 0,
			.range = sizeof(ObjectUniforms),
		};

		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_sets[i],
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_info,
		};

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}
	return true;
}

bool createPipelines() {
	VkDevice device = graphics::internal::context.device;

	const VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};

	if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	VkShaderModule vertex_shader = graphics::createShaderModule("shaders/sphere.vert.spv");
	VkShaderModule fragment_shader = graphics::createShaderModule("shaders/sphere.frag.spv");

	if (vertex_shader != VK_NULL_HANDLE && fragment_shader != VK_NULL_HANDLE) {
		fill_pipeline = graphics::createPipeline(pipeline_layout, vertex_shader, fragment_shader,
		                                         VK_POLYGON_MODE_FILL);
		wireframe_pipeline = graphics::createPipeline(pipeline_layout, vertex_shader, fragment_shader,
		                                              VK_POLYGON_MODE_LINE);
	}

	vkDestroyShaderModule(device, vertex_shader, nullptr);
	vkDestroyShaderModule(device, fragment_shader, nullptr);

	return fill_pipeline != VK_NULL_HANDLE && wireframe_pipeline != VK_NULL_HANDLE;
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
	Mat4 distance = translationMatrix(0.0f, 0.0f, -camera_distance);
	Mat4 pitch = rotationXMatrix(radians(camera_pitch));
	Mat4 yaw = rotationYMatrix(radians(camera_yaw));
	return multiply(distance, multiply(pitch, yaw));
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
	camera_distance -= io.MouseWheel * 0.5f;

	if (camera_yaw > 180.0f) {
		camera_yaw -= 360.0f;
	}
	if (camera_yaw < -180.0f) {
		camera_yaw += 360.0f;
	}
	camera_pitch = std::clamp(camera_pitch, -89.0f, 89.0f);
	camera_distance = std::clamp(camera_distance, 1.0f, 30.0f);
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
	ImGui::SliderFloat("Distance", &camera_distance, 1.0f, 30.0f);
	ImGui::SliderFloat("Yaw", &camera_yaw, -180.0f, 180.0f, "%.0f deg");
	ImGui::SliderFloat("Pitch", &camera_pitch, -89.0f, 89.0f, "%.0f deg");
	ImGui::TextDisabled("Mouse: drag to rotate, wheel to zoom");
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
	ImGui::SeparatorText("Color");
	ImGui::ColorEdit3("Color", object.color);
	ImGui::Checkbox("Multiply by vertex colors", &object.use_vertex_colors);
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

void drawSceneWindow() {
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);
	ImGui::Text("Sphere mesh: %u vertices, %u triangles", vertex_count, index_count / 3);

	drawProjectionSettings();
	drawCameraSettings();

	ImGui::SeparatorText("Objects");
	ImGui::SliderInt("Object count", &object_count, 1, max_objects);

	ImGui::End();
}

void drawObjectWindow() {
	float right_edge = ImGui::GetIO().DisplaySize.x - 10.0f;
	ImGui::SetNextWindowPos(ImVec2(right_edge, 10.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
	ImGui::Begin("Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

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

	for (int i = 0; i < object_count; i++) {
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		                        0, 1, &descriptor_sets[i], 0, nullptr);
		vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
	}
}

bool createResources() {
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

	vkDestroyPipeline(context.device, wireframe_pipeline, nullptr);
	vkDestroyPipeline(context.device, fill_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);

	for (int i = 0; i < max_objects; i++) {
		graphics::destroyBuffer(uniform_buffers[i]);
	}
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void update(double time) {
	float delta_time = (float)(time - previous_time);
	previous_time = time;

	handleMouseInput();
	drawSceneWindow();
	drawObjectWindow();

	Mat4 view = computeViewMatrix();
	Mat4 projection = computeProjectionMatrix();

	for (int i = 0; i < object_count; i++) {
		if (objects[i].animation_playing) {
			objects[i].animation_time += delta_time * objects[i].animation_speed;
		}

		object_uniforms[i].model = computeModelMatrix(objects[i]);
		object_uniforms[i].view = view;
		object_uniforms[i].projection = projection;
		for (int k = 0; k < 3; k++) {
			object_uniforms[i].color[k] = objects[i].color[k];
		}
		if (objects[i].use_vertex_colors) {
			object_uniforms[i].use_vertex_colors = 1.0f;
		} else {
			object_uniforms[i].use_vertex_colors = 0.0f;
		}
	}
}

void render(const graphics::internal::FrameData& fd) {
	for (int i = 0; i < object_count; i++) {
		graphics::uploadToBuffer(uniform_buffers[i], &object_uniforms[i], sizeof(ObjectUniforms));
	}

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(fd.command_buffer, &begin_info);
	beginRenderPass(fd);
	drawObjects(fd.command_buffer);
	vkCmdEndRenderPass(fd.command_buffer);
	vkEndCommandBuffer(fd.command_buffer);
}

}
