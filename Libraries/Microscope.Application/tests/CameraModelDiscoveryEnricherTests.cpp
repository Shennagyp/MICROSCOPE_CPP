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
    testIncompleteIdentityRequiresCandidateSelection();
    testLiveBackendConflictsAreRetainedAndReported();

    if (failures != 0) {
        std::cerr << failures << " camera discovery enrichment test(s) failed.\n";
        return 1;
    }

    std::cout << "All camera discovery enrichment tests passed.\n";
    return 0;
}
