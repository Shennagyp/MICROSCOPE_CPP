#include "microscope/application/camera/CameraModelDiscoveryEnricher.h"

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace microscope::application::camera_support;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename TValue, typename TError>
TValue take(microscope::core::Result<TValue, TError> result) {
    if (!result) {
        throw std::runtime_error("A camera discovery enrichment fixture failed.");
    }
    return std::move(result).value();
}

camera::CameraModelProfile makeProfile(
    const char* variant,
    bool globalShutter,
    double readNoise) {
    camera::CameraCapabilities capabilities;
    capabilities.set(camera::CameraCapability::GlobalShutter, globalShutter);
    capabilities.set(camera::CameraCapability::MonochromeSensor, true);
    capabilities.set(camera::CameraCapability::ExternalTrigger, false);

    camera::CameraSensorCharacteristics sensor;
    sensor.technology = camera::CameraSensorTechnology::CMOS;
    sensor.shutterType = globalShutter
        ? camera::CameraShutterType::Global
        : camera::CameraShutterType::Rolling;
    sensor.colorMode = camera::CameraColorMode::Monochrome;
    sensor.activeSensorDimensions = camera::ImageResolution{2048, 2048};
    sensor.pixelPitchXMicrometers = 6.5;
    sensor.pixelPitchYMicrometers = 6.5;
    sensor.readNoiseElectronsRms = readNoise;
    sensor.darkCurrentElectronsPerPixelPerSecond = 0.02;
    sensor.fullWellCapacityElectrons = 30000.0;
    sensor.maximumFrameRateFramesPerSecond = 80.0;
    sensor.quantumEfficiency = {
        {450.0, 0.80}, {550.0, 0.91}, {650.0, 0.72}};
    sensor.provenance = camera::CameraSpecificationProvenance::ManufacturerDatasheet;

    return take(camera::CameraModelProfile::create(
        camera::CameraModelKey{
            "Example Imaging",
            "FluoroCam-2K",
            std::optional<std::string>{variant}},
        capabilities,
        sensor,
        2,
        "datasheet:fluorocam-2k"));
}

camera::CameraModelCatalog makeCatalog(
    std::vector<camera::CameraModelProfile> profiles) {
    return take(camera::CameraModelCatalog::create(std::move(profiles), 5));
}

camera::CameraDeviceInfo makeDiscoveredDevice() {
    camera::CameraDeviceInfo device;
    device.id = "sdk-enumeration-opaque-1";
    device.displayName = "FluoroCam-2K";
    device.identity.manufacturer = "Example Imaging";
    device.identity.model = "FluoroCam-2K";
    device.identity.modelVariant = "Rev-C";
    device.identity.serialNumber = "unit-serial-0001";
    device.capabilities.set(camera::CameraCapability::VideoAcquisition, true);

    camera::CameraSensorCharacteristics runtimeSensor;
    runtimeSensor.technology = camera::CameraSensorTechnology::CMOS;
    runtimeSensor.shutterType = camera::CameraShutterType::Unknown;
    runtimeSensor.colorMode = camera::CameraColorMode::Unknown;
    runtimeSensor.provenance =
        camera::CameraSpecificationProvenance::VendorModelDescriptor;
    device.sensorCharacteristics = runtimeSensor;
    return device;
}

bool hasWarning(
    const CameraModelEnrichmentResult& result,
    const char* code) {
    for (const auto& warning : result.warnings) {
        if (warning.code == code) {
            return true;
        }
    }
    return false;
}

void testExactProfileEnrichesMissingSensorFacts() {
    const auto catalog = makeCatalog({makeProfile("Rev-C", true, 1.2)});
    const auto discovered = makeDiscoveredDevice();

    const auto result = CameraModelDiscoveryEnricher::enrich(
        discovered, catalog);

    check(result.status == CameraModelEnrichmentStatus::ExactProfileApplied,
          "applies a persisted camera profile only for an exact model identity");
    check(result.deviceInfo.id == discovered.id &&
          result.deviceInfo.identity.serialNumber == discovered.identity.serialNumber,
          "retains the connected unit identity and serial number");
    check(result.deviceInfo.capabilities.supports(
              camera::CameraCapability::GlobalShutter),
          "enriches a capability unknown to the current backend result");
    check(result.deviceInfo.capabilities.status(
              camera::CameraCapability::ExternalTrigger) ==
              camera::CapabilityStatus::Unsupported,
          "retains known-unsupported profile capabilities");
    check(result.deviceInfo.capabilities.supports(
              camera::CameraCapability::VideoAcquisition),
          "does not overwrite an already known live capability");

    check(result.deviceInfo.sensorCharacteristics.has_value(),
          "retains merged sensor characteristics on the discovered device");
    if (result.deviceInfo.sensorCharacteristics) {
        const auto& sensor = *result.deviceInfo.sensorCharacteristics;
        check(sensor.technology == camera::CameraSensorTechnology::CMOS,
              "keeps the live vendor sensor technology over the catalog's generic sCMOS label");
        check(sensor.shutterType == camera::CameraShutterType::Global,
              "enriches shutter type from the exact persisted model profile");
        check(sensor.colorMode == camera::CameraColorMode::Monochrome,
              "enriches monochrome sensor identity");
        check(sensor.pixelPitchXMicrometers == 6.5 &&
              sensor.pixelPitchYMicrometers == 6.5,
              "enriches independently stored X/Y pixel pitches");
        check(sensor.readNoiseElectronsRms == 1.2,
              "enriches read-noise characterization");
        check(sensor.quantumEfficiency.size() == 3 &&
              sensor.quantumEfficiency[1].quantumEfficiency == 0.91,
              "enriches the wavelength-dependent QE curve");
        check(sensor.provenance ==
                  camera::CameraSpecificationProvenance::ManufacturerDatasheet,
              "records the source of profile-enriched sensor characteristics");
        check(sensor.provenanceReference ==
                  "datasheet:fluorocam-2k",
              "retains the camera model datasheet reference");
    }

    const auto projected =
        CameraModelDiscoveryEnricher::toCoreSensorCharacteristics(result.deviceInfo);
    check(projected.has_value(),
          "projects enriched sensor facts into the persistence-free Core value type");
    if (projected) {
        check(projected->shutterType == microscope::core::CameraShutterType::Global,
              "projects the selected global shutter fact into Core");
        check(projected->colorMode == microscope::core::CameraSensorColorMode::Monochrome,
              "projects monochrome sensor identity into Core");
        check(projected->quantumEfficiency.size() == 3 &&
              projected->quantumEfficiency[1].quantumEfficiency == 0.91,
              "projects the wavelength-dependent QE curve into Core");
        check(projected->provenanceReference ==
                  "datasheet:fluorocam-2k",
              "preserves the catalog provenance reference across the Core boundary");
    }
}

void testBoundDeviceUsesExactPersistentProfileAndSuppliesMissingIdentity() {
    const auto catalog = makeCatalog({makeProfile("Rev-C", true, 1.2)});
    auto discovered = makeDiscoveredDevice();
    discovered.identity.manufacturer.reset();
    discovered.identity.model.reset();
    discovered.identity.modelVariant.reset();
    const auto componentId =
        take<microscope::core::EquipmentComponentId>(
            microscope::core::EquipmentComponentId::create("bound-camera-1"));
    const camera::CameraModelKey modelKey{
        "Example Imaging",
        "FluoroCam-2K",
        std::optional<std::string>{"Rev-C"}};

    const auto result = CameraModelDiscoveryEnricher::enrichBoundDevice(
        discovered, catalog, componentId, modelKey, 2);

    check(result.status == CameraModelEnrichmentStatus::ExactProfileApplied,
          "applies the exact persisted model profile for the selected installed camera component");
    check(result.equipmentComponentId == componentId,
          "retains the installed equipment component identity through enrichment");
    check(result.deviceInfo.identity.manufacturer ==
              std::optional<std::string>{"Example Imaging"} &&
          result.deviceInfo.identity.model ==
              std::optional<std::string>{"FluoroCam-2K"} &&
          result.deviceInfo.identity.modelVariant ==
              std::optional<std::string>{"Rev-C"},
          "fills missing runtime manufacturer/model/variant identity from the explicit equipment binding");
    check(result.deviceInfo.identity.serialNumber ==
              discovered.identity.serialNumber,
          "retains the connected device serial as runtime identity without making it the model key");
    check(result.deviceInfo.sensorCharacteristics &&
          result.deviceInfo.sensorCharacteristics->quantumEfficiency.size() == 4,
          "enriches sensor characteristics from the bound exact model profile");
}

void testBoundDeviceRejectsRuntimeIdentityAndProfileRevisionConflicts() {
    const auto catalog = makeCatalog({makeProfile("Rev-C", true, 1.2)});
    const auto componentId =
        take<microscope::core::EquipmentComponentId>(
            microscope::core::EquipmentComponentId::create("bound-camera-2"));
    const camera::CameraModelKey modelKey{
        "Example Imaging",
        "FluoroCam-2K",
        std::optional<std::string>{"Rev-C"}};

    auto conflictingDevice = makeDiscoveredDevice();
    conflictingDevice.identity.model = "Different-Camera-Model";
    const auto identityConflict = CameraModelDiscoveryEnricher::enrichBoundDevice(
        conflictingDevice, catalog, componentId, modelKey, 2);
    check(identityConflict.status ==
              CameraModelEnrichmentStatus::BoundProfileIdentityMismatch,
          "does not apply a persisted model binding to a conflicting live device identity");
    check(!identityConflict.deviceInfo.sensorCharacteristics ||
          !identityConflict.deviceInfo.sensorCharacteristics->readNoiseElectronsRms,
          "does not merge the bound model sensor profile after an identity conflict");

    const auto staleProfile = CameraModelDiscoveryEnricher::enrichBoundDevice(
        makeDiscoveredDevice(), catalog, componentId, modelKey, 1);
    check(staleProfile.status ==
              CameraModelEnrichmentStatus::BoundProfileRevisionMismatch,
          "does not apply a stale persisted model-profile revision");
    check(!staleProfile.deviceInfo.sensorCharacteristics ||
          !staleProfile.deviceInfo.sensorCharacteristics->readNoiseElectronsRms,
          "does not enrich sensor data when the bound profile revision is stale");
}

void testCoreCameraSpecificationProjectionFillsSensorFactsAndRetainsInstalledGeometry() {
    const auto catalog = makeCatalog({makeProfile("Rev-C", true, 1.2)});
    auto discovered = makeDiscoveredDevice();
    discovered.identity.manufacturer.reset();
    discovered.identity.model.reset();
    discovered.identity.modelVariant.reset();

    const auto componentId =
        take<microscope::core::EquipmentComponentId>(
            microscope::core::EquipmentComponentId::create("installed-camera-1"));
    const auto enriched = CameraModelDiscoveryEnricher::enrichBoundDevice(
        discovered,
        catalog,
        componentId,
        camera::CameraModelKey{
            "Example Imaging", "FluoroCam-2K",
            std::optional<std::string>{"Rev-C"}},
        2);
    const auto activeDimensions =
        take(microscope::core::PixelDimensions::create(1024, 768));
    const auto cameraMode = take(microscope::core::CameraOutputMode::create(
        take<microscope::core::CameraModeId>(
            microscope::core::CameraModeId::create("installed-camera-mono8")),
        take(microscope::core::SensorRegionOfInterest::create(
            0, 0, 1024, 768)),
        take(microscope::core::PixelBinning::create(1, 1)),
        activeDimensions,
        microscope::core::CameraPixelLayout::Monochrome,
        "Mono8"));
    const auto installedSpecification = take(
        microscope::core::CameraSpecification::create(
            componentId,
            activeDimensions,
            take(microscope::core::PositiveMicrometers::create(5.0)),
            take(microscope::core::PositiveMicrometers::create(5.0)),
            {cameraMode}));

    const auto projection =
        CameraModelDiscoveryEnricher::projectToCoreCameraSpecification(
            installedSpecification, enriched);

    check(static_cast<bool>(projection),
          "creates a Core camera specification from the installed spec and exact model discovery");
    if (!projection) {
        return;
    }

    const auto& projected = projection.value().specification;
    check(projected.componentId() == componentId,
          "retains the installed camera component identity");
    check(projected.activeSensorDimensions().width() == 1024 &&
          projected.activeSensorDimensions().height() == 768,
          "does not silently replace equipment-owned active sensor dimensions");
    check(projected.pixelPitchXMicrometers() == 5.0 &&
          projected.pixelPitchYMicrometers() == 5.0,
          "does not silently replace equipment-owned pixel pitch");
    check(projected.outputModes().size() == 1 &&
          projected.outputModes().front().id() ==
              take<microscope::core::CameraModeId>(
                  microscope::core::CameraModeId::create("installed-camera-mono8")),
          "preserves the installed camera output-mode definition");
    check(projected.sensorCharacteristics().has_value() &&
          projected.sensorCharacteristics()->quantumEfficiency.size() == 4,
          "projects the model catalog's wavelength-dependent QE curve into Core");

    bool dimensionsWarning = false;
    bool pitchWarning = false;
    for (const auto& warning : projection.value().warnings) {
        dimensionsWarning |=
            warning.code == "camera.model_profile.active_sensor_dimensions_mismatch";
        pitchWarning |=
            warning.code == "camera.model_profile.pixel_pitch_x_mismatch" ||
            warning.code == "camera.model_profile.pixel_pitch_y_mismatch";
    }
    check(dimensionsWarning && pitchWarning,
          "reports catalog/equipment geometry disagreements without overwriting installed values");
}

void testIncompleteIdentityRequiresCandidateSelection() {
    const auto catalog = makeCatalog({
        makeProfile("Rev-B", true, 1.2),
        makeProfile("Rev-C", false, 0.9)});
    auto discovered = makeDiscoveredDevice();
    discovered.identity.modelVariant.reset();

    const auto result = CameraModelDiscoveryEnricher::enrich(
        discovered, catalog);

    check(result.status ==
              CameraModelEnrichmentStatus::CandidateSelectionRequired,
          "does not silently choose a profile when model variant is missing");
    check(result.candidateProfiles.size() == 2,
          "returns both matching model variants for explicit resolution");
    check(!result.deviceInfo.sensorCharacteristics ||
          !result.deviceInfo.sensorCharacteristics->readNoiseElectronsRms,
          "does not merge an ambiguous candidate's sensor characteristics");
}

void testCoreCameraProjectionRejectsDifferentInstalledComponentIdentity() {
    const auto catalog = makeCatalog({makeProfile("Rev-C", true, 1.2)});
    const auto discovered = makeDiscoveredDevice();
    const auto boundComponentId =
        take<microscope::core::EquipmentComponentId>(
            microscope::core::EquipmentComponentId::create("camera-bound-to-profile"));
    const auto enrichment = CameraModelDiscoveryEnricher::enrichBoundDevice(
        discovered,
        catalog,
        boundComponentId,
        camera::CameraModelKey{
            "Example Imaging", "FluoroCam-2K",
            std::optional<std::string>{"Rev-C"}},
        2);

    const auto dimensions =
        take(microscope::core::PixelDimensions::create(2048, 2048));
    const auto mode = take(microscope::core::CameraOutputMode::create(
        take<microscope::core::CameraModeId>(
            microscope::core::CameraModeId::create("unrelated-camera-mode")),
        take(microscope::core::SensorRegionOfInterest::create(0, 0, 2048, 2048)),
        take(microscope::core::PixelBinning::create(1, 1)),
        dimensions,
        microscope::core::CameraPixelLayout::Monochrome,
        "Mono8"));
    const auto anotherInstalledCamera = take(
        microscope::core::CameraSpecification::create(
            take<microscope::core::EquipmentComponentId>(
                microscope::core::EquipmentComponentId::create("different-installed-camera")),
            dimensions,
            take(microscope::core::PositiveMicrometers::create(6.5)),
            take(microscope::core::PositiveMicrometers::create(6.5)),
            {mode}));

    const auto projection =
        CameraModelDiscoveryEnricher::projectToCoreCameraSpecification(
            anotherInstalledCamera, enrichment);
    check(!projection &&
          projection.error().code ==
              CameraSpecificationProjectionErrorCode::EquipmentComponentMismatch,
          "does not attach one camera component's persisted model binding to another installed camera specification");
}

void testProjectionRejectsAmbiguousModelIdentity() {
    const auto catalog = makeCatalog({
        makeProfile("Rev-B", true, 1.2),
        makeProfile("Rev-C", false, 0.9)});
    auto discovered = makeDiscoveredDevice();
    discovered.identity.modelVariant.reset();
    const auto enrichment =
        CameraModelDiscoveryEnricher::enrich(discovered, catalog);

    const auto activeDimensions =
        take(microscope::core::PixelDimensions::create(1024, 768));
    const auto cameraMode = take(microscope::core::CameraOutputMode::create(
        take<microscope::core::CameraModeId>(
            microscope::core::CameraModeId::create("ambiguous-camera-mono8")),
        take(microscope::core::SensorRegionOfInterest::create(0, 0, 1024, 768)),
        take(microscope::core::PixelBinning::create(1, 1)),
        activeDimensions,
        microscope::core::CameraPixelLayout::Monochrome,
        "Mono8"));
    const auto installedSpecification = take(
        microscope::core::CameraSpecification::create(
            take<microscope::core::EquipmentComponentId>(
                microscope::core::EquipmentComponentId::create("ambiguous-camera")),
            activeDimensions,
            take(microscope::core::PositiveMicrometers::create(5.0)),
            take(microscope::core::PositiveMicrometers::create(5.0)),
            {cameraMode}));

    const auto projection =
        CameraModelDiscoveryEnricher::projectToCoreCameraSpecification(
            installedSpecification, enrichment);
    check(!projection &&
          projection.error().code ==
              CameraSpecificationProjectionErrorCode::ModelProfileNotResolved,
          "does not project a camera model profile while variant selection is ambiguous");
}

void testLiveBackendConflictsAreRetainedAndReported() {
    const auto catalog = makeCatalog({makeProfile("Rev-C", true, 1.2)});
    auto discovered = makeDiscoveredDevice();
    discovered.capabilities.set(camera::CameraCapability::GlobalShutter, false);

    const auto result = CameraModelDiscoveryEnricher::enrich(
        discovered, catalog);

    check(result.status ==
              CameraModelEnrichmentStatus::ExactProfileAppliedWithConflicts,
          "marks an exact profile match that conflicts with live backend facts");
    check(result.deviceInfo.capabilities.status(
              camera::CameraCapability::GlobalShutter) ==
              camera::CapabilityStatus::Unsupported,
          "retains the connected backend's explicit capability report");
    check(hasWarning(
              result, "camera.model_profile.capability_conflict"),
          "returns a capability conflict warning");
    check(hasWarning(
              result, "camera.model_profile.shutter_conflict"),
          "returns a separate sensor shutter conflict warning");
    if (result.deviceInfo.sensorCharacteristics) {
        check(!result.deviceInfo.sensorCharacteristics->shutterType.has_value(),
              "does not silently project the conflicting persisted shutter type");
    }
}

} // namespace

int main() {
    testExactProfileEnrichesMissingSensorFacts();
    testBoundDeviceUsesExactPersistentProfileAndSuppliesMissingIdentity();
    testBoundDeviceRejectsRuntimeIdentityAndProfileRevisionConflicts();
    testCoreCameraSpecificationProjectionFillsSensorFactsAndRetainsInstalledGeometry();
    testIncompleteIdentityRequiresCandidateSelection();
    testProjectionRejectsAmbiguousModelIdentity();
    testCoreCameraProjectionRejectsDifferentInstalledComponentIdentity();
    testLiveBackendConflictsAreRetainedAndReported();

    if (failures != 0) {
        std::cerr << failures << " camera discovery enrichment test(s) failed.\n";
        return 1;
    }

    std::cout << "All camera discovery enrichment tests passed.\n";
    return 0;
}
