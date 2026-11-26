#include <amscope/AmcamCamera.h>
int main() {
    amscope::AmcamCamera camera;
    return camera.state() == amscope::CameraState::Closed ? 0 : 1;
}
