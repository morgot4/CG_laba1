#include "math3d.hpp"

#include <cmath>

float radians(float degrees) {
	return degrees * pi / 180.0f;
}

float getElement(Mat4 matrix, int row, int column) {
	return matrix.m[column * 4 + row];
}

void setElement(Mat4& matrix, int row, int column, float value) {
	matrix.m[column * 4 + row] = value;
}

Mat4 identityMatrix() {
	Mat4 result = {};
	for (int i = 0; i < 4; i++) {
		setElement(result, i, i, 1.0f);
	}
	return result;
}

Mat4 multiply(Mat4 a, Mat4 b) {
	Mat4 result = {};
	for (int row = 0; row < 4; row++) {
		for (int column = 0; column < 4; column++) {
			float sum = 0.0f;
			for (int k = 0; k < 4; k++) {
				sum += getElement(a, row, k) * getElement(b, k, column);
			}
			setElement(result, row, column, sum);
		}
	}
	return result;
}

Mat4 translationMatrix(float x, float y, float z) {
	Mat4 result = identityMatrix();
	setElement(result, 0, 3, x);
	setElement(result, 1, 3, y);
	setElement(result, 2, 3, z);
	return result;
}

Mat4 scaleMatrix(float x, float y, float z) {
	Mat4 result = identityMatrix();
	setElement(result, 0, 0, x);
	setElement(result, 1, 1, y);
	setElement(result, 2, 2, z);
	return result;
}

Mat4 rotationXMatrix(float angle) {
	float c = std::cos(angle);
	float s = std::sin(angle);
	Mat4 result = identityMatrix();
	setElement(result, 1, 1, c);
	setElement(result, 1, 2, -s);
	setElement(result, 2, 1, s);
	setElement(result, 2, 2, c);
	return result;
}

Mat4 rotationYMatrix(float angle) {
	float c = std::cos(angle);
	float s = std::sin(angle);
	Mat4 result = identityMatrix();
	setElement(result, 0, 0, c);
	setElement(result, 0, 2, s);
	setElement(result, 2, 0, -s);
	setElement(result, 2, 2, c);
	return result;
}

Mat4 rotationZMatrix(float angle) {
	float c = std::cos(angle);
	float s = std::sin(angle);
	Mat4 result = identityMatrix();
	setElement(result, 0, 0, c);
	setElement(result, 0, 1, -s);
	setElement(result, 1, 0, s);
	setElement(result, 1, 1, c);
	return result;
}

Mat4 perspectiveMatrix(float fov_y, float aspect, float near_plane, float far_plane) {
	float focal_length = 1.0f / std::tan(fov_y / 2.0f);
	Mat4 result = {};
	setElement(result, 0, 0, focal_length / aspect);
	setElement(result, 1, 1, -focal_length);
	setElement(result, 2, 2, far_plane / (near_plane - far_plane));
	setElement(result, 2, 3, near_plane * far_plane / (near_plane - far_plane));
	setElement(result, 3, 2, -1.0f);
	return result;
}

Mat4 orthographicMatrix(float left, float right, float bottom, float top, float near_plane, float far_plane) {
	Mat4 result = identityMatrix();
	setElement(result, 0, 0, 2.0f / (right - left));
	setElement(result, 1, 1, -2.0f / (top - bottom));
	setElement(result, 2, 2, 1.0f / (near_plane - far_plane));
	setElement(result, 0, 3, -(right + left) / (right - left));
	setElement(result, 1, 3, (top + bottom) / (top - bottom));
	setElement(result, 2, 3, near_plane / (near_plane - far_plane));
	return result;
}
