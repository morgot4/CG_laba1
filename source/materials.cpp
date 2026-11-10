#include "materials.hpp"

#include <iostream>

#include "texture.hpp"

namespace materials {
namespace {

struct MaterialFiles {
	const char* name;
	const char* albedo;
	const char* specular;
	const char* emissive;
};

constexpr MaterialFiles files[count] = {
	{ "Ceramic tiles", "textures/ceramic_albedo.png", "textures/ceramic_specular.png", nullptr },
	{ "Copper panels", "textures/copper_albedo.png", "textures/copper_specular.png", nullptr },
	{ "Lava (emissive)", "textures/lava_albedo.png", "textures/lava_specular.png", "textures/lava_emissive.png" },
	{ "UV grid (matte)", "textures/uv_grid.png", nullptr, nullptr },
};

struct Material {
	graphics::Texture albedo;
	graphics::Texture specular;
	graphics::Texture emissive;
	VkDescriptorSet set = VK_NULL_HANDLE;
};

Material loaded[count];
graphics::Texture black;
VkSampler sampler;

}

bool initialize() {
	black = graphics::loadTexture("textures/black.png", VK_FORMAT_R8G8B8A8_UNORM);
	sampler = graphics::createTextureSampler();
	if (!black.view || !sampler) return false;
	for (int i = 0; i < count; ++i) {
		auto& material = loaded[i];
		material.albedo = graphics::loadTexture(files[i].albedo, VK_FORMAT_R8G8B8A8_SRGB);
		if (!material.albedo.view) return false;
		if (files[i].specular) {
			material.specular = graphics::loadTexture(files[i].specular, VK_FORMAT_R8G8B8A8_UNORM);
			if (!material.specular.view) return false;
		}
		if (files[i].emissive) {
			material.emissive = graphics::loadTexture(files[i].emissive, VK_FORMAT_R8G8B8A8_SRGB);
			if (!material.emissive.view) return false;
		}
	}
	return true;
}

void shutdown() {
	for (auto& material : loaded) {
		graphics::destroyTexture(material.emissive);
		graphics::destroyTexture(material.specular);
		graphics::destroyTexture(material.albedo);
		material = {};
	}
	graphics::destroyTexture(black);
	vkDestroySampler(graphics::internal::context.device, sampler, nullptr);
	black = {};
	sampler = VK_NULL_HANDLE;
}

bool createDescriptorSets(VkDescriptorPool pool, VkDescriptorSetLayout layout,
                          graphics::Buffer models, VkDeviceSize object_size) {
	for (auto& material : loaded) {
		const VkDescriptorSetAllocateInfo allocate = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &layout,
		};
		VkDevice device = graphics::internal::context.device;
		if (vkAllocateDescriptorSets(device, &allocate, &material.set) != VK_SUCCESS) return false;
		const VkDescriptorBufferInfo buffer = { models.buffer, 0, object_size };
		const VkImageView views[] = {
			material.albedo.view,
			material.specular.view ? material.specular.view : black.view,
			material.emissive.view ? material.emissive.view : black.view,
		};
		VkDescriptorImageInfo images[3] = {};
		VkWriteDescriptorSet writes[4] = {};
		writes[0] = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = material.set, .dstBinding = 0,
			.descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .pBufferInfo = &buffer,
		};
		for (uint32_t i = 0; i < 3; ++i) {
			images[i] = { sampler, views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			writes[i + 1] = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = material.set, .dstBinding = i + 1,
				.descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &images[i],
			};
		}
		vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
	}
	return true;
}

VkDescriptorSet descriptorSet(int index) { return loaded[index].set; }
const char* name(int index) { return files[index].name; }
bool hasSpecular(int index) { return files[index].specular != nullptr; }
bool hasEmissive(int index) { return files[index].emissive != nullptr; }

}
