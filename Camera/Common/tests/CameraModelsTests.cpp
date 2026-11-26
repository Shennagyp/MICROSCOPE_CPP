#include "camera/CameraModels.h"
#include "camera/CameraModelCatalog.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testCapabilityKnowledge() {
    using camera::CameraCapabilities;
    using camera::CameraCapability;
    using camera::CapabilityStatus;

    CameraCapabilities capabilities;
    require(capabilities.status(CameraCapability::SoftwareTrigger) ==
                CapabilityStatus::Unknown,
            "unprobed capability is Unknown, not Unsupported");
    require(!capabilities.isKnown(CameraCapability::SoftwareTrigger),
            "unknown capability is not marked known");
    require(!capabilities.supports(CameraCapability::SoftwareTrigger),
            "unknown capability is not reported as supported");

    capabilities.set(CameraCapability::SoftwareTrigger, false);
    require(capabilities.status(CameraCapability::SoftwareTrigger) ==
                CapabilityStatus::Unsupported,
            "known absent capability is Unsupported");
    require(capabilities.isKnown(CameraCapability::SoftwareTrigger),
            "known absent capability is marked known");

    capabilities.set(CameraCapability::ExternalTrigger, true);
    require(capabilities.status(CameraCapability::ExternalTrigger) ==
                CapabilityStatus::Supported,
            "known present capability is Supported");
    require(capabilities.supports(CameraCapability::ExternalTrigger),
            "supports returns true only for Supported");

    require(capabilities.status(CameraCapability::SoftwareTrigger) ==
                CapabilityStatus::Unsupported,
            "setting one capability does not alter another capability");

    capabilities.set(CameraCapability::SoftwareTrigger, true);
    require(capabilities.supports(CameraCapability::SoftwareTrigger),
            "capability can be updated when a newer discovery snapshot arrives");

    capabilities.set(CameraCapability::GlobalShutter, true);
    require(capabilities.status(CameraCapability::GlobalShutter) ==
                CapabilityStatus::Supported,
            "global shutter is exposed as a standard camera capability");
    capabilities.set(CameraCapability::MonochromeSensor, false);
    require(capabilities.status(CameraCapability::MonochromeSensor) ==
                CapabilityStatus::Unsupported,
            "known color sensor is distinguishable from an unknown color mode");

    capabilities.set(CameraCapability::Count, true);
    require(capabilities.status(CameraCapability::Count) ==
                CapabilityStatus::Unknown,
            "sentinel is rejected without indexing beyond the bitset");
}

void testPersistableCameraModelCatalog() {
    using namespace camera;

    CameraCapabilities capabilities;
    capabilities.set(CameraCapability::GlobalShutter, true);
    capabilities.set(CameraCapability::MonochromeSensor, true);

    CameraSensorCharacteristics sensor;
    sensor.technology = CameraSensorTechnology::ScientificCMOS;
    sensor.shutterType = CameraShutterType::Global;
    sensor.colorMode = CameraColorMode::Monochrome;
    sensor.pixelPitchXMicrometers = 6.5;
    sensor.pixelPitchYMicrometers = 6.5;
    sensor.readNoiseElectronsRms = 1.1;
    sensor.darkCurrentElectronsPerPixelPerSecond = 0.02;
    sensor.fullWellCapacityElectrons = 30000.0;
    sensor.quantumEfficiency = {
        {450.0, 0.82}, {550.0, 0.91}, {650.0, 0.74}};
    sensor.provenance = CameraSpecificationProvenance::ManufacturerDatasheet;

    auto profile = CameraModelProfile::create(
        CameraModelKey{"Example Imaging", "SCMOS-2048", "rev-B"},
        capabilities,
        sensor);
    require(profile.has_value(),
            "creates a persistable camera model profile from stable manufacturer/model/variant identity");

    auto catalog = CameraModelCatalog::create({*profile});
    require(catalog.has_value(),
            "creates a typed model catalog with unique model identities");

    CameraIdentity identity;
    identity.manufacturer = " example imaging ";
    identity.model = "SCMOS-2048";
    identity.modelVariant = "REV-b";
    identity.serialNumber = "unit-serial-123";

    const auto* match = catalog->findExact(identity);
    require(match != nullptr,
            "matches exact model identity independently of case, surrounding whitespace and camera serial number");
    require(match && match->capabilities().supports(CameraCapability::GlobalShutter),
            "catalog exposes model-level capabilities for resolver ranking");
    require(match && match->sensorCharacteristics().quantumEfficiency.size() == 3,
            "catalog retains wavelength-dependent fluorescence sensor QE data");

    identity.modelVariant = "rev-C";
    require(catalog->findExact(identity) == nullptr,
            "does not apply a profile for a different hardware variant");
    identity.modelVariant.reset();
    require(catalog->findExact(identity) == nullptr,
            "does not guess a variant when the connected model does not identify it");

    CameraIdentity incompleteIdentity;
    incompleteIdentity.model = "SCMOS-2048";
    require(catalog->findExact(incompleteIdentity) == nullptr,
            "does not infer manufacturer identity when discovery omitted it");

    auto profileOtherVariant = CameraModelProfile::create(
        CameraModelKey{"Example Imaging", "SCMOS-2048", "rev-C"},
        capabilities,
        sensor,
        2,
        "datasheet:SCMOS-2048-rev-C");
    require(profileOtherVariant.has_value(),
            "allows distinct hardware variants of the same camera model");

    auto multiVariantCatalog = CameraModelCatalog::create(
        {*profile, *profileOtherVariant}, 4);
    require(multiVariantCatalog.has_value() &&
            multiVariantCatalog->revision() == 4,
            "catalog preserves its own persistent revision");
    CameraIdentity ambiguousIdentity;
    ambiguousIdentity.manufacturer = "Example Imaging";
    ambiguousIdentity.model = "SCMOS-2048";
    const auto candidates = multiVariantCatalog->findCandidates(ambiguousIdentity);
    require(candidates.size() == 2,
            "returns both matching model variants when runtime discovery omits variant identity");
    require(multiVariantCatalog->findExact(ambiguousIdentity) == nullptr,
            "does not silently choose an exact profile when its variant is unknown");

    auto genericProfile = CameraModelProfile::create(
        CameraModelKey{"Example Imaging", "SCMOS-2048", std::nullopt},
        capabilities,
        sensor);
    require(genericProfile.has_value(),
            "allows a generic profile where the manufacturer does not distinguish variants");
    auto genericAndSpecificCatalog = CameraModelCatalog::create(
        {*genericProfile, *profileOtherVariant}, 5);
    require(genericAndSpecificCatalog.has_value() &&
            genericAndSpecificCatalog->findExact(ambiguousIdentity) == nullptr,
            "does not use a generic profile when variant-specific profiles also exist");

    auto duplicateCatalog = CameraModelCatalog::create({*profile, *profile});
    require(!duplicateCatalog,
            "rejects duplicate stable model keys rather than using last-wins behavior");

    CameraSensorCharacteristics invalidSensor;
    invalidSensor.quantumEfficiency = {{550.0, 1.2}};
    auto invalidProfile = CameraModelProfile::create(
        CameraModelKey{"Example Imaging", "Invalid QE", std::nullopt},
        capabilities,
        invalidSensor);
    require(!invalidProfile,
            "rejects QE values outside the physical zero-to-one fraction");
}

void testBackendNeutralDiscoveryModel() {
    using camera::CameraDeviceInfo;
    using camera::ImageResolution;

    CameraDeviceInfo device;
    device.id = "opaque-backend-device-id";
    device.displayName = "Camera display label";
    device.previewResolutions.push_back(ImageResolution{640, 480});

    require(device.id == "opaque-backend-device-id",
            "discovery retains the backend opaque identifier");
    require(!device.identity.serialNumber.has_value(),
            "opaque device ID is not inferred to be a serial number");
    require(!device.identity.manufacturer.has_value(),
            "unknown manufacturer remains explicitly unknown");
    require(device.previewResolutions.size() == 1 &&
            device.previewResolutions.front().width == 640 &&
            device.previewResolutions.front().height == 480,
            "preview dimensions use the common resolution value type");
    require(device.capabilities.status(camera::CameraCapability::VideoAcquisition) ==
                camera::CapabilityStatus::Unknown,
            "missing model descriptor leaves capabilities unknown");

    device.identity.model = "MU503";
    device.identity.firmwareVersion = "firmware-reported-by-backend";
    require(device.identity.model.value() == "MU503",
            "model identity is independent of backend-specific model flags");
    require(device.identity.firmwareVersion.has_value(),
            "optional firmware identity can be populated by capable backends");

    CameraSensorCharacteristics sensor;
    sensor.technology = CameraSensorTechnology::ScientificCMOS;
    sensor.shutterType = CameraShutterType::Rolling;
    sensor.colorMode = CameraColorMode::Monochrome;
    sensor.activeSensorDimensions = ImageResolution{2048, 2048};
    sensor.pixelPitchXMicrometers = 6.5;
    sensor.pixelPitchYMicrometers = 6.5;
    sensor.readNoiseElectronsRms = 1.2;
    sensor.darkCurrentElectronsPerPixelPerSecond = 0.01;
    sensor.fullWellCapacityElectrons = 30000.0;
    sensor.maximumFrameRateFramesPerSecond = 100.0;
    sensor.quantumEfficiency = {
        CameraQuantumEfficiencySample{450.0, 0.82},
        CameraQuantumEfficiencySample{550.0, 0.91},
        CameraQuantumEfficiencySample{650.0, 0.74}};
    sensor.provenance = CameraSpecificationProvenance::ManufacturerDatasheet;
    device.sensorCharacteristics = sensor;

    require(device.sensorCharacteristics.has_value(),
            "model-level sensor characteristics can enrich runtime discovery");
    require(device.sensorCharacteristics->quantumEfficiency.size() == 3 &&
            device.sensorCharacteristics->quantumEfficiency[1].wavelengthNanometers == 550.0,
            "fluorescence QE remains a wavelength-dependent curve");
    require(device.sensorCharacteristics->shutterType ==
                CameraShutterType::Rolling,
            "rolling shutter is represented explicitly rather than inferred from global-shutter absence");
    require(device.sensorCharacteristics->provenance ==
                CameraSpecificationProvenance::ManufacturerDatasheet,
            "persisted camera model data retains its evidence provenance");
}

} // namespace

int main() {
    testCapabilityKnowledge();
    testPersistableCameraModelCatalog();
    testBackendNeutralDiscoveryModel();
    std::cout << "All common camera model tests passed.\n";
    return EXIT_SUCCESS;
}
