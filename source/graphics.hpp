#pragma once

#include "graphics_internal.hpp"

namespace graphics {

struct Buffer {
	VkBuffer buffer;
	VmaAllocation allocation;
};

Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool device_local = false);
bool uploadToBuffer(Buffer buffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
Buffer createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
void copyBuffer(VkCommandBuffer command, Buffer source, Buffer destination, VkDeviceSize size,
                VkAccessFlags destination_access);
void destroyBuffer(Buffer buffer);

VkShaderModule createShaderModule(const char* path);

VkPipeline createPipeline(VkPipelineLayout layout, VkShaderModule vertex_shader,
                          VkShaderModule fragment_shader, VkPolygonMode polygon_mode,
                          VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                          bool position_only = false);

}
