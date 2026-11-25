#include "amscope/AmcamCamera.h"

#include <functional>
#include <iostream>
#include <type_traits>

namespace {

static_assert(std::is_final_v<amscope::AmcamCamera>);
static_assert(!std::is_copy_constructible_v<amscope::AmcamCamera>);
static_assert(!std::is_copy_assignable_v<amscope::AmcamCamera>);
static_assert(!std::is_move_constructible_v<amscope::AmcamCamera>);
static_assert(!std::is_move_assignable_v<amscope::AmcamCamera>);
static_assert(std::is_same_v<
    amscope::AmcamCamera::FrameCallback,
    std::function<void(amscope::FramePtr)>>);

} // namespace

int main() {
    // Construction and idempotent close are pure lifecycle operations while
    // Closed. This test must not enumerate hardware or open an SDK handle.
    amscope::AmcamCamera camera;

    if (camera.state() != amscope::CameraState::Closed) {
        std::cerr << "A newly constructed camera must be Closed.\n";
        return 1;
    }

    if (camera.framesPulled() != 0 ||
        camera.imageEventsReceived() != 0 ||
        camera.droppedFrames() != 0) {
        std::cerr << "A new camera must start with zero acquisition counters.\n";
        return 2;
    }

    amscope::CameraError error;
    if (!camera.close(&error)) {
        std::cerr << "Closing an already Closed camera must be idempotent: "
                  << error.operation << ": " << error.message << '\n';
        return 3;
    }

    if (camera.state() != amscope::CameraState::Closed) {
        std::cerr << "Idempotent close must preserve the Closed state.\n";
        return 4;
    }

    return 0;
}
