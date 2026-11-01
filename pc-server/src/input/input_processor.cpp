#include "input/input_processor.hpp"

#include <algorithm>
#include <cmath>

namespace argb::input {
namespace {

constexpr float kRotateSensitivity = 4.0f;
constexpr float kMinPitch = -1.45f;
constexpr float kMaxPitch = 1.45f;
constexpr float kMinDistance = 1.8f;
constexpr float kMaxDistance = 14.0f;

} // namespace

float InputProcessor::pointerDistance(const Point& a, const Point& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

void InputProcessor::handleEvent(const TouchEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);

    lastEventTime_ = std::chrono::steady_clock::now();
    hasPendingInput_ = true;

    switch (event.action) {
        case TouchAction::Down:
            pointers_[event.pointerId] = Point{event.x, event.y};
            if (pointers_.size() == 2) {
                auto it = pointers_.begin();
                const Point& a = it->second;
                ++it;
                const Point& b = it->second;
                pinchReference_ = pointerDistance(a, b);
            }
            break;

        case TouchAction::Move: {
            auto it = pointers_.find(event.pointerId);
            if (it == pointers_.end()) {
                pointers_[event.pointerId] = Point{event.x, event.y};
                break;
            }

            const Point previous = it->second;
            it->second = Point{event.x, event.y};

            if (pointers_.size() == 1) {
                const float dx = event.x - previous.x;
                const float dy = event.y - previous.y;
                yaw_ -= dx * kRotateSensitivity;
                pitch_ -= dy * kRotateSensitivity;
                pitch_ = std::clamp(pitch_, kMinPitch, kMaxPitch);
            } else if (pointers_.size() >= 2) {
                auto first = pointers_.begin();
                const Point& a = first->second;
                ++first;
                const Point& b = first->second;
                const float current = pointerDistance(a, b);
                if (pinchReference_ > 1.0e-4f && current > 1.0e-4f) {
                    distance_ *= pinchReference_ / current;
                    distance_ = std::clamp(distance_, kMinDistance, kMaxDistance);
                }
                pinchReference_ = current;
            }
            break;
        }

        case TouchAction::Up:
        case TouchAction::Cancel:
            pointers_.erase(event.pointerId);
            if (pointers_.size() < 2) {
                pinchReference_ = 0.0f;
            }
            break;
    }
}

void InputProcessor::applyToCamera(renderer::Camera& camera) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);

    camera.target = {0.0f, 0.0f, 0.0f};
    camera.position = {
        distance_ * cp * std::sin(yaw_),
        distance_ * sp,
        distance_ * cp * std::cos(yaw_),
    };
}

void InputProcessor::markFrameRendered() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasPendingInput_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    lastInputToFrameMs_ =
        std::chrono::duration<double, std::milli>(now - lastEventTime_).count();
    hasPendingInput_ = false;
}

double InputProcessor::lastInputToFrameMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastInputToFrameMs_;
}

} // namespace argb::input
