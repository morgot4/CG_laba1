#include "graphics_upload.hpp"

#include <iostream>

namespace graphics {

UploadCommands beginUpload() {
	UploadCommands upload;
	const VkCommandPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = internal::context.graphics_queue_index,
	};
	VkDevice device = internal::context.device;
	if (vkCreateCommandPool(device, &pool, nullptr, &upload.pool) != VK_SUCCESS) return {};
	const VkCommandBufferAllocateInfo allocate = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = upload.pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
	};
	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	if (vkAllocateCommandBuffers(device, &allocate, &upload.command) != VK_SUCCESS ||
	    vkBeginCommandBuffer(upload.command, &begin) != VK_SUCCESS) {
		vkDestroyCommandPool(device, upload.pool, nullptr);
		return {};
	}
	return upload;
}

bool finishUpload(UploadCommands upload) {
	VkDevice device = internal::context.device;
	VkFence fence = VK_NULL_HANDLE;
	const VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	const VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1, .pCommandBuffers = &upload.command,
	};
	bool success = vkEndCommandBuffer(upload.command) == VK_SUCCESS &&
	               vkCreateFence(device, &fence_info, nullptr, &fence) == VK_SUCCESS;
	if (success) {
		success = vkQueueSubmit(internal::context.graphics_queue, 1, &submit, fence) == VK_SUCCESS;
		if (success) success = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
	}
	if (!success) std::cerr << "Failed to submit or complete staging transfer\n";
	vkDestroyFence(device, fence, nullptr);
	vkDestroyCommandPool(device, upload.pool, nullptr);
	return success;
}

void copyBuffer(VkCommandBuffer command, Buffer source, Buffer destination, VkDeviceSize size,
                VkAccessFlags destination_access) {
	const VkBufferCopy region = { .size = size };
	vkCmdCopyBuffer(command, source.buffer, destination.buffer, 1, &region);
	const VkBufferMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = destination_access,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = destination.buffer, .offset = 0, .size = size,
	};
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	                     0, 0, nullptr, 1, &barrier, 0, nullptr);
}

Buffer createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
	Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	Buffer result = createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
	bool success = staging.buffer && result.buffer && uploadToBuffer(staging, data, size);
	if (success) {
		UploadCommands upload = beginUpload();
		success = upload.command != VK_NULL_HANDLE;
		if (success) {
			VkAccessFlags access = usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT
				? VK_ACCESS_INDEX_READ_BIT : VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			copyBuffer(upload.command, staging, result, size, access);
			success = finishUpload(upload);
		}
	}
	destroyBuffer(staging);
	if (success) return result;
	destroyBuffer(result);
	return {};
}

}
