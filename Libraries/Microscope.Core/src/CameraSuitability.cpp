#include "microscope/core/configuration/CameraSuitability.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace microscope::core {
namespace {

[[nodiscard]] std::optional<double> interpolateQuantumEfficiency(
    const std::vector<CameraQuantumEfficiencySample>& samples,
    double wavelength) noexcept {
    if (samples.size() < 2 ||
        wavelength < samples.front().wavelengthNanometers ||
        wavelength > samples.back().wavelengthNanometers) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index + 1 < samples.size(); ++index) {
        const auto& lower = samples[index];
        const auto& upper = samples[index + 1];
        if (wavelength >= lower.wavelengthNanometers &&
            wavelength <= upper.wavelengthNanometers) {
            const double span =
                upper.wavelengthNanometers - lower.wavelengthNanometers;
            if (span <= 0.0) {
                return std::nullopt;
            }
            const double fraction =
                (wavelength - lower.wavelengthNanometers) / span;
            return lower.quantumEfficiency +
                fraction * (upper.quantumEfficiency - lower.quantumEfficiency);
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<double> meanQuantumEfficiencyInBand(
    const std::vector<CameraQuantumEfficiencySample>& samples,
    const SpectralBand& band) noexcept {
    if (samples.empty()) {
        return std::nullopt;
    }

    const double minimum = band.minimumWavelengthNanometers();
    const double maximum = band.maximumWavelengthNanometers();
    const auto atMinimum = interpolateQuantumEfficiency(samples, minimum);
    if (!atMinimum) {
        return std::nullopt;
    }

    if (maximum == minimum) {
        return atMinimum;
    }

    const auto atMaximum = interpolateQuantumEfficiency(samples, maximum);
    if (!atMaximum) {
        return std::nullopt;
    }

    std::vector<double> wavelengths;
    wavelengths.reserve(samples.size() + 2);
    wavelengths.push_back(minimum);
    for (const auto& sample : samples) {
        if (sample.wavelengthNanometers > minimum &&
            sample.wavelengthNanometers < maximum) {
            wavelengths.push_back(sample.wavelengthNanometers);
        }
    }
    wavelengths.push_back(maximum);

    double area = 0.0;
    for (std::size_t index = 0; index + 1 < wavelengths.size(); ++index) {
        const double leftWavelength = wavelengths[index];
        const double rightWavelength = wavelengths[index + 1];
        const auto leftQe =
            interpolateQuantumEfficiency(samples, leftWavelength);
        const auto rightQe =
            interpolateQuantumEfficiency(samples, rightWavelength);
        if (!leftQe || !rightQe) {
            return std::nullopt;
        }

        area += (rightWavelength - leftWavelength) *
            (*leftQe + *rightQe) * 0.5;
    }

    const double mean = area / (maximum - minimum);
    return std::isfinite(mean) ? std::optional<double>(mean) : std::nullopt;
}

void addWarning(
    CameraSuitabilityReport& report,
    const CameraSpecification& camera,
    std::string code,
    std::string message) {
    DiagnosticContext context;
    context.entityType = "Camera";
    context.entityId = camera.componentId().value();
    context.fieldPath = "sensorCharacteristics";

    auto diagnostic = Diagnostic::create(
        DiagnosticSeverity::Warning,
        std::move(code),
        std::move(message),
        std::move(context));
    if (diagnostic) {
        report.warnings.push_back(std::move(diagnostic).value());
    }
}

void assessMaximum(
    CameraSuitabilityReport& report,
    const CameraSpecification& camera,
    const std::optional<double>& actual,
    const std::optional<double>& maximum,
    const char* unknownCode,
    const char* belowCode,
    const char* label) {
    if (!maximum) {
        return;
    }
    if (!actual) {
        ++report.unknownPreferenceData;
        addWarning(
            report, camera, unknownCode,
            std::string(label) + " is not present in the camera model profile.");
        return;
    }
    if (*actual > *maximum) {
        ++report.knownPreferenceFailures;
        addWarning(
            report, camera, belowCode,
            std::string(label) + " exceeds the requested preference.");
    }
}

void assessMinimum(
    CameraSuitabilityReport& report,
    const CameraSpecification& camera,
    const std::optional<double>& actual,
    const std::optional<double>& minimum,
    const char* unknownCode,
    const char* belowCode,
    const char* label) {
    if (!minimum) {
        return;
    }
    if (!actual) {
        ++report.unknownPreferenceData;
        addWarning(
            report, camera, unknownCode,
            std::string(label) + " is not present in the camera model profile.");
        return;
    }
    if (*actual < *minimum) {
        ++report.knownPreferenceFailures;
        addWarning(
            report, camera, belowCode,
            std::string(label) + " is below the requested preference.");
    }
}

[[nodiscard]] bool hasDifferentKnownValue(
    const std::optional<double>& left,
    const std::optional<double>& right) noexcept {
    return left && right && *left != *right;
}

} // namespace

CameraSuitabilityReport CameraSuitabilityAssessor::assessFluorescence(
    const CameraSpecification& camera,
    const FluorescenceCameraRequirements& requirements) {
    CameraSuitabilityReport report;

    if (!camera.sensorCharacteristics()) {
        ++report.unknownPreferenceData;
        addWarning(
            report,
            camera,
            "camera.fluorescence.sensor_profile_unknown",
            "The camera has no sensor-characteristics profile; fluorescence suitability cannot be fully assessed.");
        return report;
    }

    const auto& sensor = *camera.sensorCharacteristics();
    report.shutterType = sensor.shutterType;
    report.colorMode = sensor.colorMode;
    report.readNoiseElectronsRms = sensor.readNoiseElectronsRms;
    report.darkCurrentElectronsPerPixelPerSecond =
        sensor.darkCurrentElectronsPerPixelPerSecond;
    report.fullWellCapacityElectrons = sensor.fullWellCapacityElectrons;
    report.maximumFrameRateFramesPerSecond =
        sensor.maximumFrameRateFramesPerSecond;

    report.meanQuantumEfficiencyInEmissionBand =
        meanQuantumEfficiencyInBand(
            sensor.quantumEfficiency, requirements.emissionBand);

    if (!report.meanQuantumEfficiencyInEmissionBand) {
        ++report.unknownPreferenceData;
        addWarning(
            report,
            camera,
            "camera.fluorescence.qe_unknown_in_emission_band",
            "The camera QE curve does not cover the complete requested emission band; spectral sensitivity is unknown for this acquisition.");
    } else if (requirements.minimumMeanQuantumEfficiency &&
               *report.meanQuantumEfficiencyInEmissionBand <
                   *requirements.minimumMeanQuantumEfficiency) {
        ++report.knownPreferenceFailures;
        addWarning(
            report,
            camera,
            "camera.fluorescence.qe_below_preference",
            "Mean camera quantum efficiency across the requested emission band is below the configured preference.");
    }

    assessMaximum(
        report, camera,
        sensor.readNoiseElectronsRms,
        requirements.maximumReadNoiseElectronsRms,
        "camera.fluorescence.read_noise_unknown",
        "camera.fluorescence.read_noise_above_preference",
        "Read noise");
    assessMaximum(
        report, camera,
        sensor.darkCurrentElectronsPerPixelPerSecond,
        requirements.maximumDarkCurrentElectronsPerPixelPerSecond,
        "camera.fluorescence.dark_current_unknown",
        "camera.fluorescence.dark_current_above_preference",
        "Dark current");
    assessMinimum(
        report, camera,
        sensor.fullWellCapacityElectrons,
        requirements.minimumFullWellCapacityElectrons,
        "camera.fluorescence.full_well_unknown",
        "camera.fluorescence.full_well_below_preference",
        "Full-well capacity");
    assessMinimum(
        report, camera,
        sensor.maximumFrameRateFramesPerSecond,
        requirements.minimumFrameRateFramesPerSecond,
        "camera.fluorescence.frame_rate_unknown",
        "camera.fluorescence.frame_rate_below_preference",
        "Maximum frame rate");

    if (requirements.preferMonochrome) {
        if (!sensor.colorMode ||
            *sensor.colorMode == CameraSensorColorMode::Unknown) {
            ++report.unknownPreferenceData;
            addWarning(
                report,
                camera,
                "camera.fluorescence.color_mode_unknown",
                "The camera sensor color mode is unknown; monochrome sensitivity preference cannot be evaluated.");
        } else if (*sensor.colorMode != CameraSensorColorMode::Monochrome) {
            ++report.knownPreferenceFailures;
            addWarning(
                report,
                camera,
                "camera.fluorescence.monochrome_preferred",
                "A monochrome sensor is preferred for this fluorescence acquisition; the selected camera is not monochrome.");
        }
    }

    return report;
}

bool CameraSuitabilityAssessor::shouldRecommendBefore(
    const CameraSpecification& leftCamera,
    const CameraSuitabilityReport& left,
    const CameraSpecification& rightCamera,
    const CameraSuitabilityReport& right,
    const FluorescenceCameraRequirements& requirements) noexcept {
    if (left.knownPreferenceFailures != right.knownPreferenceFailures) {
        return left.knownPreferenceFailures < right.knownPreferenceFailures;
    }
    if (left.unknownPreferenceData != right.unknownPreferenceData) {
        return left.unknownPreferenceData < right.unknownPreferenceData;
    }

    if (hasDifferentKnownValue(
            left.meanQuantumEfficiencyInEmissionBand,
            right.meanQuantumEfficiencyInEmissionBand)) {
        return *left.meanQuantumEfficiencyInEmissionBand >
               *right.meanQuantumEfficiencyInEmissionBand;
    }
    if (hasDifferentKnownValue(
            left.readNoiseElectronsRms,
            right.readNoiseElectronsRms)) {
        return *left.readNoiseElectronsRms < *right.readNoiseElectronsRms;
    }
    if (hasDifferentKnownValue(
            left.darkCurrentElectronsPerPixelPerSecond,
            right.darkCurrentElectronsPerPixelPerSecond)) {
        return *left.darkCurrentElectronsPerPixelPerSecond <
               *right.darkCurrentElectronsPerPixelPerSecond;
    }
    if (hasDifferentKnownValue(
            left.fullWellCapacityElectrons,
            right.fullWellCapacityElectrons)) {
        return *left.fullWellCapacityElectrons >
               *right.fullWellCapacityElectrons;
    }
    if (hasDifferentKnownValue(
            left.maximumFrameRateFramesPerSecond,
            right.maximumFrameRateFramesPerSecond)) {
        return *left.maximumFrameRateFramesPerSecond >
               *right.maximumFrameRateFramesPerSecond;
    }

    if (requirements.preferMonochrome &&
        left.colorMode && right.colorMode &&
        *left.colorMode != *right.colorMode) {
        return *left.colorMode == CameraSensorColorMode::Monochrome;
    }

    if (requirements.preferGlobalShutter &&
        left.shutterType && right.shutterType) {
        const bool leftGlobal = *left.shutterType == CameraShutterType::Global;
        const bool rightGlobal = *right.shutterType == CameraShutterType::Global;
        if (leftGlobal != rightGlobal) {
            return leftGlobal;
        }
    }

    return leftCamera.componentId().value() <
           rightCamera.componentId().value();
}

} // namespace microscope::core
