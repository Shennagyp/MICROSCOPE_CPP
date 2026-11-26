#pragma once

#include "microscope/core/diagnostics/Diagnostic.h"
#include "microscope/core/equipment/OpticalSpecifications.h"

#include <cstdint>
#include <optional>

namespace microscope::core {

struct FluorescenceCameraRequirements {
    SpectralBand emissionBand;
    std::optional<double> minimumMeanQuantumEfficiency;
    std::optional<double> maximumReadNoiseElectronsRms;
    std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond;
    std::optional<double> minimumFullWellCapacityElectrons;
    std::optional<double> minimumFrameRateFramesPerSecond;
    bool preferMonochrome = true;
    bool preferGlobalShutter = true;
    // Supply all three for a detector-side spectral estimate. The dichroic
    // transmission and emission-filter transmission are applied to the
    // fluorophore emission spectrum before weighting camera QE.
    std::optional<FluorescenceEmissionSpectrum> emissionSpectrum;
    std::optional<OpticalFilterSpecification> dichroic;
    std::optional<OpticalFilterSpecification> emissionFilter;
};

struct CameraSuitabilityReport {
    std::optional<double> meanQuantumEfficiencyInEmissionBand;
    std::optional<double> readNoiseElectronsRms;
    std::optional<double> darkCurrentElectronsPerPixelPerSecond;
    std::optional<double> fullWellCapacityElectrons;
    std::optional<double> maximumFrameRateFramesPerSecond;
    std::optional<CameraShutterType> shutterType;
    std::optional<CameraSensorColorMode> colorMode;
    bool completeSpectralWeighting = false;
    std::uint32_t knownPreferenceFailures = 0;
    std::uint32_t unknownPreferenceData = 0;
    Diagnostics warnings;

    [[nodiscard]] bool hasWarnings() const noexcept {
        return !warnings.empty();
    }
};

// Evaluates detector suitability for the emission band of a fluorescence
// acquisition. Every failure is a warning, not a configuration rejection.
// Physical LightPath connectivity and driver-operability are validated by
// their own contracts and are not overridden by this assessor.
class CameraSuitabilityAssessor final {
public:
    [[nodiscard]] static CameraSuitabilityReport assessFluorescence(
        const CameraSpecification& camera,
        const FluorescenceCameraRequirements& requirements);

    // Lower known shortfall count and better measured detector metrics rank
    // first. Global shutter is intentionally a final preference tie-breaker,
    // never a hard compatibility rule.
    [[nodiscard]] static bool shouldRecommendBefore(
        const CameraSpecification& leftCamera,
        const CameraSuitabilityReport& left,
        const CameraSpecification& rightCamera,
        const CameraSuitabilityReport& right,
        const FluorescenceCameraRequirements& requirements) noexcept;
};

} // namespace microscope::core
