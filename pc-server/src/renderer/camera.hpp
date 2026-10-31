#pragma once

#include "renderer/math.hpp"

namespace argb::renderer {

struct Camera {
    math::Vec3 position{0.0f, 0.0f, 3.0f};
    math::Vec3 target{0.0f, 0.0f, 0.0f};
    math::Vec3 up{0.0f, 1.0f, 0.0f};
    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    math::Mat4 viewMatrix() const;
    math::Mat4 projectionMatrix(float aspect) const;
};

} // namespace argb::renderer
