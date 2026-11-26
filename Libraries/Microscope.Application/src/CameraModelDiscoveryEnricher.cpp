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
    const std::string& modelSourceReference,
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
    const std::string& modelSourceReference,
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
        if (!modelSourceReference.empty()) {
            merged.provenanceReference = modelSourceReference;
        }
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
        profile->sourceReference(),
        result.warnings);

    result.status = result.warnings.empty()
        ? CameraModelEnrichmentStatus::ExactProfileApplied
        : CameraModelEnrichmentStatus::ExactProfileAppliedWithConflicts;
    return result;
}

CameraModelEnrichmentResult CameraModelDiscoveryEnricher::enrichBoundDevice(
    const ::camera::CameraDeviceInfo& discoveredDevice,
    const ::camera::CameraModelCatalog& catalog,
    const core::EquipmentComponentId& cameraComponentId,
    const ::camera::CameraModelKey& expectedModelKey,
    std::uint64_t expectedProfileRevision) {
    CameraModelEnrichmentResult result;
    result.deviceInfo = discoveredDevice;
    result.equipmentComponentId = cameraComponentId;

    const auto normalizedExpectedManufacturer =
        ::camera::detail::normalizeCameraModelText(expectedModelKey.manufacturer);
    const auto normalizedExpectedModel =
        ::camera::detail::normalizeCameraModelText(expectedModelKey.model);
    const auto normalizedExpectedVariant = expectedModelKey.variant
        ? ::camera::detail::normalizeCameraModelText(*expectedModelKey.variant)
        : std::string{};

    if (normalizedExpectedManufacturer.empty() ||
        normalizedExpectedModel.empty() ||
        (expectedModelKey.variant && normalizedExpectedVariant.empty()) ||
        expectedModelKey.manufacturer.find('\0') != std::string::npos ||
        expectedModelKey.model.find('\0') != std::string::npos ||
        (expectedModelKey.variant &&
         expectedModelKey.variant->find('\0') != std::string::npos) ||
        expectedProfileRevision == 0) {
        result.status = CameraModelEnrichmentStatus::BoundProfileNotFound;
        addWarning(
            result.warnings,
            "camera.model_binding.expected_profile_invalid",
            "The installed camera component binding does not contain a valid manufacturer/model identity and profile revision.");
        return result;
    }

    const auto identityMatches = [&](const std::optional<std::string>& actual,
                                     const std::string& expected) {
        if (!actual) {
            return true;
        }
        const auto normalizedActual =
            ::camera::detail::normalizeCameraModelText(*actual);
        return normalizedActual.empty() || normalizedActual == expected;
    };

    if (!identityMatches(
            discoveredDevice.identity.manufacturer,
            normalizedExpectedManufacturer) ||
        !identityMatches(
            discoveredDevice.identity.model,
            normalizedExpectedModel)) {
        result.status = CameraModelEnrichmentStatus::BoundProfileIdentityMismatch;
        addWarning(
            result.warnings,
            "camera.model_binding.runtime_identity_conflict",
            "The connected camera reports a manufacturer or model identity that conflicts with the persisted model binding for this installed camera component.");
        return result;
    }

    if (discoveredDevice.identity.modelVariant &&
        !::camera::detail::normalizeCameraModelText(
             *discoveredDevice.identity.modelVariant).empty()) {
        const auto normalizedActualVariant =
            ::camera::detail::normalizeCameraModelText(
                *discoveredDevice.identity.modelVariant);
        if (normalizedActualVariant != normalizedExpectedVariant) {
            result.status = CameraModelEnrichmentStatus::BoundProfileIdentityMismatch;
            addWarning(
                result.warnings,
                "camera.model_binding.runtime_variant_conflict",
                "The connected camera reports a model variant that conflicts with the persisted model binding for this installed camera component.");
            return result;
        }
    }

    ::camera::CameraIdentity boundIdentity;
    boundIdentity.manufacturer = expectedModelKey.manufacturer;
    boundIdentity.model = expectedModelKey.model;
    boundIdentity.modelVariant = expectedModelKey.variant;

    const auto* profile = catalog.findExact(boundIdentity);
    if (profile == nullptr) {
        result.status = CameraModelEnrichmentStatus::BoundProfileNotFound;
        addWarning(
            result.warnings,
            "camera.model_binding.catalog_profile_not_found",
            "The exact camera model profile referenced by the installed component binding is not present in the loaded catalog.");
        return result;
    }

    if (profile->profileRevision() != expectedProfileRevision) {
        result.status = CameraModelEnrichmentStatus::BoundProfileRevisionMismatch;
        addWarning(
            result.warnings,
            "camera.model_binding.catalog_profile_revision_mismatch",
            "The installed camera component binding references a different profile revision than the currently loaded shared catalog. The model profile was not applied.");
        return result;
    }

    auto boundDevice = discoveredDevice;
    boundDevice.identity.manufacturer = expectedModelKey.manufacturer;
    boundDevice.identity.model = expectedModelKey.model;
    if (!normalizedExpectedVariant.empty()) {
        boundDevice.identity.modelVariant = expectedModelKey.variant;
    }

    auto enriched = enrich(boundDevice, catalog);
    enriched.equipmentComponentId = cameraComponentId;
    if (enriched.status != CameraModelEnrichmentStatus::ExactProfileApplied &&
        enriched.status != CameraModelEnrichmentStatus::ExactProfileAppliedWithConflicts) {
        // Do not downgrade a validated persistent binding into an unrelated
        // candidate selection result. The exact profile was already checked.
        enriched.status = CameraModelEnrichmentStatus::BoundProfileNotFound;
        addWarning(
            enriched.warnings,
            "camera.model_binding.profile_application_failed",
            "The persisted camera model binding resolved to an exact catalog profile, but that profile could not be applied to the connected device.");
    }
    return enriched;
}

std::optional<core::CameraSensorCharacteristics>
CameraModelDiscoveryEnricher::toCoreSensorCharacteristics(
    const ::camera::CameraDeviceInfo& enrichedDevice) {
    core::CameraSensorCharacteristics result;
    bool hasKnownFacts = false;

    const auto mapTechnology = [](::camera::CameraSensorTechnology value) {
        switch (value) {
        case ::camera::CameraSensorTechnology::CCD:
            return core::CameraSensorTechnology::CCD;
        case ::camera::CameraSensorTechnology::CMOS:
            return core::CameraSensorTechnology::CMOS;
        case ::camera::CameraSensorTechnology::ScientificCMOS:
            return core::CameraSensorTechnology::ScientificCMOS;
        case ::camera::CameraSensorTechnology::Unknown:
            return core::CameraSensorTechnology::Unknown;
        }
        return core::CameraSensorTechnology::Unknown;
    };
    const auto mapShutter = [](::camera::CameraShutterType value) {
        switch (value) {
        case ::camera::CameraShutterType::Global:
            return core::CameraShutterType::Global;
        case ::camera::CameraShutterType::Rolling:
            return core::CameraShutterType::Rolling;
        case ::camera::CameraShutterType::FrameTransfer:
            return core::CameraShutterType::FrameTransfer;
        case ::camera::CameraShutterType::GlobalReset:
            return core::CameraShutterType::GlobalReset;
        case ::camera::CameraShutterType::Unknown:
            return core::CameraShutterType::Unknown;
        }
        return core::CameraShutterType::Unknown;
    };
    const auto mapColorMode = [](::camera::CameraColorMode value) {
        switch (value) {
        case ::camera::CameraColorMode::Monochrome:
            return core::CameraSensorColorMode::Monochrome;
        case ::camera::CameraColorMode::Color:
            return core::CameraSensorColorMode::Color;
        case ::camera::CameraColorMode::Unknown:
            return core::CameraSensorColorMode::Unknown;
        }
        return core::CameraSensorColorMode::Unknown;
    };
    const auto mapProvenance = [](::camera::CameraSpecificationProvenance value) {
        switch (value) {
        case ::camera::CameraSpecificationProvenance::ManufacturerDatasheet:
            return core::CameraSpecificationProvenance::ManufacturerDatasheet;
        case ::camera::CameraSpecificationProvenance::VendorModelDescriptor:
            return core::CameraSpecificationProvenance::VendorModelDescriptor;
        case ::camera::CameraSpecificationProvenance::UserVerified:
            return core::CameraSpecificationProvenance::UserVerified;
        case ::camera::CameraSpecificationProvenance::Measured:
            return core::CameraSpecificationProvenance::Measured;
        case ::camera::CameraSpecificationProvenance::Unknown:
            return core::CameraSpecificationProvenance::Unknown;
        }
        return core::CameraSpecificationProvenance::Unknown;
    };

    if (enrichedDevice.sensorCharacteristics) {
        const auto& sensor = *enrichedDevice.sensorCharacteristics;
        if (sensor.technology) {
            result.technology = mapTechnology(*sensor.technology);
            hasKnownFacts |= *sensor.technology !=
                ::camera::CameraSensorTechnology::Unknown;
        }
        if (sensor.shutterType) {
            result.shutterType = mapShutter(*sensor.shutterType);
            hasKnownFacts |= *sensor.shutterType !=
                ::camera::CameraShutterType::Unknown;
        }
        if (sensor.colorMode) {
            result.colorMode = mapColorMode(*sensor.colorMode);
            hasKnownFacts |= *sensor.colorMode !=
                ::camera::CameraColorMode::Unknown;
        }
        result.readNoiseElectronsRms = sensor.readNoiseElectronsRms;
        result.darkCurrentElectronsPerPixelPerSecond =
            sensor.darkCurrentElectronsPerPixelPerSecond;
        result.fullWellCapacityElectrons = sensor.fullWellCapacityElectrons;
        result.maximumFrameRateFramesPerSecond =
            sensor.maximumFrameRateFramesPerSecond;
        result.provenance = mapProvenance(sensor.provenance);
        result.provenanceReference = sensor.provenanceReference;

        hasKnownFacts |= sensor.readNoiseElectronsRms.has_value() ||
                         sensor.darkCurrentElectronsPerPixelPerSecond.has_value() ||
                         sensor.fullWellCapacityElectrons.has_value() ||
                         sensor.maximumFrameRateFramesPerSecond.has_value() ||
                         !sensor.quantumEfficiency.empty();

        result.quantumEfficiency.reserve(sensor.quantumEfficiency.size());
        for (const auto& sample : sensor.quantumEfficiency) {
            result.quantumEfficiency.push_back({
                sample.wavelengthNanometers,
                sample.quantumEfficiency});
        }
    }

    if (!result.shutterType &&
        enrichedDevice.capabilities.supports(
            ::camera::CameraCapability::GlobalShutter)) {
        result.shutterType = core::CameraShutterType::Global;
        hasKnownFacts = true;
        if (result.provenance == core::CameraSpecificationProvenance::Unknown) {
            result.provenance =
                core::CameraSpecificationProvenance::VendorModelDescriptor;
        }
    }

    if (!result.colorMode &&
        enrichedDevice.capabilities.supports(
            ::camera::CameraCapability::MonochromeSensor)) {
        result.colorMode = core::CameraSensorColorMode::Monochrome;
        hasKnownFacts = true;
        if (result.provenance == core::CameraSpecificationProvenance::Unknown) {
            result.provenance =
                core::CameraSpecificationProvenance::VendorModelDescriptor;
        }
    }

    return hasKnownFacts
        ? std::optional<core::CameraSensorCharacteristics>{std::move(result)}
        : std::nullopt;
}

core::Result<
    CameraSpecificationProjectionResult,
    CameraSpecificationProjectionError>
CameraModelDiscoveryEnricher::projectToCoreCameraSpecification(
    const core::CameraSpecification& installedSpecification,
    const CameraModelEnrichmentResult& enrichment) {
    if (enrichment.status != CameraModelEnrichmentStatus::ExactProfileApplied &&
        enrichment.status != CameraModelEnrichmentStatus::ExactProfileAppliedWithConflicts) {
        return core::Result<
            CameraSpecificationProjectionResult,
            CameraSpecificationProjectionError>::failure(
                CameraSpecificationProjectionError{
                    CameraSpecificationProjectionErrorCode::ModelProfileNotResolved,
                    std::nullopt});
    }

    if (enrichment.equipmentComponentId &&
        *enrichment.equipmentComponentId != installedSpecification.componentId()) {
        return core::Result<
            CameraSpecificationProjectionResult,
            CameraSpecificationProjectionError>::failure(
                CameraSpecificationProjectionError{
                    CameraSpecificationProjectionErrorCode::EquipmentComponentMismatch,
                    std::nullopt});
    }

    const auto& enrichedDevice = enrichment.deviceInfo;
    std::vector<CameraModelEnrichmentWarning> warnings =
        enrichment.warnings;

    const auto addProjectionWarning = [&](std::string code, std::string message) {
        warnings.push_back({std::move(code), std::move(message)});
    };

    if (enrichedDevice.sensorCharacteristics) {
        const auto& discovered = *enrichedDevice.sensorCharacteristics;
        const auto& installedDimensions =
            installedSpecification.activeSensorDimensions();

        if (discovered.activeSensorDimensions &&
            (discovered.activeSensorDimensions->width != installedDimensions.width() ||
             discovered.activeSensorDimensions->height != installedDimensions.height())) {
            addProjectionWarning(
                "camera.model_profile.active_sensor_dimensions_mismatch",
                "The exact camera model profile reports active sensor dimensions that differ from the installed microscope CameraSpecification. The installed equipment value was retained.");
        }

        if (discovered.pixelPitchXMicrometers &&
            !nearlyEqual(
                *discovered.pixelPitchXMicrometers,
                installedSpecification.pixelPitchXMicrometers())) {
            addProjectionWarning(
                "camera.model_profile.pixel_pitch_x_mismatch",
                "The exact camera model profile reports a different horizontal pixel pitch. The installed microscope CameraSpecification value was retained.");
        }

        if (discovered.pixelPitchYMicrometers &&
            !nearlyEqual(
                *discovered.pixelPitchYMicrometers,
                installedSpecification.pixelPitchYMicrometers())) {
            addProjectionWarning(
                "camera.model_profile.pixel_pitch_y_mismatch",
                "The exact camera model profile reports a different vertical pixel pitch. The installed microscope CameraSpecification value was retained.");
        }
    }

    auto projectedSensor =
        toCoreSensorCharacteristics(enrichedDevice);
    auto mergedSensor = installedSpecification.sensorCharacteristics();

    if (projectedSensor) {
        if (!mergedSensor) {
            mergedSensor = *projectedSensor;
        } else {
            auto& target = *mergedSensor;
            const auto& source = *projectedSensor;

            const auto mergeEnum = [&](auto& targetValue,
                                       const auto& sourceValue,
                                       auto unknownValue,
                                       const char* fieldName) {
                if (!targetValue || *targetValue == unknownValue) {
                    if (sourceValue && *sourceValue != unknownValue) {
                        targetValue = sourceValue;
                    }
                    return;
                }
                if (sourceValue && *sourceValue != unknownValue &&
                    *targetValue != *sourceValue) {
                    addProjectionWarning(
                        "camera.model_profile.sensor_characteristic_conflict",
                        std::string("The exact camera model profile conflicts with the installed CameraSpecification for ") +
                            fieldName + "; the installed value was retained.");
                }
            };

            const auto mergeDouble = [&](std::optional<double>& targetValue,
                                         const std::optional<double>& sourceValue,
                                         const char* fieldName) {
                if (!targetValue) {
                    targetValue = sourceValue;
                    return;
                }
                if (sourceValue &&
                    !nearlyEqual(*targetValue, *sourceValue)) {
                    addProjectionWarning(
                        "camera.model_profile.sensor_characteristic_conflict",
                        std::string("The exact camera model profile conflicts with the installed CameraSpecification for ") +
                            fieldName + "; the installed value was retained.");
                }
            };

            mergeEnum(
                target.technology,
                source.technology,
                core::CameraSensorTechnology::Unknown,
                "sensor technology");
            mergeEnum(
                target.shutterType,
                source.shutterType,
                core::CameraShutterType::Unknown,
                "shutter type");
            mergeEnum(
                target.colorMode,
                source.colorMode,
                core::CameraSensorColorMode::Unknown,
                "sensor color mode");

            mergeDouble(
                target.readNoiseElectronsRms,
                source.readNoiseElectronsRms,
                "read noise");
            mergeDouble(
                target.darkCurrentElectronsPerPixelPerSecond,
                source.darkCurrentElectronsPerPixelPerSecond,
                "dark current");
            mergeDouble(
                target.fullWellCapacityElectrons,
                source.fullWellCapacityElectrons,
                "full-well capacity");
            mergeDouble(
                target.maximumFrameRateFramesPerSecond,
                source.maximumFrameRateFramesPerSecond,
                "maximum frame rate");

            if (target.quantumEfficiency.empty()) {
                target.quantumEfficiency = source.quantumEfficiency;
            } else if (!source.quantumEfficiency.empty()) {
                bool sameCurve =
                    target.quantumEfficiency.size() ==
                    source.quantumEfficiency.size();
                if (sameCurve) {
                    for (std::size_t index = 0;
                         index < target.quantumEfficiency.size();
                         ++index) {
                        if (!nearlyEqual(
                                target.quantumEfficiency[index].wavelengthNanometers,
                                source.quantumEfficiency[index].wavelengthNanometers) ||
                            !nearlyEqual(
                                target.quantumEfficiency[index].quantumEfficiency,
                                source.quantumEfficiency[index].quantumEfficiency)) {
                            sameCurve = false;
                            break;
                        }
                    }
                }
                if (!sameCurve) {
                    addProjectionWarning(
                        "camera.model_profile.sensor_characteristic_conflict",
                        "The exact camera model profile QE curve differs from the installed CameraSpecification; the installed QE curve was retained.");
                }
            }

            if (target.provenance == core::CameraSpecificationProvenance::Unknown &&
                source.provenance != core::CameraSpecificationProvenance::Unknown) {
                target.provenance = source.provenance;
            }
            if (!target.provenanceReference && source.provenanceReference) {
                target.provenanceReference = source.provenanceReference;
            }
        }
    }

    auto pixelPitchX = core::PositiveMicrometers::create(
        installedSpecification.pixelPitchXMicrometers());
    auto pixelPitchY = core::PositiveMicrometers::create(
        installedSpecification.pixelPitchYMicrometers());
    if (!pixelPitchX || !pixelPitchY) {
        return core::Result<
            CameraSpecificationProjectionResult,
            CameraSpecificationProjectionError>::failure(
                CameraSpecificationProjectionError{
                    CameraSpecificationProjectionErrorCode::CoreSpecificationRejected,
                    core::CameraSpecificationError::InvalidSensorCharacteristics});
    }

    auto specification = core::CameraSpecification::create(
        installedSpecification.componentId(),
        installedSpecification.activeSensorDimensions(),
        std::move(pixelPitchX).value(),
        std::move(pixelPitchY).value(),
        installedSpecification.outputModes(),
        installedSpecification.exposureTimeRange(),
        std::move(mergedSensor));

    if (!specification) {
        return core::Result<
            CameraSpecificationProjectionResult,
            CameraSpecificationProjectionError>::failure(
                CameraSpecificationProjectionError{
                    CameraSpecificationProjectionErrorCode::CoreSpecificationRejected,
                    specification.error()});
    }

    return core::Result<
        CameraSpecificationProjectionResult,
        CameraSpecificationProjectionError>::success(
            CameraSpecificationProjectionResult{
                std::move(specification).value(),
                std::move(warnings)});
}

} // namespace microscope::application::camera_support
