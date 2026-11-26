#include "microscope/application/camera/CameraModelDiscoveryEnricher.h"

#include <cmath>
#include <utility>

namespace microscope::application::camera_support {
namespace {

using Warning = CameraModelEnrichmentWarning;

void addWarning(
    std::vector<Warning>& warnings,
    std::string code,
    std::string message) {
    warnings.push_back({std::move(code), std::move(message)});
}

[[nodiscard]] bool nearlyEqual(double left, double right) noexcept {
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= scale * 1e-9;
}

template <typename TValue>
void mergeOptionalValue(
    std::optional<TValue>& runtimeValue,
    const std::optional<TValue>& modelValue,
    std::vector<Warning>& warnings,
    const char* fieldName) {
    if (!runtimeValue && modelValue) {
        runtimeValue = modelValue;
        return;
    }
    if (runtimeValue && modelValue && *runtimeValue != *modelValue) {
        addWarning(
            warnings,
            "camera.model_profile.runtime_conflict",
            std::string("The live camera backend and persisted model profile disagree on ") +
                fieldName + "; the live backend value was retained.");
    }
}

void mergeOptionalDouble(
    std::optional<double>& runtimeValue,
    const std::optional<double>& modelValue,
    std::vector<Warning>& warnings,
    const char* fieldName) {
    if (!runtimeValue && modelValue) {
        runtimeValue = modelValue;
        return;
    }
    if (runtimeValue && modelValue &&
        !nearlyEqual(*runtimeValue, *modelValue)) {
        addWarning(
            warnings,
            "camera.model_profile.runtime_conflict",
            std::string("The live camera backend and persisted model profile disagree on ") +
                fieldName + "; the live backend value was retained.");
    }
}

[[nodiscard]] bool sameResolution(
    const ::camera::ImageResolution& left,
    const ::camera::ImageResolution& right) noexcept {
    return left.width == right.width && left.height == right.height;
}

[[nodiscard]] bool sameQuantumEfficiencyCurve(
    const std::vector<::camera::CameraQuantumEfficiencySample>& left,
    const std::vector<::camera::CameraQuantumEfficiencySample>& right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!nearlyEqual(
                left[index].wavelengthNanometers,
                right[index].wavelengthNanometers) ||
            !nearlyEqual(
                left[index].quantumEfficiency,
                right[index].quantumEfficiency)) {
            return false;
        }
    }
    return true;
}

void mergeCapabilities(
    ::camera::CameraCapabilities& runtimeCapabilities,
    const ::camera::CameraCapabilities& modelCapabilities,
    std::vector<Warning>& warnings) {
    for (std::size_t index = 0;
         index < ::camera::CameraCapabilities::count;
         ++index) {
        const auto capability = static_cast<::camera::CameraCapability>(index);
        const auto runtimeStatus = runtimeCapabilities.status(capability);
        const auto modelStatus = modelCapabilities.status(capability);

        if (runtimeStatus == ::camera::CapabilityStatus::Unknown) {
            if (modelStatus != ::camera::CapabilityStatus::Unknown) {
                runtimeCapabilities.set(
                    capability,
                    modelStatus == ::camera::CapabilityStatus::Supported);
            }
        } else if (modelStatus != ::camera::CapabilityStatus::Unknown &&
                   runtimeStatus != modelStatus) {
            addWarning(
                warnings,
                "camera.model_profile.capability_conflict",
                "A live camera capability conflicts with the persisted model profile; the live backend capability was retained.");
        }
    }
}

void mergeSensorCharacteristics(
    std::optional<::camera::CameraSensorCharacteristics>& runtimeSensor,
    const ::camera::CameraSensorCharacteristics& modelSensor,
    const ::camera::CameraCapabilities& runtimeCapabilities,
    const ::camera::CameraCapabilities& modelCapabilities,
    std::vector<Warning>& warnings) {
    const bool sensorExists = runtimeSensor.has_value();
    ::camera::CameraSensorCharacteristics merged =
        runtimeSensor.value_or(::camera::CameraSensorCharacteristics{});
    bool usedModelData = !sensorExists;

    const auto runtimeGlobalShutter =
        runtimeCapabilities.status(::camera::CameraCapability::GlobalShutter);
    const auto modelGlobalShutter =
        modelCapabilities.status(::camera::CameraCapability::GlobalShutter);
    const bool globalShutterConflict =
        runtimeGlobalShutter != ::camera::CapabilityStatus::Unknown &&
        modelSensor.shutterType.has_value() &&
        ((runtimeGlobalShutter == ::camera::CapabilityStatus::Supported) !=
         (*modelSensor.shutterType == ::camera::CameraShutterType::Global));

    if (globalShutterConflict) {
        addWarning(
            warnings,
            "camera.model_profile.shutter_conflict",
            "The persisted shutter type conflicts with the live global-shutter capability; the model shutter field was not substituted.");
    }

    const auto runtimeMonochrome =
        runtimeCapabilities.status(::camera::CameraCapability::MonochromeSensor);
    const auto modelMonochrome =
        modelCapabilities.status(::camera::CameraCapability::MonochromeSensor);
    const bool colorModeConflict =
        runtimeMonochrome != ::camera::CapabilityStatus::Unknown &&
        modelSensor.colorMode.has_value() &&
        ((runtimeMonochrome == ::camera::CapabilityStatus::Supported) !=
         (*modelSensor.colorMode == ::camera::CameraColorMode::Monochrome));

    if (colorModeConflict) {
        addWarning(
            warnings,
            "camera.model_profile.color_mode_conflict",
            "The persisted sensor color mode conflicts with the live monochrome capability; the model color mode was not substituted.");
    }

    mergeOptionalValue(
        merged.technology,
        modelSensor.technology,
        warnings,
        "sensor technology");

    if (!globalShutterConflict) {
        mergeOptionalValue(
            merged.shutterType,
            modelSensor.shutterType,
            warnings,
            "shutter type");
    }

    if (!colorModeConflict) {
        mergeOptionalValue(
            merged.colorMode,
            modelSensor.colorMode,
            warnings,
            "sensor color mode");
    }

    if (!merged.activeSensorDimensions && modelSensor.activeSensorDimensions) {
        merged.activeSensorDimensions = modelSensor.activeSensorDimensions;
        usedModelData = true;
    } else if (merged.activeSensorDimensions && modelSensor.activeSensorDimensions &&
               !sameResolution(
                   *merged.activeSensorDimensions,
                   *modelSensor.activeSensorDimensions)) {
        addWarning(
            warnings,
            "camera.model_profile.runtime_conflict",
            "The live camera backend and persisted model profile disagree on active sensor dimensions; the live backend value was retained.");
    }

    mergeOptionalDouble(
        merged.pixelPitchXMicrometers,
        modelSensor.pixelPitchXMicrometers,
        warnings,
        "X pixel pitch");
    mergeOptionalDouble(
        merged.pixelPitchYMicrometers,
        modelSensor.pixelPitchYMicrometers,
        warnings,
        "Y pixel pitch");
    mergeOptionalDouble(
        merged.readNoiseElectronsRms,
        modelSensor.readNoiseElectronsRms,
        warnings,
        "read noise");
    mergeOptionalDouble(
        merged.darkCurrentElectronsPerPixelPerSecond,
        modelSensor.darkCurrentElectronsPerPixelPerSecond,
        warnings,
        "dark current");
    mergeOptionalDouble(
        merged.fullWellCapacityElectrons,
        modelSensor.fullWellCapacityElectrons,
        warnings,
        "full-well capacity");
    mergeOptionalDouble(
        merged.maximumFrameRateFramesPerSecond,
        modelSensor.maximumFrameRateFramesPerSecond,
        warnings,
        "maximum frame rate");

    if (merged.quantumEfficiency.empty() && !modelSensor.quantumEfficiency.empty()) {
        merged.quantumEfficiency = modelSensor.quantumEfficiency;
        usedModelData = true;
    } else if (!merged.quantumEfficiency.empty() &&
               !modelSensor.quantumEfficiency.empty() &&
               !sameQuantumEfficiencyCurve(
                   merged.quantumEfficiency, modelSensor.quantumEfficiency)) {
        addWarning(
            warnings,
            "camera.model_profile.runtime_conflict",
            "The live camera backend and persisted model profile disagree on the QE curve; the live backend curve was retained.");
    }

    if (usedModelData) {
        merged.provenance = modelSensor.provenance;
    }

    if (sensorExists || usedModelData) {
        runtimeSensor = std::move(merged);
    }
}

} // namespace

CameraModelEnrichmentResult CameraModelDiscoveryEnricher::enrich(
    const ::camera::CameraDeviceInfo& discoveredDevice,
    const ::camera::CameraModelCatalog& catalog) {
    CameraModelEnrichmentResult result;
    result.deviceInfo = discoveredDevice;

    if (!discoveredDevice.identity.model ||
        discoveredDevice.identity.model->empty()) {
        result.status = CameraModelEnrichmentStatus::InsufficientIdentity;
        return result;
    }

    const auto candidates = catalog.findCandidates(discoveredDevice.identity);
    if (candidates.empty()) {
        result.status = CameraModelEnrichmentStatus::NoMatchingProfile;
        return result;
    }

    for (const auto* candidate : candidates) {
        if (candidate != nullptr) {
            result.candidateProfiles.push_back(candidate->key());
        }
    }

    const auto* profile = catalog.findExact(discoveredDevice.identity);
    if (profile == nullptr) {
        result.status = CameraModelEnrichmentStatus::CandidateSelectionRequired;
        return result;
    }

    mergeCapabilities(
        result.deviceInfo.capabilities,
        profile->capabilities(),
        result.warnings);

    mergeSensorCharacteristics(
        result.deviceInfo.sensorCharacteristics,
        profile->sensorCharacteristics(),
        discoveredDevice.capabilities,
        profile->capabilities(),
        result.warnings);

    result.status = result.warnings.empty()
        ? CameraModelEnrichmentStatus::ExactProfileApplied
        : CameraModelEnrichmentStatus::ExactProfileAppliedWithConflicts;
    return result;
}

} // namespace microscope::application::camera_support
