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
        AmcamCameraFeature::AnalogGain,
        AmcamCameraFeature::RawOutput,
        AmcamCameraFeature::StillCapture,
        AmcamCameraFeature::ImageProcessing,
        AmcamCameraFeature::BlackLevel,
        AmcamCameraFeature::ConversionGain,
        AmcamCameraFeature::LowNoiseMode,
        AmcamCameraFeature::HighFullWell,
        AmcamCameraFeature::ThermoelectricCooling,
        AmcamCameraFeature::ThermoelectricCoolingOnOff,
        AmcamCameraFeature::SensorTemperature,
        AmcamCameraFeature::CoolingFan,
        AmcamCameraFeature::HeatAntiFog,
        AmcamCameraFeature::HardwareROI,
        AmcamCameraFeature::BinningAndSkipping,
        AmcamCameraFeature::SensorCMOS,
        AmcamCameraFeature::SensorCCDProgressive,
        AmcamCameraFeature::SensorCCDInterlaced,
        AmcamCameraFeature::MonochromeSensor,
        AmcamCameraFeature::GlobalShutter,
        AmcamCameraFeature::SensorGhopto,
        AmcamCameraFeature::FrameBuffer,
        AmcamCameraFeature::DDRFrameBuffer,
        AmcamCameraFeature::TriggerSoftware,
        AmcamCameraFeature::TriggerExternal,
        AmcamCameraFeature::TriggerSingle,
        AmcamCameraFeature::SelfTrigger,
        AmcamCameraFeature::HardwareEvents,
        AmcamCameraFeature::FocusMotor,
        AmcamCameraFeature::AutoFocus,
        AmcamCameraFeature::AstroAutoFocuser,
        AmcamCameraFeature::ST4Port,
        AmcamCameraFeature::FilterWheel,
        AmcamCameraFeature::EmbeddedLightSource,
        AmcamCameraFeature::StandaloneLightSource,
        AmcamCameraFeature::HardwareLevelRange,
        AmcamCameraFeature::TransportUSB3,
        AmcamCameraFeature::TransportUSB3OverUSB2,
        AmcamCameraFeature::TransportGigE,
        AmcamCameraFeature::Transport2Point5GigE,
        AmcamCameraFeature::Transport5GigE,
        AmcamCameraFeature::Transport10GigE,
        AmcamCameraFeature::TransportCameraLink,
        AmcamCameraFeature::TransportCoaXPress,
        AmcamCameraFeature::IoControl,
    };
    restricted.deniedFeatures = {AmcamCameraFeature::WhiteBalance};

    const AmcamCameraProfileRegistry registry({restricted});
    require(registry.allows("Model-A", AmcamCameraFeature::ExposureAuto),
            "allow-list permits an explicitly allowed feature");
    require(registry.allows("Model-A", AmcamCameraFeature::RawOutput),
            "allow-list can explicitly permit RAW sensor output");
    require(registry.allows("Model-A", AmcamCameraFeature::StillCapture),
            "allow-list can explicitly permit still capture");
    require(registry.allows("Model-A", AmcamCameraFeature::CoolingFan),
            "allow-list can explicitly permit a model's cooling fan");
    require(registry.allows("Model-A", AmcamCameraFeature::ImageProcessing),
            "allow-list can explicitly permit image processing controls");
    require(registry.allows("Model-A", AmcamCameraFeature::HardwareROI),
            "allow-list can explicitly permit hardware ROI");
    require(registry.allows("Model-A", AmcamCameraFeature::BinningAndSkipping),
            "allow-list can explicitly permit binning and skip modes");
    require(registry.allows("Model-A", AmcamCameraFeature::MonochromeSensor),
            "allow-list can explicitly permit sensor metadata");
    require(registry.allows("Model-A", AmcamCameraFeature::TriggerExternal),
            "allow-list can explicitly permit trigger capability metadata");
    require(registry.allows("Model-A", AmcamCameraFeature::TransportGigE),
            "allow-list can explicitly permit transport metadata");
    require(registry.allows("Model-A", AmcamCameraFeature::IoControl),
            "allow-list can explicitly permit hardware I/O line access");
    require(!registry.allows("Model-A", AmcamCameraFeature::WhiteBalance),
            "deny-list takes precedence over allow-list");
    require(!registry.allows("Model-A", AmcamCameraFeature::PixelFormatSelection),
            "profile denies pixel-format selection when it is omitted from the allow-list");
    require(!registry.allows("Model-A", AmcamCameraFeature::PreciseFrameRate),
            "profile denies features omitted from its allow-list");
    require(!registry.allows("Model-A", AmcamCameraFeature::DiscreteFrameSpeed),
            "profile continues to restrict capabilities omitted from its allow-list");
    require(registry.allows("Model-B", AmcamCameraFeature::CoolingFan),
            "a model without a configured profile is not restricted by the profile registry");
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
