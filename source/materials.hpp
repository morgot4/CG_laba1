#pragma once

#include "graphics.hpp"

namespace materials {

constexpr int count = 4;

bool initialize();
void shutdown();
bool createDescriptorSets(VkDescriptorPool pool, VkDescriptorSetLayout layout,
                          graphics::Buffer models, VkDeviceSize object_size);
VkDescriptorSet descriptorSet(int index);
const char* name(int index);
bool hasSpecular(int index);
bool hasEmissive(int index);

}
