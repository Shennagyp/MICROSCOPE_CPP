#include "microscope/application/persistence/MicroscopeConfigurationSqliteStore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace microscope::application::persistence;
using namespace microscope::core;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename TValue, typename TError>
TValue take(Result<TValue, TError> result) {
    if (!result) {
        throw std::runtime_error("A microscope configuration persistence fixture failed.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

LightPathCalibration makeCalibration(
    const char* calibrationRevision,
    const char* modeId,
    std::uint64_t modeRevision,
    const char* lightPathId,
    const char* neutralDensityFilterId) {
    return take(LightPathCalibration::create(
        makeId<CalibrationRevisionId>(calibrationRevision),
        makeId<MicroscopeId>("persisted-microscope"),
        5,
        makeId<OperationModeId>(modeId),
        modeRevision,
        makeId<LightPathId>(lightPathId),
        makeId<EquipmentComponentId>("transmitted-led"),
        makeId<IlluminationChannelId>("white-led-channel"),
        {makeId<EquipmentComponentId>("objective-4x"),
         makeId<EquipmentComponentId>("condenser-abbe"),
         makeId<EquipmentComponentId>(neutralDensityFilterId)},
        IlluminationIntensityUnit::Percent,
        {{0.0, 0.0, 0.2},
         {25.0, 10.0, 0.2},
         {50.0, 30.0, 0.3},
         {100.0, 80.0, 0.4}}));
}

LightPathModeSetting makeModeSetting(
    const LightPathCalibration& calibration,
    double targetIrradiance) {
    return take(LightPathModeSetting::create(
        calibration.microscopeId(),
        calibration.equipmentDefinitionRevision(),
        calibration.operationModeId(),
        calibration.operationModeRevision(),
        calibration.lightPathId(),
        calibration.revisionId(),
        calibration.illuminationComponentId(),
        calibration.illuminationChannelId(),
        calibration.configuredOpticalComponentIds(),
        targetIrradiance));
}

OperationMode makeMode(
    const char* modeId,
    std::uint64_t revision,
    std::vector<LightPathModeSetting> settings) {
    return take(OperationMode::create(
        makeId<MicroscopeId>("persisted-microscope"),
        makeId<OperationModeId>(modeId),
        revision,
        revision == 1 ? "4× Brightfield" : "4× Brightfield updated",
        ContrastMethod::Brightfield,
        std::move(settings)));
}

void testCalibrationAndOperationModePersistence() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto databasePath =
        std::filesystem::temp_directory_path() /
        ("microscope-config-store-" + std::to_string(nonce) + ".sqlite");
    const auto path = databasePath.string();

    {
        auto opened = MicroscopeConfigurationSqliteStore::open(path);
        check(static_cast<bool>(opened),
              "opens the microscope configuration SQLite store");
        if (!opened) {
            return;
        }
        auto store = std::move(opened).value();

        const auto microscopeId = makeId<MicroscopeId>("persisted-microscope");
        const auto calibrationId =
            makeId<CalibrationRevisionId>("calibration-4x-bf-rev-1");
        const auto absent = store->loadCalibration(microscopeId, calibrationId);
        check(absent && !absent.value(),
              "returns no calibration for an unknown immutable revision");

        const auto calibration1 = makeCalibration(
            "calibration-4x-bf-rev-1",
            "mode-4x-brightfield",
            1,
            "transmitted-light",
            "nd-filter-low");
        const auto savedCalibration1 = store->saveCalibration(calibration1);
        check(savedCalibration1.isSuccess(),
              "persists a measured LightPath calibration as an immutable record");

        const auto loadedCalibration1 =
            store->loadCalibration(microscopeId, calibrationId);
        check(loadedCalibration1 && loadedCalibration1.value().has_value(),
              "reloads a persisted LightPath calibration by microscope and revision");
        if (loadedCalibration1 && loadedCalibration1.value()) {
            const auto& loaded = *loadedCalibration1.value();
            check(loaded.points().size() == 4,
                  "restores every calibrated source-drive/irradiance point");
            check(loaded.configuredOpticalComponentIds().size() == 3,
                  "restores the optical component binding");
            check(loaded.equipmentDefinitionRevision() == 5,
                  "restores the equipment constitution revision");
            check(loaded.operationModeRevision() == 1,
                  "restores the operation mode revision");
        }

        const auto duplicateCalibration = store->saveCalibration(calibration1);
        check(!duplicateCalibration.isSuccess() &&
              duplicateCalibration.error().code ==
                  MicroscopeConfigurationStoreErrorCode::DuplicateCalibrationRevision,
              "rejects mutation/reuse of an existing calibration revision");

        const auto mode1 = makeMode(
            "mode-4x-brightfield",
            1,
            {makeModeSetting(calibration1, 8.0)});
        const auto savedMode1 = store->saveOperationMode(mode1, 0);
        check(savedMode1.isSuccess(),
              "persists the first editable OperationMode revision");

        const auto loadedMode1 = store->loadCurrentOperationMode(
            microscopeId,
            makeId<OperationModeId>("mode-4x-brightfield"));
        check(loadedMode1 && loadedMode1.value().has_value(),
              "loads the current OperationMode revision");
        if (loadedMode1 && loadedMode1.value()) {
            const auto& loaded = *loadedMode1.value();
            check(loaded.revision() == 1 &&
                  loaded.contrastMethod() == ContrastMethod::Brightfield,
                  "restores OperationMode identity, revision and contrast method");
            check(loaded.lightPathSettings().size() == 1,
                  "restores per-LightPath mode settings");
            const auto* setting =
                loaded.findLightPathSetting(makeId<LightPathId>("transmitted-light"));
            check(setting != nullptr,
                  "restores the mode's selected LightPath setting");
            check(setting &&
                  setting->targetIrradianceMicrowattsPerSquareMillimeter() == 8.0,
                  "restores the mode-specific specimen-plane irradiance target");
        }

        const auto calibration2 = makeCalibration(
            "calibration-4x-bf-rev-2",
            "mode-4x-brightfield",
            2,
            "transmitted-light",
            "nd-filter-medium");
        check(store->saveCalibration(calibration2).isSuccess(),
              "stores a new calibration revision for the edited mode");

        const auto mode2 = makeMode(
            "mode-4x-brightfield",
            2,
            {makeModeSetting(calibration2, 5.0)});
        const auto staleSave = store->saveOperationMode(mode2, 0);
        check(!staleSave.isSuccess() &&
              staleSave.error().code ==
                  MicroscopeConfigurationStoreErrorCode::RevisionConflict,
              "rejects a stale OperationMode writer");

        const auto savedMode2 = store->saveOperationMode(mode2, 1);
        check(savedMode2.isSuccess(),
              "commits the new OperationMode revision against the expected current revision");

        const auto loadedMode2 = store->loadCurrentOperationMode(
            microscopeId,
            makeId<OperationModeId>("mode-4x-brightfield"));
        check(loadedMode2 && loadedMode2.value() &&
              loadedMode2.value()->revision() == 2,
              "makes the new operation mode revision current after commit");
        if (loadedMode2 && loadedMode2.value()) {
            const auto* setting = loadedMode2.value()->findLightPathSetting(
                makeId<LightPathId>("transmitted-light"));
            check(setting &&
                  setting->targetIrradianceMicrowattsPerSquareMillimeter() == 5.0,
                  "persists a different calibrated intensity target for the revised mode");
            check(setting &&
                  setting->calibrationRevisionId() ==
                      makeId<CalibrationRevisionId>("calibration-4x-bf-rev-2"),
                  "binds the revised mode to its exact new calibration revision");
        }

        const auto wrongCalibrationBinding = take(LightPathModeSetting::create(
            microscopeId,
            5,
            makeId<OperationModeId>("mode-4x-brightfield"),
            3,
            makeId<LightPathId>("transmitted-light"),
            makeId<CalibrationRevisionId>("calibration-4x-bf-rev-2"),
            makeId<EquipmentComponentId>("transmitted-led"),
            makeId<IlluminationChannelId>("white-led-channel"),
            {makeId<EquipmentComponentId>("objective-4x"),
             makeId<EquipmentComponentId>("condenser-abbe"),
             makeId<EquipmentComponentId>("nd-filter-medium")},
            4.0));
        const auto invalidMode = makeMode(
            "mode-4x-brightfield",
            3,
            {wrongCalibrationBinding});
        const auto invalidSave = store->saveOperationMode(invalidMode, 2);
        check(!invalidSave.isSuccess() &&
              invalidSave.error().code ==
                  MicroscopeConfigurationStoreErrorCode::CalibrationBindingMismatch,
              "rejects a mode setting that references a calibration from another mode revision");
    }

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

} // namespace

int main() {
    testCalibrationAndOperationModePersistence();

    if (failures != 0) {
        std::cerr << failures << " microscope configuration persistence test(s) failed.\n";
        return 1;
    }

    std::cout << "All microscope configuration persistence tests passed.\n";
    return 0;
}
