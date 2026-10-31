#include "renderer/camera.hpp"

#include <numbers>

namespace argb::renderer {

math::Mat4 Camera::viewMatrix() const {
    return math::lookAt(position, target, up);
}

math::Mat4 Camera::projectionMatrix(float aspect) const {
    const float fovRadians = fovDegrees * std::numbers::pi_v<float> / 180.0f;
    math::Mat4 projection = math::perspective(fovRadians, aspect, nearPlane, farPlane);
    // El eje Y de NDC en Vulkan esta invertido respecto a OpenGL.
    projection.m[5] = -projection.m[5];
    return projection;
}

} // namespace argb::renderer
