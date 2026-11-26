#include "microscope/application/camera/CameraModelDiscoveryEnricher.h"

#include <algorithm>
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
bool mergeOptionalValue(
    std::optional<TValue>& runtimeValue,
    const std::optional<TValue>& modelValue,
    std::vector<Warning>& warnings,
    const char* fieldName) {
    if (!runtimeValue && modelValue) {
        runtimeValue = modelValue;
        return true;
    }
    if (runtimeValue && modelValue && *runtimeValue != *modelValue) {
        addWarning(
            warnings,
            "camera.model_profile.runtime_conflict",
            std::string("The live camera backend and persisted model profile disagree on ") +
                fieldName + "; the live backend value was retained.");
    }
    return false;
}

bool mergeOptionalDouble(
    std::optional<double>& runtimeValue,
    const std::optional<double>& modelValue,
    std::vector<Warning>& warnings,
    const char* fieldName) {
    if (!runtimeValue && modelValue) {
        runtimeValue = modelValue;
        return true;
    }
    if (runtimeValue && modelValue &&
        !nearlyEqual(*runtimeValue, *modelValue)) {
        addWarning(
            warnings,
            "camera.model_profile.runtime_conflict",
            std::string("The live camera backend and persisted model profile disagree on ") +
                fieldName + "; the live backend value was retained.");
    }
    return false;
}

[[nodiscard]] bool isKnownTechnology(
    ::camera::CameraSensorTechnology value) noexcept {
    return value != ::camera::CameraSensorTechnology::Unknown;
}

[[nodiscard]] bool isKnownShutter(
    ::camera::CameraShutterType value) noexcept {
    return value != ::camera::CameraShutterType::Unknown;
}

[[nodiscard]] bool isKnownColorMode(
    ::camera::CameraColorMode value) noexcept {
    return value != ::camera::CameraColorMode::Unknown;
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
    const bool hasKnownModelShutter =
        modelSensor.shutterType &&
        isKnownShutter(*modelSensor.shutterType);
    const bool hasKnownModelColorMode =
        modelSensor.colorMode &&
        isKnownColorMode(*modelSensor.colorMode);
    const bool globalShutterConflict =
        runtimeGlobalShutter != ::camera::CapabilityStatus::Unknown &&
        hasKnownModelShutter &&
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
    const bool colorModeConflict =
        runtimeMonochrome != ::camera::CapabilityStatus::Unknown &&
        hasKnownModelColorMode &&
        ((runtimeMonochrome == ::camera::CapabilityStatus::Supported) !=
         (*modelSensor.colorMode == ::camera::CameraColorMode::Monochrome));

    if (colorModeConflict) {
        addWarning(
            warnings,
            "camera.model_profile.color_mode_conflict",
            "The persisted sensor color mode conflicts with the live monochrome capability; the model color mode was not substituted.");
    }

    if (merged.technology &&
        !isKnownTechnology(*merged.technology)) {
        merged.technology.reset();
    }
    const auto knownModelTechnology =
        modelSensor.technology && isKnownTechnology(*modelSensor.technology)
            ? modelSensor.technology
            : std::optional<::camera::CameraSensorTechnology>{};
    usedModelData |= mergeOptionalValue(
        merged.technology,
        knownModelTechnology,
        warnings,
        "sensor technology");

    if (merged.shutterType &&
        !isKnownShutter(*merged.shutterType)) {
        merged.shutterType.reset();
    }
    if (!globalShutterConflict) {
        const auto knownModelShutter =
            hasKnownModelShutter
                ? modelSensor.shutterType
                : std::optional<::camera::CameraShutterType>{};
        usedModelData |= mergeOptionalValue(
            merged.shutterType,
            knownModelShutter,
            warnings,
            "shutter type");
    }

    if (merged.colorMode &&
        !isKnownColorMode(*merged.colorMode)) {
        merged.colorMode.reset();
    }
    if (!colorModeConflict) {
        const auto knownModelColorMode =
            hasKnownModelColorMode
                ? modelSensor.colorMode
                : std::optional<::camera::CameraColorMode>{};
        usedModelData |= mergeOptionalValue(
            merged.colorMode,
            knownModelColorMode,
            warnings,
            "sensor color mode");
    }

    // A positive model capability is enough to establish these two facts
    // when no richer sensor profile value exists. A known negative capability
    // is not used to infer Rolling or Color; unknown hardware variants exist.
    if (!merged.shutterType &&
        !modelSensor.shutterType &&
        modelCapabilities.supports(::camera::CameraCapability::GlobalShutter) &&
        runtimeGlobalShutter != ::camera::CapabilityStatus::Unsupported) {
        merged.shutterType = ::camera::CameraShutterType::Global;
        usedModelData = true;
    }
    if (!merged.colorMode &&
        !modelSensor.colorMode &&
        modelCapabilities.supports(::camera::CameraCapability::MonochromeSensor) &&
        runtimeMonochrome != ::camera::CapabilityStatus::Unsupported) {
        merged.colorMode = ::camera::CameraColorMode::Monochrome;
        usedModelData = true;
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

    usedModelData |= mergeOptionalDouble(
        merged.pixelPitchXMicrometers,
        modelSensor.pixelPitchXMicrometers,
        warnings,
        "X pixel pitch");
    usedModelData |= mergeOptionalDouble(
        merged.pixelPitchYMicrometers,
        modelSensor.pixelPitchYMicrometers,
        warnings,
        "Y pixel pitch");
    usedModelData |= mergeOptionalDouble(
        merged.readNoiseElectronsRms,
        modelSensor.readNoiseElectronsRms,
        warnings,
        "read noise");
    usedModelData |= mergeOptionalDouble(
        merged.darkCurrentElectronsPerPixelPerSecond,
        modelSensor.darkCurrentElectronsPerPixelPerSecond,
        warnings,
        "dark current");
    usedModelData |= mergeOptionalDouble(
        merged.fullWellCapacityElectrons,
        modelSensor.fullWellCapacityElectrons,
        warnings,
        "full-well capacity");
    usedModelData |= mergeOptionalDouble(
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
