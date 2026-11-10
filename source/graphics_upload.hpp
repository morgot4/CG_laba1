#pragma once

#include "graphics.hpp"

namespace graphics {

struct UploadCommands {
	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandBuffer command = VK_NULL_HANDLE;
};

UploadCommands beginUpload();
bool finishUpload(UploadCommands commands);

}
