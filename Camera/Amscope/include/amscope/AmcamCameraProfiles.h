#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace amscope {

enum class AmcamCameraFeature : std::uint8_t {
    Resolution,
    ExposureAuto,
    ExposureManual,
    AnalogGain,
    WhiteBalance,
    ColorCorrection,
    PixelFormatSelection,
    RawOutput,
    StillCapture,
    PreciseFrameRate,
    DiscreteFrameSpeed,
    ImageProcessing,
    BlackLevel,
    ConversionGain,
    LowNoiseMode,
    HighFullWell,
    ThermoelectricCooling,
    ThermoelectricCoolingOnOff,
    SensorTemperature,
    CoolingFan,
    HeatAntiFog,
    HardwareROI,
    BinningAndSkipping
};

struct AmcamCameraModelProfile {
    std::string modelCode;
    std::unordered_set<AmcamCameraFeature> allowedFeatures;
    std::unordered_set<AmcamCameraFeature> deniedFeatures;
};

// Exact model-code policy. A missing profile imposes no additional restriction.
// A present profile is an allow-list; deny entries take precedence. Profiles
// can narrow SDK-probed capabilities, but cannot fabricate hardware support.
class AmcamCameraProfileRegistry final {
public:
    explicit AmcamCameraProfileRegistry(
        std::vector<AmcamCameraModelProfile> profiles = {});

    [[nodiscard]] bool allows(
        std::string_view modelCode,
        AmcamCameraFeature feature) const;

private:
    std::unordered_map<std::string, AmcamCameraModelProfile> profiles_;
};

} // namespace amscope
