#include "input/input_processor.hpp"

#include <cmath>
#include <cstdio>

using argb::input::InputProcessor;
using argb::input::TouchAction;
using argb::input::TouchEvent;
using argb::renderer::Camera;

namespace {

float distanceFromOrigin(const Camera& camera) {
    const auto& p = camera.position;
    return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

} // namespace

int main() {
    InputProcessor processor;
    Camera camera;

    processor.applyToCamera(camera);
    const auto initial = camera.position;
    const float initialDistance = distanceFromOrigin(camera);
    std::printf("initial position: (%.3f, %.3f, %.3f), distance %.3f\n",
                initial.x, initial.y, initial.z, initialDistance);

    processor.handleEvent(TouchEvent{1, TouchAction::Down, 0.5f, 0.5f, 0});
    processor.handleEvent(TouchEvent{1, TouchAction::Move, 0.65f, 0.55f, 0});
    processor.applyToCamera(camera);
    const auto afterDrag = camera.position;
    const bool dragMoved =
        std::abs(afterDrag.x - initial.x) > 1.0e-3f ||
        std::abs(afterDrag.z - initial.z) > 1.0e-3f;
    std::printf("drag moved: %s\n", dragMoved ? "yes" : "no");

    processor.handleEvent(TouchEvent{1, TouchAction::Up, 0.65f, 0.55f, 0});
    processor.applyToCamera(camera);
    const auto afterUp = camera.position;
    const bool upStable =
        std::abs(afterUp.x - afterDrag.x) < 1.0e-4f &&
        std::abs(afterUp.z - afterDrag.z) < 1.0e-4f;
    std::printf("up keeps camera stable: %s\n", upStable ? "yes" : "no");

    processor.handleEvent(TouchEvent{2, TouchAction::Down, 0.4f, 0.5f, 0});
    processor.handleEvent(TouchEvent{3, TouchAction::Down, 0.6f, 0.5f, 0});
    processor.handleEvent(TouchEvent{2, TouchAction::Move, 0.3f, 0.5f, 0});
    processor.handleEvent(TouchEvent{3, TouchAction::Move, 0.7f, 0.5f, 0});
    processor.applyToCamera(camera);
    const float pinchDistance = distanceFromOrigin(camera);
    std::printf("pinch distance: %.3f (initial %.3f)\n", pinchDistance, initialDistance);
    const bool pinchZoomedIn = pinchDistance < initialDistance;

    processor.handleEvent(TouchEvent{2, TouchAction::Cancel, 0.0f, 0.0f, 0});
    processor.handleEvent(TouchEvent{3, TouchAction::Cancel, 0.0f, 0.0f, 0});

    processor.handleEvent(TouchEvent{4, TouchAction::Down, 0.5f, 0.5f, 0});
    processor.handleEvent(TouchEvent{4, TouchAction::Up, 0.5f, 0.5f, 0});
    processor.applyToCamera(camera);
    const float afterReset = distanceFromOrigin(camera);
    const bool cancelStable = std::abs(afterReset - pinchDistance) < 1.0e-3f;
    std::printf("cancel does not change distance: %s\n", cancelStable ? "yes" : "no");

    const bool ok = dragMoved && upStable && pinchZoomedIn && cancelStable;
    std::printf("RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
