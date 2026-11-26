#include "microscope/core/configuration/OperationMode.h"

#include <iostream>
#include <stdexcept>
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
        throw std::runtime_error("An OperationMode fixture factory rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

LightPathModeSetting makeSetting(
    const char* modeId,
    std::uint64_t modeRevision,
    const char* lightPathId,
    const char* calibrationRevision,
    const char* sourceId,
    const char* channelId,
    double targetIrradiance) {
    return take(LightPathModeSetting::create(
        makeId<MicroscopeId>("optika-microscope"),
        9,
        makeId<OperationModeId>(modeId),
        modeRevision,
        makeId<LightPathId>(lightPathId),
        makeId<CalibrationRevisionId>(calibrationRevision),
        makeId<EquipmentComponentId>(sourceId),
        makeId<IlluminationChannelId>(channelId),
        {makeId<EquipmentComponentId>("objective-4x"),
         makeId<EquipmentComponentId>("condenser-darkfield"),
         makeId<EquipmentComponentId>("filter-neutral-density")},
        targetIrradiance));
}

void testOperationModeOwnsModeSpecificLightPathSettings() {
    const auto modeId = makeId<OperationModeId>("mode-4x-darkfield");
    auto brightfield = makeSetting(
        "mode-4x-darkfield", 3, "transmitted-light", "cal-brightfield-3",
        "white-led-source", "white-led-channel", 3.0);
    auto darkfield = makeSetting(
        "mode-4x-darkfield", 3, "darkfield-light", "cal-darkfield-3",
        "white-led-source", "white-led-channel", 11.0);

    const auto mode = take(OperationMode::create(
        makeId<MicroscopeId>("optika-microscope"),
        modeId,
        3,
        "4× Darkfield",
        ContrastMethod::Darkfield,
        {brightfield, darkfield}));

    check(mode.id() == modeId && mode.revision() == 3,
          "retains stable mode identity and revision");
    check(mode.lightPathSettings().size() == 2,
          "owns one saved illumination setting per configured LightPath");
    check(mode.findLightPathSetting(makeId<LightPathId>("transmitted-light")) != nullptr,
          "supports direct lookup of a LightPath setting");
    check(mode.findLightPathSetting(makeId<LightPathId>("darkfield-light")) != nullptr,
          "retains additional LightPath settings in the same mode");
    check(mode.findLightPathSetting(makeId<LightPathId>("not-in-mode")) == nullptr,
          "does not return settings for LightPaths outside the mode");
    check(mode.lightPathSettings()[0]
              .targetIrradianceMicrowattsPerSquareMillimeter() == 3.0 &&
          mode.lightPathSettings()[1]
              .targetIrradianceMicrowattsPerSquareMillimeter() == 11.0,
          "allows distinct calibrated specimen irradiance targets for different routes");
}

void testOperationModeRejectsSettingsFromAnotherRevisionOrMicroscope() {
    auto staleRevision = makeSetting(
        "mode-40x-brightfield", 4, "transmitted-light", "cal-40x-old",
        "white-led-source", "white-led-channel", 20.0);
    auto result = OperationMode::create(
        makeId<MicroscopeId>("optika-microscope"),
        makeId<OperationModeId>("mode-40x-brightfield"),
        5,
        "40× Brightfield",
        ContrastMethod::Brightfield,
        {staleRevision});
    check(!result &&
          result.error() == OperationModeError::LightPathSettingRevisionMismatch,
          "does not mix a LightPath setting from an earlier mode revision");

    auto anotherMicroscope = take(LightPathModeSetting::create(
        makeId<MicroscopeId>("other-microscope"),
        9,
        makeId<OperationModeId>("mode-40x-brightfield"),
        5,
        makeId<LightPathId>("transmitted-light"),
        makeId<CalibrationRevisionId>("cal-40x-other"),
        makeId<EquipmentComponentId>("white-led-source"),
        makeId<IlluminationChannelId>("white-led-channel"),
        {makeId<EquipmentComponentId>("objective-40x")},
        20.0));
    result = OperationMode::create(
        makeId<MicroscopeId>("optika-microscope"),
        makeId<OperationModeId>("mode-40x-brightfield"),
        5,
        "40× Brightfield",
        ContrastMethod::Brightfield,
        {anotherMicroscope});
    check(!result &&
          result.error() ==
              OperationModeError::LightPathSettingMicroscopeMismatch,
          "does not attach another microscope's LightPath settings");
}

void testOperationModeRejectsDuplicateLightPathSettingsAndEmptyModes() {
    const auto setting = makeSetting(
        "mode-4x-brightfield", 1, "transmitted-light", "cal-4x-1",
        "white-led-source", "white-led-channel", 4.0);
    auto duplicate = OperationMode::create(
        makeId<MicroscopeId>("optika-microscope"),
        makeId<OperationModeId>("mode-4x-brightfield"),
        1,
        "4× Brightfield",
        ContrastMethod::Brightfield,
        {setting, setting});
    check(!duplicate &&
          duplicate.error() == OperationModeError::DuplicateLightPath,
          "rejects duplicate settings for one LightPath within a mode");

    auto empty = OperationMode::create(
        makeId<MicroscopeId>("optika-microscope"),
        makeId<OperationModeId>("empty-mode"),
        1,
        "Empty",
        ContrastMethod::Brightfield,
        {});
    check(!empty &&
          empty.error() == OperationModeError::NoLightPathSettings,
          "requires at least one configured and calibrated LightPath");
}

} // namespace

int main() {
    testOperationModeOwnsModeSpecificLightPathSettings();
    testOperationModeRejectsSettingsFromAnotherRevisionOrMicroscope();
    testOperationModeRejectsDuplicateLightPathSettingsAndEmptyModes();

    if (failures != 0) {
        std::cerr << failures << " OperationMode contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All OperationMode contract tests passed.\n";
    return 0;
}
