#pragma once

#include "graphics_internal.hpp"

namespace graphics {

struct Texture {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t width = 0;
	uint32_t height = 0;
};

Texture loadTexture(const char* path, VkFormat format);
void destroyTexture(Texture texture);
VkSampler createTextureSampler();

}
