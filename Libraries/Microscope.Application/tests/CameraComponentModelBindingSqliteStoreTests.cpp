#include "microscope/application/persistence/CameraComponentModelBindingSqliteStore.h"
#include "microscope/application/camera/InstalledCameraModelEnricher.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
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
        if constexpr (std::is_enum_v<TError>) {
            throw std::runtime_error(
                "A camera component model binding fixture failed; error type=" +
                std::string(typeid(TError).name()) +
                ", code=" + std::to_string(static_cast<int>(result.error())));
        }
        throw std::runtime_error("A camera component model binding fixture failed.");
    }
    return std::move(result).value();
}

template <typename TValue>
TValue take(std::optional<TValue> result) {
    if (!result) {
        throw std::runtime_error("An optional camera test fixture was empty.");
    }
    return std::move(*result);
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
    std::uint64_t equipmentRevision,
    bool includeCameraSpecification = true) {
    using namespace microscope::core;

    const auto microscopeId = makeId<MicroscopeId>("binding-test-microscope");
    const auto objectiveId = makeId<EquipmentComponentId>("objective-main");
    const auto cameraId = makeId<EquipmentComponentId>("camera-main");
    const auto objectiveInput =
        makeId<OpticalPortId>("objective-main-input");
    const auto objectiveOutput =
        makeId<OpticalPortId>("objective-main-output");
    const auto cameraInput = makeId<OpticalPortId>("camera-main-input");

    const auto objectiveComponent = take(EquipmentComponent::create(
        objectiveId,
        EquipmentComponentKind::OpticalElement,
        {take(OpticalPort::create(
             objectiveInput, OpticalPortDirection::Input)),
         take(OpticalPort::create(
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

    std::vector<OpticalPath> opticalPaths;
    std::vector<LightPath> lightPaths;
    std::vector<CameraSpecification> cameraSpecifications;
    if (includeCameraSpecification) {
        const auto imagingPathId =
            makeId<OpticalPathId>("camera-main-imaging-path");
        opticalPaths.push_back(take(OpticalPath::create(
            imagingPathId,
            {take(OpticalPathStep::create(
                 objectiveId, std::nullopt, objectiveOutput)),
             take(OpticalPathStep::create(
                 cameraId, cameraInput, std::nullopt))},
            OpticalPathRole::Imaging)));
        lightPaths.push_back(take(LightPath::create(
            makeId<LightPathId>("camera-main-light-path"),
            "Camera main",
            {imagingPathId})));
        cameraSpecifications.push_back(cameraSpecification);
    }

    return take(MicroscopeEquipmentDefinition::create(
        microscopeId,
        {objectiveComponent, cameraComponent},
        {connection},
        std::move(opticalPaths),
        {},
        std::move(cameraSpecifications),
        {},
        {},
        {},
        {},
        std::move(lightPaths),
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

camera::CameraDeviceInfo makeDiscoveredCameraDevice() {
    camera::CameraDeviceInfo device;
    device.id = "runtime-sdk-camera-handle-42";
    device.displayName = "FluoroCam-2048";
    device.identity.manufacturer = "Example Imaging";
    device.identity.model = "FluoroCam-2048";
    device.identity.modelVariant = "Rev-C";
    device.identity.serialNumber = "serial-camera-42";
    device.capabilities.set(camera::CameraCapability::VideoAcquisition, true);
    return device;
}

void testCameraComponentModelBindingRejectsInvalidEquipmentAndModelKeys() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto databasePath =
        std::filesystem::temp_directory_path() /
        ("microscope-camera-model-binding-invalid-" + std::to_string(nonce) + ".sqlite");
    const auto path = databasePath.string();

    {
        auto opened = CameraComponentModelBindingSqliteStore::open(path);
        check(static_cast<bool>(opened),
              "opens store for camera binding validation tests");
        if (!opened) {
            return;
        }
        auto store = std::move(opened).value();
        const auto equipment = makeEquipment(3);
        const auto catalog = makeCatalog(1, 1);

        const auto missingComponent = makeBinding(
            makeId<microscope::core::EquipmentComponentId>("not-installed"), 1);
        const auto missingResult = store->replaceBindings(
            equipment, catalog, {missingComponent}, 0, 1);
        check(!missingResult.isSuccess() &&
              missingResult.error().code ==
                  CameraComponentModelBindingStoreErrorCode::CameraComponentNotInstalled,
              "rejects bindings to components absent from the equipment constitution");

        const auto wrongKind = makeBinding(
            makeId<microscope::core::EquipmentComponentId>("objective-main"), 1);
        const auto wrongKindResult = store->replaceBindings(
            equipment, catalog, {wrongKind}, 0, 1);
        check(!wrongKindResult.isSuccess() &&
              wrongKindResult.error().code ==
                  CameraComponentModelBindingStoreErrorCode::CameraComponentWrongKind,
              "rejects model bindings on non-imaging equipment components");

        auto emptyVariant = makeBinding(
            makeId<microscope::core::EquipmentComponentId>("camera-main"), 1);
        emptyVariant.modelKey.variant = std::string{};
        const auto emptyVariantResult = store->replaceBindings(
            equipment, catalog, {emptyVariant}, 0, 1);
        check(!emptyVariantResult.isSuccess() &&
              emptyVariantResult.error().code ==
                  CameraComponentModelBindingStoreErrorCode::InvalidModelKey,
              "requires an absent variant to be represented as null rather than an empty string");

        const auto equipmentWithoutCameraSpecification = makeEquipment(4, false);
        const auto noSpecificationResult = store->replaceBindings(
            equipmentWithoutCameraSpecification,
            catalog,
            {makeBinding(
                makeId<microscope::core::EquipmentComponentId>("camera-main"), 1)},
            0,
            1);
        check(!noSpecificationResult.isSuccess() &&
              noSpecificationResult.error().code ==
                  CameraComponentModelBindingStoreErrorCode::CameraSpecificationMissing,
              "rejects model binding where an ImagingDevice has no CameraSpecification");
    }

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
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
        check(initial && initial.value().microscopeId ==
                  std::optional<microscope::core::MicroscopeId>{equipment.microscopeId()} &&
              initial.value().equipmentDefinitionRevision == equipment.revision(),
              "tags even an empty binding set with its microscope and equipment revision");

        const auto saved = store->replaceBindings(
            equipment, catalog1, {binding}, 0, 1);
        check(saved.isSuccess(),
              "persists a camera component's exact manufacturer/model/variant profile binding");

        const auto loaded = store->loadBindings(equipment, catalog1);
        if (loaded) {
            const auto composed =
                microscope::application::camera_support::InstalledCameraModelEnricher::enrich(
                    equipment,
                    catalog1,
                    loaded.value(),
                    makeId<microscope::core::LightPathId>("camera-main-light-path"),
                    makeId<microscope::core::EquipmentComponentId>("camera-main"),
                    makeDiscoveredCameraDevice());
            check(static_cast<bool>(composed),
                  "composes the loaded component binding with live discovery and Core projection");
            if (composed) {
                check(composed.value().microscopeId == equipment.microscopeId() &&
                      composed.value().equipmentDefinitionRevision == 7 &&
                      composed.value().bindingSetRevision == 1,
                      "returns microscope, equipment and binding revision provenance");
                check(composed.value().cameraComponentId ==
                          makeId<microscope::core::EquipmentComponentId>("camera-main") &&
                      composed.value().modelProfileRevision == 1,
                      "returns the selected LightPath camera component and exact model profile revision");
                check(composed.value().cameraSpecification.sensorCharacteristics().has_value() &&
                      composed.value().cameraSpecification.sensorCharacteristics()->quantumEfficiency.size() == 3,
                      "returns the Core camera specification enriched from the persisted profile");
            }

            const auto wrongEndpoint =
                microscope::application::camera_support::InstalledCameraModelEnricher::enrich(
                    equipment,
                    catalog1,
                    loaded.value(),
                    makeId<microscope::core::LightPathId>("camera-main-light-path"),
                    makeId<microscope::core::EquipmentComponentId>("objective-main"),
                    makeDiscoveredCameraDevice());
            check(!wrongEndpoint &&
                  wrongEndpoint.error().code ==
                      microscope::application::camera_support::InstalledCameraModelEnrichmentErrorCode::CameraNotConnectedToLightPath,
                  "rejects applying a camera model binding to a non-camera component on the selected route");

            const auto newerEquipment = makeEquipment(8);
            const auto staleBindingSetUse =
                microscope::application::camera_support::InstalledCameraModelEnricher::enrich(
                    newerEquipment,
                    catalog1,
                    loaded.value(),
                    makeId<microscope::core::LightPathId>("camera-main-light-path"),
                    makeId<microscope::core::EquipmentComponentId>("camera-main"),
                    makeDiscoveredCameraDevice());
            check(!staleBindingSetUse &&
                  staleBindingSetUse.error().code ==
                      microscope::application::camera_support::InstalledCameraModelEnrichmentErrorCode::BindingSetEquipmentRevisionMismatch,
                  "does not reuse a cached camera binding set against another equipment revision");
        }
        check(loaded && loaded.value().bindingRevision == 1,
              "restores the binding-set revision for this equipment revision");
        check(loaded && loaded.value().microscopeId ==
                  std::optional<microscope::core::MicroscopeId>{equipment.microscopeId()} &&
              loaded.value().equipmentDefinitionRevision == 7,
              "returns binding-set scope metadata alongside its entries");
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
              noBindingsForNewEquipmentRevision.value().bindings.empty() &&
              noBindingsForNewEquipmentRevision.value().equipmentDefinitionRevision == 8,
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
    testCameraComponentModelBindingRejectsInvalidEquipmentAndModelKeys();
    testCameraComponentModelBindingRoundTripAndRevisionControl();

    if (failures != 0) {
        std::cerr << failures << " camera component model binding test(s) failed.\n";
        return 1;
    }

    std::cout << "All camera component model binding tests passed.\n";
    return 0;
}
