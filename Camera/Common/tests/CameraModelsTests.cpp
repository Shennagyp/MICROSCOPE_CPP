#include "camera/CameraModels.h"

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
    testBackendNeutralDiscoveryModel();
    std::cout << "All common camera model tests passed.\n";
    return EXIT_SUCCESS;
}
