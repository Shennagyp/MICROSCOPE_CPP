#include "amscope/AmcamCameraProfiles.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using amscope::AmcamCameraFeature;
    using amscope::AmcamCameraModelProfile;
    using amscope::AmcamCameraProfileRegistry;

    const AmcamCameraProfileRegistry noProfiles;
    require(noProfiles.allows("Model-A", AmcamCameraFeature::ExposureAuto),
            "unprofiled model is not restricted");

    AmcamCameraModelProfile restricted;
    restricted.modelCode = " Model-A ";
    restricted.allowedFeatures = {
        AmcamCameraFeature::ExposureAuto,
        AmcamCameraFeature::WhiteBalance,
        AmcamCameraFeature::AnalogGain
    };
    restricted.deniedFeatures = {AmcamCameraFeature::WhiteBalance};

    const AmcamCameraProfileRegistry registry({restricted});
    require(registry.allows("Model-A", AmcamCameraFeature::ExposureAuto),
            "allow-list permits an explicitly allowed feature");
    require(!registry.allows("Model-A", AmcamCameraFeature::WhiteBalance),
            "deny-list takes precedence over allow-list");
    require(!registry.allows("Model-A", AmcamCameraFeature::PreciseFrameRate),
            "profile denies features omitted from its allow-list");
    require(registry.allows("Model-B", AmcamCameraFeature::PreciseFrameRate),
            "unprofiled model retains independently probed support");

    AmcamCameraModelProfile older;
    older.modelCode = "Model-C";
    older.allowedFeatures = {AmcamCameraFeature::ExposureAuto};

    AmcamCameraModelProfile newest;
    newest.modelCode = " Model-C ";
    newest.allowedFeatures = {AmcamCameraFeature::Resolution};

    const AmcamCameraProfileRegistry duplicateRegistry({older, newest});
    require(!duplicateRegistry.allows("Model-C", AmcamCameraFeature::ExposureAuto),
            "newest duplicate model profile replaces the previous one");
    require(duplicateRegistry.allows("Model-C", AmcamCameraFeature::Resolution),
            "latest duplicate profile is applied");

    std::cout << "All Amcam model-profile tests passed.\n";
    return EXIT_SUCCESS;
}
