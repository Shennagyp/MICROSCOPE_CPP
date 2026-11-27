#include "microscope/core/configuration/LightPathCalibration.h"
#include "microscope/core/configuration/OperationMode.h"

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

    const auto changedEquipmentRevision = take(LightPathModeSetting::create(
        makeId<MicroscopeId>("calibration-microscope"),
        13,
        modeId,
        7,
        lightPathId,
        calibration.revisionId(),
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        calibratedComponents,
        20.0));
    check(!changedEquipmentRevision.resolveDriveSetting(calibration),
          "rejects calibration when the microscope equipment constitution revision changes");

    const auto wrongMicroscope = take(LightPathModeSetting::create(
        makeId<MicroscopeId>("another-microscope"),
        12,
        modeId,
        7,
        lightPathId,
        calibration.revisionId(),
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        calibratedComponents,
        20.0));
    check(!wrongMicroscope.resolveDriveSetting(calibration),
          "rejects reuse of a calibration belonging to a different microscope");
}

void testOperationModeHardwareConfigurationIsSeparateAndRevisionBound() {
    const auto mode = take(OperationMode::create(
        id<MicroscopeId>("hardware-config-microscope"),
        id<OperationModeId>("mode-with-two-lightpaths"),
        4,
        "Brightfield",
        ContrastMethod::Brightfield));

    const auto settingA = take(LightPathModeSetting::create(
        mode.microscopeId(), 12, mode.id(), mode.revision(),
        id<LightPathId>("transmitted-light"),
        id<CalibrationRevisionId>("cal-transmitted-r4"),
        id<EquipmentComponentId>("white-led"),
        id<IlluminationChannelId>("white-channel"),
        {id<EquipmentComponentId>("objective-4x")}, 9.0));
    const auto settingB = take(LightPathModeSetting::create(
        mode.microscopeId(), 12, mode.id(), mode.revision(),
        id<LightPathId>("epi-light"),
        id<CalibrationRevisionId>("cal-epi-r4"),
        id<EquipmentComponentId>("epi-led"),
        id<IlluminationChannelId>("epi-channel"),
        {id<EquipmentComponentId>("objective-10x")}, 4.0));

    const auto hardware = take(OperationModeHardwareConfiguration::create(
        mode, {settingA, settingB}));
    check(hardware.microscopeId() == mode.microscopeId() &&
          hardware.operationModeId() == mode.id() &&
          hardware.operationModeRevision() == mode.revision() &&
          hardware.equipmentDefinitionRevision() == 12,
          "binds hardware realization to the exact microscope, mode and equipment revisions");
    check(hardware.lightPathSettings().size() == 2 &&
          hardware.findLightPathSetting(id<LightPathId>("epi-light")) != nullptr,
          "stores and looks up independent per-LightPath settings");

    const auto empty = OperationModeHardwareConfiguration::create(mode, {});
    check(!empty && empty.error() ==
              OperationModeHardwareConfigurationError::EmptyLightPathSettings,
          "rejects an empty hardware realization object");

    const auto staleSetting = take(LightPathModeSetting::create(
        mode.microscopeId(), 12, mode.id(), mode.revision() - 1,
        id<LightPathId>("stale-light"),
        id<CalibrationRevisionId>("cal-stale"),
        id<EquipmentComponentId>("white-led"),
        id<IlluminationChannelId>("white-channel"), {}, 2.0));
    const auto staleModeRevision =
        OperationModeHardwareConfiguration::create(mode, {staleSetting});
    check(!staleModeRevision && staleModeRevision.error() ==
              OperationModeHardwareConfigurationError::OperationModeRevisionMismatch,
          "rejects settings from a different OperationMode revision");

    const auto wrongModeSetting = take(LightPathModeSetting::create(
        mode.microscopeId(), 12, id<OperationModeId>("other-mode"), mode.revision(),
        id<LightPathId>("other-mode-light"),
        id<CalibrationRevisionId>("cal-other-mode"),
        id<EquipmentComponentId>("white-led"),
        id<IlluminationChannelId>("white-channel"), {}, 2.0));
    const auto wrongMode =
        OperationModeHardwareConfiguration::create(mode, {wrongModeSetting});
    check(!wrongMode && wrongMode.error() ==
              OperationModeHardwareConfigurationError::OperationModeIdentityMismatch,
          "rejects settings assigned to another mode identity");

    const auto mixedEquipmentSetting = take(LightPathModeSetting::create(
        mode.microscopeId(), 13, mode.id(), mode.revision(),
        id<LightPathId>("mixed-equipment-light"),
        id<CalibrationRevisionId>("cal-mixed-equipment"),
        id<EquipmentComponentId>("white-led"),
        id<IlluminationChannelId>("white-channel"), {}, 2.0));
    const auto mixedEquipment =
        OperationModeHardwareConfiguration::create(mode, {settingA, mixedEquipmentSetting});
    check(!mixedEquipment && mixedEquipment.error() ==
              OperationModeHardwareConfigurationError::EquipmentDefinitionRevisionMismatch,
          "rejects settings captured from different equipment revisions");

    const auto duplicate =
        OperationModeHardwareConfiguration::create(mode, {settingA, settingA});
    check(!duplicate && duplicate.error() ==
              OperationModeHardwareConfigurationError::DuplicateLightPath,
          "rejects duplicate LightPath settings");
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
    testOperationModeHardwareConfigurationIsSeparateAndRevisionBound();
    testCalibrationRejectsInvalidCurves();

    if (failures != 0) {
        std::cerr << failures << " light-path calibration contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All light-path calibration contract tests passed.\n";
    return 0;
}
