#pragma once

#include <cmath>

namespace argb::math {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& v, float scalar) { return {v.x * scalar, v.y * scalar, v.z * scalar}; }

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : Vec3{};
}

// Matriz 4x4 column-major (compatible con std140 y con GLSL mat4).
struct Mat4 {
    float m[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                   0.0f, 1.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 1.0f, 0.0f,
                   0.0f, 0.0f, 0.0f, 1.0f};
};

inline Mat4 identity() { return Mat4{}; }

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[column * 4 + k];
            }
            result.m[column * 4 + row] = sum;
        }
    }
    return result;
}

inline Mat4 rotationX(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result{};
    result.m[5] = c;
    result.m[6] = s;
    result.m[9] = -s;
    result.m[10] = c;
    return result;
}

inline Mat4 rotationY(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result{};
    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;
    return result;
}

// Proyeccion en perspectiva para el rango de profundidad de Vulkan [0, 1].
inline Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    Mat4 result{};
    result.m[0] = f / aspect;
    result.m[5] = f;
    result.m[10] = farPlane / (nearPlane - farPlane);
    result.m[11] = -1.0f;
    result.m[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
    result.m[15] = 0.0f;
    return result;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 forward = normalize(center - eye);
    const Vec3 side = normalize(cross(forward, up));
    const Vec3 trueUp = cross(side, forward);

    Mat4 result{};
    result.m[0] = side.x;
    result.m[4] = side.y;
    result.m[8] = side.z;

    result.m[1] = trueUp.x;
    result.m[5] = trueUp.y;
    result.m[9] = trueUp.z;

    result.m[2] = -forward.x;
    result.m[6] = -forward.y;
    result.m[10] = -forward.z;

    result.m[12] = -dot(side, eye);
    result.m[13] = -dot(trueUp, eye);
    result.m[14] = dot(forward, eye);
    result.m[15] = 1.0f;
    return result;
}

} // namespace argb::math
