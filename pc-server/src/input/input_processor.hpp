#pragma once

#include "renderer/camera.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace argb::input {

enum class TouchAction : std::uint16_t {
    Down = 1,
    Move = 2,
    Up = 3,
    Cancel = 4,
};

struct TouchEvent {
    std::uint32_t pointerId = 0;
    TouchAction action = TouchAction::Down;
    float x = 0.0f; // normalizado [0, 1]
    float y = 0.0f;
    std::uint64_t timestampMs = 0;
};

class InputProcessor {
public:
    void handleEvent(const TouchEvent& event);
    void applyToCamera(renderer::Camera& camera) const;
    void markFrameRendered();
    double lastInputToFrameMs() const;

private:
    struct Point {
        float x = 0.0f;
        float y = 0.0f;
    };

    static float pointerDistance(const Point& a, const Point& b);

    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, Point> pointers_;
    float yaw_ = 0.64f;
    float pitch_ = 0.42f;
    float distance_ = 4.36f;
    float pinchReference_ = 0.0f;

    std::chrono::steady_clock::time_point lastEventTime_{};
    bool hasPendingInput_ = false;
    double lastInputToFrameMs_ = 0.0;
};

} // namespace argb::input
