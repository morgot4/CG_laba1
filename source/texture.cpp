#include "texture.hpp"

#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "graphics_upload.hpp"

namespace graphics {
namespace {

bool createTextureImage(Texture& texture, VkFormat format) {
	const VkImageCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D, .format = format,
		.extent = { texture.width, texture.height, 1 }, .mipLevels = 1, .arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	const VmaAllocationCreateInfo allocation = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	};
	return vmaCreateImage(internal::context.allocator, &info, &allocation,
	                      &texture.image, &texture.allocation, nullptr) == VK_SUCCESS;
}

bool transferPixels(Texture texture, const unsigned char* pixels) {
	VkDeviceSize size = (VkDeviceSize)texture.width * texture.height * 4;
	Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	if (!staging.buffer) return false;
	if (!uploadToBuffer(staging, pixels, size)) {
		destroyBuffer(staging);
		return false;
	}
	UploadCommands upload = beginUpload();
	if (!upload.command) {
		destroyBuffer(staging);
		return false;
	}
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = texture.image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
	};
	vkCmdPipelineBarrier(upload.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);
	const VkBufferImageCopy region = {
		.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.imageExtent = { texture.width, texture.height, 1 },
	};
	vkCmdCopyBufferToImage(upload.command, staging.buffer, texture.image,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(upload.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);
	bool success = finishUpload(upload);
	destroyBuffer(staging);
	return success;
}

bool createTextureView(Texture& texture, VkFormat format) {
	const VkImageViewCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = texture.image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
	};
	return vkCreateImageView(internal::context.device, &info, nullptr, &texture.view) == VK_SUCCESS;
}

}

Texture loadTexture(const char* path, VkFormat format) {
	int width = 0, height = 0, channels = 0;
	unsigned char* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
	if (!pixels) {
		std::cerr << "Failed to load texture " << path << ": " << stbi_failure_reason()
		          << " (run from the project root)\n";
		return {};
	}
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(internal::context.physical_device, &properties);
	Texture texture;
	texture.width = (uint32_t)width;
	texture.height = (uint32_t)height;
	bool success = texture.width <= properties.limits.maxImageDimension2D &&
	               texture.height <= properties.limits.maxImageDimension2D &&
	               createTextureImage(texture, format) && transferPixels(texture, pixels) &&
	               createTextureView(texture, format);
	stbi_image_free(pixels);
	if (!success) {
		std::cerr << "Failed to create GPU texture " << path << '\n';
		destroyTexture(texture);
		return {};
	}
	std::cout << "Loaded texture: " << path << " (" << width << "x" << height << ")\n";
	return texture;
}

void destroyTexture(Texture texture) {
	vkDestroyImageView(internal::context.device, texture.view, nullptr);
	vmaDestroyImage(internal::context.allocator, texture.image, texture.allocation);
}

VkSampler createTextureSampler() {
	const VkSamplerCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxLod = 0.0f,
	};
	VkSampler sampler = VK_NULL_HANDLE;
	if (vkCreateSampler(internal::context.device, &info, nullptr, &sampler) != VK_SUCCESS) {
		std::cerr << "Failed to create texture sampler\n";
	}
	return sampler;
}

}
