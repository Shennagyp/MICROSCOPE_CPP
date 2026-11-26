#include "microscope/application/persistence/CameraModelCatalogSqliteStore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using microscope::application::persistence::CameraModelCatalogSqliteStore;
using microscope::application::persistence::CameraModelCatalogStoreErrorCode;

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
        throw std::runtime_error("A camera catalog persistence fixture failed.");
    }
    return std::move(result).value();
}

camera::CameraModelCatalog makeCatalog(
    std::uint64_t catalogRevision,
    std::uint64_t profileRevision,
    bool globalShutter,
    double qeAt550) {
    camera::CameraCapabilities capabilities;
    capabilities.set(camera::CameraCapability::VideoAcquisition, true);
    capabilities.set(camera::CameraCapability::GlobalShutter, globalShutter);
    capabilities.set(camera::CameraCapability::MonochromeSensor, true);
    capabilities.set(camera::CameraCapability::ExternalTrigger, false);

    camera::CameraSensorCharacteristics sensor;
    sensor.technology = camera::CameraSensorTechnology::ScientificCMOS;
    sensor.shutterType = globalShutter
        ? camera::CameraShutterType::Global
        : camera::CameraShutterType::Rolling;
    sensor.colorMode = camera::CameraColorMode::Monochrome;
    sensor.activeSensorDimensions = camera::ImageResolution{2048, 2048};
    sensor.pixelPitchXMicrometers = 6.5;
    sensor.pixelPitchYMicrometers = 6.5;
    sensor.readNoiseElectronsRms = 0.0;
    sensor.darkCurrentElectronsPerPixelPerSecond = 0.0;
    sensor.fullWellCapacityElectrons = 30000.0;
    sensor.maximumFrameRateFramesPerSecond = 100.0;
    sensor.quantumEfficiency = {
        {400.0, 0.55},
        {450.0, 0.76},
        {550.0, qeAt550},
        {650.0, 0.72}};
    sensor.provenance = camera::CameraSpecificationProvenance::ManufacturerDatasheet;

    auto profile = take(camera::CameraModelProfile::create(
        camera::CameraModelKey{
            "Example Scientific Cameras",
            "SCMOS-2048",
            std::optional<std::string>{"Rev-B"}},
        capabilities,
        sensor,
        profileRevision,
        "manufacturer-datasheet:scmos-2048-rev-b"));

    return take(camera::CameraModelCatalog::create(
        {std::move(profile)}, catalogRevision));
}

void testCameraModelCatalogRoundTripAndOptimisticRevision() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto databasePath =
        std::filesystem::temp_directory_path() /
        ("microscope-camera-model-catalog-" + std::to_string(nonce) + ".sqlite");
    const auto path = databasePath.string();

    {
        auto opened = CameraModelCatalogSqliteStore::open(path);
        check(static_cast<bool>(opened),
              "opens a new camera model catalog database");
        if (!opened) {
            return;
        }
        auto store = std::move(opened).value();

        const auto initiallyEmpty = store->load();
        check(initiallyEmpty && !initiallyEmpty.value().has_value(),
              "reports an absent catalog before the first save");

        const auto firstCatalog = makeCatalog(1, 1, true, 0.91);
        const auto firstSave = store->save(firstCatalog, 0);
        check(firstSave.isSuccess(),
              "inserts the initial shared camera model catalog at revision one");

        const auto loaded = store->load();
        check(loaded && loaded.value().has_value(),
              "loads a persisted camera catalog");
        if (loaded && loaded.value()) {
            const auto& catalog = *loaded.value();
            check(catalog.revision() == 1,
                  "restores the durable catalog revision");

            camera::CameraIdentity identity;
            identity.manufacturer = "Example Scientific Cameras";
            identity.model = "SCMOS-2048";
            identity.modelVariant = "Rev-B";
            identity.serialNumber = "connected-unit-001";

            const auto* profile = catalog.findExact(identity);
            check(profile != nullptr,
                  "restores the model profile by manufacturer/model/variant rather than serial number");
            if (profile != nullptr) {
                check(profile->profileRevision() == 1,
                      "restores the model profile revision");
                check(profile->sourceReference() ==
                          "manufacturer-datasheet:scmos-2048-rev-b",
                      "restores the sensor evidence reference");
                check(profile->capabilities().supports(
                          camera::CameraCapability::GlobalShutter),
                      "restores the known global-shutter capability");
                check(profile->capabilities().status(
                          camera::CameraCapability::ExternalTrigger) ==
                          camera::CapabilityStatus::Unsupported,
                      "preserves known-unsupported capability separately from unknown");
                check(profile->capabilities().status(
                          camera::CameraCapability::FocusMotor) ==
                          camera::CapabilityStatus::Unknown,
                      "preserves unknown capability separately from unsupported");
                check(profile->sensorCharacteristics().readNoiseElectronsRms == 0.0,
                      "round-trips zero-valued read-noise measurements");
                check(profile->sensorCharacteristics().darkCurrentElectronsPerPixelPerSecond == 0.0,
                      "round-trips zero-valued dark-current measurements");
                check(profile->sensorCharacteristics().activeSensorDimensions.has_value() &&
                      profile->sensorCharacteristics().activeSensorDimensions->width == 2048 &&
                      profile->sensorCharacteristics().activeSensorDimensions->height == 2048,
                      "restores active sensor dimensions");
                check(profile->sensorCharacteristics().quantumEfficiency.size() == 4,
                      "restores the full QE response curve");
                check(profile->sensorCharacteristics().quantumEfficiency[2]
                          .quantumEfficiency == 0.91,
                      "restores QE sample values without loss");
            }
        }

        const auto staleCatalog = makeCatalog(2, 2, false, 0.80);
        const auto staleSave = store->save(staleCatalog, 0);
        check(!staleSave.isSuccess() &&
              staleSave.error().code ==
                  CameraModelCatalogStoreErrorCode::RevisionConflict,
              "rejects a stale writer instead of silently overwriting a newer catalog");

        const auto secondCatalog = makeCatalog(2, 2, false, 0.88);
        const auto secondSave = store->save(secondCatalog, 1);
        check(secondSave.isSuccess(),
              "commits a new catalog revision when the expected revision matches");

        const auto updated = store->load();
        check(updated && updated.value() &&
              updated.value()->revision() == 2,
              "publishes the new catalog revision after a committed save");
        if (updated && updated.value()) {
            camera::CameraIdentity identity;
            identity.manufacturer = "Example Scientific Cameras";
            identity.model = "SCMOS-2048";
            identity.modelVariant = "Rev-B";
            const auto* profile = updated.value()->findExact(identity);
            check(profile != nullptr &&
                  profile->sensorCharacteristics().quantumEfficiency[2]
                      .quantumEfficiency == 0.88,
                  "persists model-profile edits together with the catalog revision");
            check(profile != nullptr &&
                  profile->sensorCharacteristics().shutterType ==
                      camera::CameraShutterType::Rolling,
                  "persists a changed shutter characteristic");
        }
    }

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

} // namespace

int main() {
    testCameraModelCatalogRoundTripAndOptimisticRevision();

    if (failures != 0) {
        std::cerr << failures << " camera model catalog persistence test(s) failed.\n";
        return 1;
    }

    std::cout << "All camera model catalog persistence tests passed.\n";
    return 0;
}
