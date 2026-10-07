#pragma once

const float pi = 3.14159265358979f;

struct Mat4 {
	float m[16];
};

float radians(float degrees);

float getElement(Mat4 matrix, int row, int column);
void setElement(Mat4& matrix, int row, int column, float value);

Mat4 identityMatrix();
Mat4 multiply(Mat4 a, Mat4 b);

Mat4 translationMatrix(float x, float y, float z);
Mat4 scaleMatrix(float x, float y, float z);
Mat4 rotationXMatrix(float angle);
Mat4 rotationYMatrix(float angle);
Mat4 rotationZMatrix(float angle);

Mat4 perspectiveMatrix(float fov_y, float aspect, float near_plane, float far_plane);
Mat4 orthographicMatrix(float left, float right, float bottom, float top, float near_plane, float far_plane);
