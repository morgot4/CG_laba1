#include "mesh.hpp"

#include <cmath>

#include "math3d.hpp"

Mesh createSphereMesh(int stacks, int slices) {
	Mesh mesh;

	for (int i = 0; i <= stacks; i++) {
		float theta = pi * i / stacks;
		for (int j = 0; j <= slices; j++) {
			float phi = 2.0f * pi * j / slices;

			Vertex vertex;
			vertex.position[0] = std::sin(theta) * std::cos(phi);
			vertex.position[1] = std::cos(theta);
			vertex.position[2] = std::sin(theta) * std::sin(phi);
			for (int k = 0; k < 3; k++) {
				vertex.color[k] = vertex.position[k] * 0.5f + 0.5f;
			}
			mesh.vertices.push_back(vertex);
		}
	}

	for (int i = 0; i < stacks; i++) {
		for (int j = 0; j < slices; j++) {
			uint32_t current = i * (slices + 1) + j;
			uint32_t next = current + 1;
			uint32_t below = current + slices + 1;
			uint32_t below_next = below + 1;

			if (i != stacks - 1) {
				mesh.indices.push_back(current);
				mesh.indices.push_back(below_next);
				mesh.indices.push_back(below);
			}
			if (i != 0) {
				mesh.indices.push_back(current);
				mesh.indices.push_back(next);
				mesh.indices.push_back(below_next);
			}
		}
	}

	return mesh;
}
