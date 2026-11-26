#include "microscope/application/persistence/CameraComponentModelBindingSqliteStore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace microscope::application::persistence;

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
        throw std::runtime_error("A camera component model binding fixture failed.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

camera::CameraModelProfile makeProfile(std::uint64_t profileRevision) {
    camera::CameraCapabilities capabilities;
    capabilities.set(camera::CameraCapability::VideoAcquisition, true);
    capabilities.set(camera::CameraCapability::GlobalShutter, true);
    capabilities.set(camera::CameraCapability::MonochromeSensor, true);

    camera::CameraSensorCharacteristics sensor;
    sensor.technology = camera::CameraSensorTechnology::CMOS;
    sensor.shutterType = camera::CameraShutterType::Global;
    sensor.colorMode = camera::CameraColorMode::Monochrome;
    sensor.activeSensorDimensions = camera::ImageResolution{2048, 2048};
    sensor.pixelPitchXMicrometers = 6.5;
    sensor.pixelPitchYMicrometers = 6.5;
    sensor.readNoiseElectronsRms = 1.2;
    sensor.quantumEfficiency = {{450.0, 0.8}, {550.0, 0.9}, {650.0, 0.7}};
    sensor.provenance = camera::CameraSpecificationProvenance::ManufacturerDatasheet;

    return take(camera::CameraModelProfile::create(
        camera::CameraModelKey{
            "Example Imaging",
            "FluoroCam-2048",
            std::optional<std::string>{"Rev-C"}},
        capabilities,
        sensor,
        profileRevision,
        "datasheet:fluorocam-2048-rev-c"));
}

camera::CameraModelCatalog makeCatalog(std::uint64_t catalogRevision,
                                       std::uint64_t profileRevision) {
    return take(camera::CameraModelCatalog::create(
        {makeProfile(profileRevision)}, catalogRevision));
}

microscope::core::MicroscopeEquipmentDefinition makeEquipment(
    std::uint64_t equipmentRevision) {
    using namespace microscope::core;

    const auto microscopeId = makeId<MicroscopeId>("binding-test-microscope");
    const auto objectiveId = makeId<EquipmentComponentId>("objective-main");
    const auto cameraId = makeId<EquipmentComponentId>("camera-main");
    const auto objectiveOutput =
        makeId<OpticalPortId>("objective-main-output");
    const auto cameraInput = makeId<OpticalPortId>("camera-main-input");

    const auto objectiveComponent = take(EquipmentComponent::create(
        objectiveId,
        EquipmentComponentKind::OpticalElement,
        {take(OpticalPort::create(
            objectiveOutput, OpticalPortDirection::Output))}));
    const auto cameraComponent = take(EquipmentComponent::create(
        cameraId,
        EquipmentComponentKind::ImagingDevice,
        {take(OpticalPort::create(
            cameraInput, OpticalPortDirection::Input))}));
    const auto connection = take(OpticalConnection::create(
        makeId<OpticalConnectionId>("objective-to-camera"),
        objectiveOutput,
        cameraInput));

    const auto cameraMode = take(CameraOutputMode::create(
        makeId<CameraModeId>("camera-main-mono8"),
        take(SensorRegionOfInterest::create(0, 0, 2048, 2048)),
        take(PixelBinning::create(1, 1)),
        take(PixelDimensions::create(2048, 2048)),
        CameraPixelLayout::Monochrome,
        "Mono8"));
    const auto cameraSpecification = take(CameraSpecification::create(
        cameraId,
        take(PixelDimensions::create(2048, 2048)),
        take(PositiveMicrometers::create(6.5)),
        take(PositiveMicrometers::create(6.5)),
        {cameraMode}));

    const auto imagingPathId = makeId<OpticalPathId>("camera-main-imaging-path");
    const auto imagingPath = take(OpticalPath::create(
        imagingPathId,
        {take(OpticalPathStep::create(
             objectiveId, std::nullopt, objectiveOutput)),
         take(OpticalPathStep::create(
             cameraId, cameraInput, std::nullopt))},
        OpticalPathRole::Imaging));
    const auto lightPath = take(LightPath::create(
        makeId<LightPathId>("camera-main-light-path"),
        "Camera main",
        {imagingPathId}));

    return take(MicroscopeEquipmentDefinition::create(
        microscopeId,
        {objectiveComponent, cameraComponent},
        {connection},
        {imagingPath},
        {},
        {cameraSpecification},
        {},
        {},
        {},
        {},
        {lightPath},
        {},
        equipmentRevision));
}

CameraComponentModelBinding makeBinding(
    const microscope::core::EquipmentComponentId& cameraComponentId,
    std::uint64_t profileRevision) {
    return CameraComponentModelBinding{
        cameraComponentId,
        camera::CameraModelKey{
            "Example Imaging",
            "FluoroCam-2048",
            std::optional<std::string>{"Rev-C"}},
        profileRevision};
}

void testCameraComponentModelBindingRoundTripAndRevisionControl() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto databasePath =
        std::filesystem::temp_directory_path() /
        ("microscope-camera-model-binding-" + std::to_string(nonce) + ".sqlite");
    const auto path = databasePath.string();

    {
        auto opened = CameraComponentModelBindingSqliteStore::open(path);
        check(static_cast<bool>(opened),
              "opens the camera component/model binding database");
        if (!opened) {
            return;
        }
        auto store = std::move(opened).value();

        const auto equipment = makeEquipment(7);
        const auto catalog1 = makeCatalog(1, 1);
        const auto binding = makeBinding(
            makeId<microscope::core::EquipmentComponentId>("camera-main"), 1);

        const auto initial = store->loadBindings(equipment, catalog1);
        check(initial && initial.value().bindingRevision == 0 &&
              initial.value().bindings.empty(),
              "returns an empty binding set before the first save");

        const auto saved = store->replaceBindings(
            equipment, catalog1, {binding}, 0, 1);
        check(saved.isSuccess(),
              "persists a camera component's exact manufacturer/model/variant profile binding");

        const auto loaded = store->loadBindings(equipment, catalog1);
        check(loaded && loaded.value().bindingRevision == 1,
              "restores the binding-set revision for this equipment revision");
        check(loaded && loaded.value().bindings.size() == 1,
              "restores one installed camera component binding");
        if (loaded && !loaded.value().bindings.empty()) {
            const auto& restored = loaded.value().bindings.front();
            check(restored.cameraComponentId ==
                      makeId<microscope::core::EquipmentComponentId>("camera-main"),
                  "restores the installed equipment component identity");
            check(restored.modelKey.manufacturer == "Example Imaging" &&
                  restored.modelKey.model == "FluoroCam-2048" &&
                  restored.modelKey.variant ==
                      std::optional<std::string>{"Rev-C"},
                  "restores exact camera model identity including variant");
            check(restored.modelProfileRevision == 1,
                  "restores the exact shared model profile revision");
        }

        const auto staleWriter = store->replaceBindings(
            equipment, catalog1, {binding}, 0, 1);
        check(!staleWriter.isSuccess() &&
              staleWriter.error().code ==
                  CameraComponentModelBindingStoreErrorCode::RevisionConflict,
              "rejects a stale binding-set writer");

        const auto catalog2 = makeCatalog(2, 2);
        const auto staleCatalogLoad = store->loadBindings(equipment, catalog2);
        check(!staleCatalogLoad &&
              staleCatalogLoad.error().code ==
                  CameraComponentModelBindingStoreErrorCode::CatalogProfileRevisionMismatch,
              "detects when the currently loaded catalog profile revision differs from the persisted binding");

        const auto updatedBinding = makeBinding(
            makeId<microscope::core::EquipmentComponentId>("camera-main"), 2);
        const auto updated = store->replaceBindings(
            equipment, catalog2, {updatedBinding}, 1, 2);
        check(updated.isSuccess(),
              "updates the component binding only after explicitly accepting the new catalog profile revision");

        const auto updatedLoaded = store->loadBindings(equipment, catalog2);
        check(updatedLoaded && updatedLoaded.value().bindingRevision == 2 &&
              updatedLoaded.value().bindings.front().modelProfileRevision == 2,
              "loads the newly accepted binding/profile revision");

        const auto newerEquipment = makeEquipment(8);
        const auto noBindingsForNewEquipmentRevision =
            store->loadBindings(newerEquipment, catalog2);
        check(noBindingsForNewEquipmentRevision &&
              noBindingsForNewEquipmentRevision.value().bindingRevision == 0 &&
              noBindingsForNewEquipmentRevision.value().bindings.empty(),
              "does not silently carry an old camera binding into a new equipment definition revision");

        const auto duplicate = store->replaceBindings(
            equipment, catalog2, {updatedBinding, updatedBinding}, 2, 3);
        check(!duplicate.isSuccess() &&
              duplicate.error().code ==
                  CameraComponentModelBindingStoreErrorCode::DuplicateCameraComponent,
              "rejects duplicate camera component bindings within one equipment revision");
    }

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

} // namespace

int main() {
    testCameraComponentModelBindingRoundTripAndRevisionControl();

    if (failures != 0) {
        std::cerr << failures << " camera component model binding test(s) failed.\n";
        return 1;
    }

    std::cout << "All camera component model binding tests passed.\n";
    return 0;
}
