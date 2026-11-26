#include "microscope/core/configuration/LightPathCalibration.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace {

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
        throw std::runtime_error("A calibration test fixture factory rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

LightPathCalibration makeCalibration(
    const OperationModeId& modeId,
    const LightPathId& lightPathId,
    const std::vector<EquipmentComponentId>& opticalComponents) {
    return take(LightPathCalibration::create(
        makeId<CalibrationRevisionId>("calibration-revision-4x-brightfield"),
        makeId<MicroscopeId>("calibration-microscope"),
        12,
        modeId,
        7,
        lightPathId,
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        opticalComponents,
        IlluminationIntensityUnit::Percent,
        {{0.0, 0.0, 0.5},
         {25.0, 10.0, 0.5},
         {50.0, 30.0, 0.4},
         {100.0, 100.0, 0.4}}));
}

void testModeSettingUsesExactCalibrationAndInterpolatesIrradiance() {
    const auto modeId = makeId<OperationModeId>("mode-4x-brightfield");
    const auto lightPathId = makeId<LightPathId>("light-path-transmitted");
    const std::vector<EquipmentComponentId> opticalComponents{
        makeId<EquipmentComponentId>("objective-4x"),
        makeId<EquipmentComponentId>("condenser-abbe"),
        makeId<EquipmentComponentId>("filter-neutral-density-1")};

    const auto calibration = makeCalibration(modeId, lightPathId, opticalComponents);
    const auto setting = take(LightPathModeSetting::create(
        makeId<MicroscopeId>("calibration-microscope"),
        12,
        modeId,
        7,
        lightPathId,
        calibration.revisionId(),
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        opticalComponents,
        20.0));

    const auto drive = setting.resolveDriveSetting(calibration);
    check(drive.has_value(),
          "resolves a source drive value from the exact persisted calibration revision");
    check(drive && std::abs(*drive - 37.5) < 1e-9,
          "uses piecewise-linear calibration interpolation in specimen irradiance units");

    const auto outsideRange = take(LightPathModeSetting::create(
        makeId<MicroscopeId>("calibration-microscope"),
        12,
        modeId,
        7,
        lightPathId,
        calibration.revisionId(),
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        opticalComponents,
        150.0));
    check(!outsideRange.resolveDriveSetting(calibration),
          "does not extrapolate beyond the measured irradiance calibration range");
}

void testCalibrationBindingRejectsChangedOpticalConfiguration() {
    const auto modeId = makeId<OperationModeId>("mode-40x-brightfield");
    const auto lightPathId = makeId<LightPathId>("light-path-transmitted");
    const std::vector<EquipmentComponentId> calibratedComponents{
        makeId<EquipmentComponentId>("objective-40x"),
        makeId<EquipmentComponentId>("condenser-abbe"),
        makeId<EquipmentComponentId>("filter-neutral-density-2")};
    const auto calibration = makeCalibration(
        modeId, lightPathId, calibratedComponents);

    const auto changedFilter = take(LightPathModeSetting::create(
        makeId<MicroscopeId>("calibration-microscope"),
        12,
        modeId,
        7,
        lightPathId,
        calibration.revisionId(),
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        {makeId<EquipmentComponentId>("objective-40x"),
         makeId<EquipmentComponentId>("condenser-abbe"),
         makeId<EquipmentComponentId>("filter-neutral-density-3")},
        20.0));
    check(!changedFilter.resolveDriveSetting(calibration),
          "rejects reusing calibration after an optical filter has changed");

    const auto changedModeRevision = take(LightPathModeSetting::create(
        makeId<MicroscopeId>("calibration-microscope"),
        12,
        modeId,
        8,
        lightPathId,
        calibration.revisionId(),
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        calibratedComponents,
        20.0));
    check(!changedModeRevision.resolveDriveSetting(calibration),
          "rejects calibration from an older operation-mode revision");

    const auto changedPath = take(LightPathModeSetting::create(
        makeId<MicroscopeId>("calibration-microscope"),
        12,
        modeId,
        7,
        makeId<LightPathId>("light-path-episcopic"),
        calibration.revisionId(),
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        calibratedComponents,
        20.0));
    check(!changedPath.resolveDriveSetting(calibration),
          "rejects calibration from a different physical LightPath");
}

void testCalibrationRejectsInvalidCurves() {
    const auto common = std::tuple{
        makeId<CalibrationRevisionId>("bad-calibration"),
        makeId<MicroscopeId>("bad-calibration-microscope"),
        std::uint64_t{1},
        makeId<OperationModeId>("bad-mode"),
        std::uint64_t{1},
        makeId<LightPathId>("bad-light-path"),
        makeId<EquipmentComponentId>("bad-source"),
        makeId<IlluminationChannelId>("bad-channel"),
        std::vector<EquipmentComponentId>{},
        IlluminationIntensityUnit::Percent};

    auto pointsNotIncreasing = LightPathCalibration::create(
        std::get<0>(common), std::get<1>(common), std::get<2>(common),
        std::get<3>(common), std::get<4>(common), std::get<5>(common),
        std::get<6>(common), std::get<7>(common), std::get<8>(common),
        std::get<9>(common),
        {{0.0, 0.0, 1.0}, {50.0, 30.0, 1.0}, {40.0, 60.0, 1.0}});
    check(!pointsNotIncreasing &&
          pointsNotIncreasing.error() ==
              LightPathCalibrationError::DriveValuesNotIncreasing,
          "rejects a non-increasing light-source drive curve");

    auto duplicateComponents = LightPathCalibration::create(
        std::get<0>(common), std::get<1>(common), std::get<2>(common),
        std::get<3>(common), std::get<4>(common), std::get<5>(common),
        std::get<6>(common), std::get<7>(common),
        {makeId<EquipmentComponentId>("duplicate-optic"),
         makeId<EquipmentComponentId>("duplicate-optic")},
        std::get<9>(common),
        {{0.0, 0.0, 1.0}, {100.0, 50.0, 1.0}});
    check(!duplicateComponents &&
          duplicateComponents.error() ==
              LightPathCalibrationError::DuplicateOpticalComponent,
          "rejects duplicate physical component identities in the calibration binding");
}

} // namespace

int main() {
    testModeSettingUsesExactCalibrationAndInterpolatesIrradiance();
    testCalibrationBindingRejectsChangedOpticalConfiguration();
    testCalibrationRejectsInvalidCurves();

    if (failures != 0) {
        std::cerr << failures << " light-path calibration contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All light-path calibration contract tests passed.\n";
    return 0;
}
