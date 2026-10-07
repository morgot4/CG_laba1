#pragma once

#include "graphics_internal.hpp"

namespace graphics {

struct Buffer {
	VkBuffer buffer;
	VmaAllocation allocation;
};

Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
void uploadToBuffer(Buffer buffer, const void* data, VkDeviceSize size);
void destroyBuffer(Buffer buffer);

VkShaderModule createShaderModule(const char* path);

VkPipeline createPipeline(VkPipelineLayout layout, VkShaderModule vertex_shader,
                          VkShaderModule fragment_shader, VkPolygonMode polygon_mode);

}
