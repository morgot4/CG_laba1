#pragma once

#include <cstdint>
#include <vector>

struct Vertex {
	float position[3];
	float color[3];
	float normal[3];
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

Mesh createSphereMesh(int stacks, int slices);
