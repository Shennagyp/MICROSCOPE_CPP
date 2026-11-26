#include "microscope/core/configuration/ConfigurationTransition.h"
#include "microscope/core/configuration/LightPathCameraResolver.h"
#include "microscope/core/configuration/LightPathConfigurationRequestFactory.h"
#include "microscope/core/configuration/OperationMode.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
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

template<class T, class E>
T take(Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("optical workflow integration fixture rejected valid input");
    }
    return std::move(result).value();
}

template<class T>
T id(const char* value) {
    return take(T::create(value));
}

PositiveMicrometers micrometers(double value) {
    return take(PositiveMicrometers::create(value));
}

NominalMagnification magnification(double value) {
    return take(NominalMagnification::create(value));
}

NumericalAperture numericalAperture(double value) {
    return take(NumericalAperture::create(value));
}

NonNegativeNumericalAperture nonNegativeAperture(double value) {
    return take(NonNegativeNumericalAperture::create(value));
}

OpticalPort opticalPort(const char* name, OpticalPortDirection direction) {
    return take(OpticalPort::create(id<OpticalPortId>(name), direction));
}

OpticalPathStep pathStep(
    const EquipmentComponentId& componentId,
    std::optional<OpticalPortId> entry,
    std::optional<OpticalPortId> exit) {
    return take(OpticalPathStep::create(
        componentId, std::move(entry), std::move(exit)));
}

struct Fixture final {
    MicroscopeId microscopeId;
    EquipmentComponentId sourceId;
    EquipmentComponentId condenserId;
    EquipmentComponentId objectiveId;
    EquipmentComponentId cameraId;
    IlluminationChannelId channelId;
    OpticalPathId pathId;
    LightPathId lightPathId;
    CameraModeId cameraModeId;
    ConfigurationProfileId profileId;
    OperationModeId modeId;
    MicroscopeEquipmentDefinition equipment;
    OperationMode operationMode;
    ConfigurationProfileCatalog profileCatalog;
};

Fixture makeFixture() {
    const auto microscopeId = id<MicroscopeId>("workflow-microscope");
    const auto sourceId = id<EquipmentComponentId>("workflow-transmitted-led");
    const auto condenserId = id<EquipmentComponentId>("workflow-abbe-condenser");
    const auto objectiveId = id<EquipmentComponentId>("workflow-objective-10x");
    const auto cameraId = id<EquipmentComponentId>("workflow-camera");
    const auto channelId = id<IlluminationChannelId>("workflow-white-channel");
    const auto pathId = id<OpticalPathId>("workflow-imaging-route");
    const auto lightPathId = id<LightPathId>("workflow-brightfield-light-path");
    const auto cameraModeId = id<CameraModeId>("workflow-mono8");
    const auto profileId = id<ConfigurationProfileId>("workflow-brightfield-profile");
    const auto modeId = id<OperationModeId>("workflow-brightfield-mode");

    const auto sourceOutput = id<OpticalPortId>("workflow-source-output");
    const auto condenserInput = id<OpticalPortId>("workflow-condenser-input");
    const auto condenserOutput = id<OpticalPortId>("workflow-condenser-output");
    const auto objectiveInput = id<OpticalPortId>("workflow-objective-input");
    const auto objectiveOutput = id<OpticalPortId>("workflow-objective-output");
    const auto cameraInput = id<OpticalPortId>("workflow-camera-input");

    std::vector<EquipmentComponent> components;
    components.push_back(take(EquipmentComponent::create(
        sourceId,
        EquipmentComponentKind::IlluminationDevice,
        {opticalPort("workflow-source-output", OpticalPortDirection::Output)})));
    components.push_back(take(EquipmentComponent::create(
        condenserId,
        EquipmentComponentKind::OpticalElement,
        {opticalPort("workflow-condenser-input", OpticalPortDirection::Input),
         opticalPort("workflow-condenser-output", OpticalPortDirection::Output)})));
    components.push_back(take(EquipmentComponent::create(
        objectiveId,
        EquipmentComponentKind::OpticalElement,
        {opticalPort("workflow-objective-input", OpticalPortDirection::Input),
         opticalPort("workflow-objective-output", OpticalPortDirection::Output)})));
    components.push_back(take(EquipmentComponent::create(
        cameraId,
        EquipmentComponentKind::ImagingDevice,
        {opticalPort("workflow-camera-input", OpticalPortDirection::Input)})));

    std::vector<OpticalConnection> connections{
        take(OpticalConnection::create(
            id<OpticalConnectionId>("workflow-source-to-condenser"),
            sourceOutput, condenserInput)),
        take(OpticalConnection::create(
            id<OpticalConnectionId>("workflow-condenser-to-objective"),
            condenserOutput, objectiveInput)),
        take(OpticalConnection::create(
            id<OpticalConnectionId>("workflow-objective-to-camera"),
            objectiveOutput, cameraInput))};

    const auto path = take(OpticalPath::create(
        pathId,
        {pathStep(sourceId, std::nullopt, sourceOutput),
         pathStep(condenserId, condenserInput, condenserOutput),
         pathStep(objectiveId, objectiveInput, objectiveOutput),
         pathStep(cameraId, cameraInput, std::nullopt)},
        OpticalPathRole::Imaging));
    const auto lightPath = take(LightPath::create(
        lightPathId, "Transmitted brightfield", {pathId}));

    const auto objective = take(ObjectiveSpecification::create(
        objectiveId,
        magnification(10.0),
        numericalAperture(0.25),
        micrometers(6000.0),
        ObjectiveChromaticCorrection::Achromatic,
        ObjectiveFieldCorrection::Plan,
        ObjectiveImmersionMedium::Air));
    const auto condenser = take(CondenserSpecification::create(
        condenserId,
        nonNegativeAperture(0.0),
        numericalAperture(0.90),
        CondenserDesign::Abbe,
        CondenserImmersionMedium::Air));
    const auto cameraMode = take(CameraOutputMode::create(
        cameraModeId,
        take(SensorRegionOfInterest::create(0, 0, 640, 480)),
        take(PixelBinning::create(1, 1)),
        take(PixelDimensions::create(640, 480)),
        CameraPixelLayout::Monochrome,
        "Mono8"));
    const auto camera = take(CameraSpecification::create(
        cameraId,
        take(PixelDimensions::create(640, 480)),
        micrometers(3.45),
        micrometers(3.45),
        {cameraMode},
        take(ExposureTimeRange::create(
            take(PositiveMicroseconds::create(100.0)),
            take(PositiveMicroseconds::create(1000000.0)))));
    const auto spectrum = take(SpectralBand::create(
        take(PositiveNanometers::create(400.0)),
        take(PositiveNanometers::create(700.0))));
    const auto intensity = take(IlluminationIntensityRange::create(
        0.0, 100.0, IlluminationIntensityUnit::Percent));
    const auto channel = take(IlluminationChannel::create(
        channelId, spectrum, IlluminationControlKind::Continuous, intensity));
    const auto illumination = take(IlluminationSpecification::create(
        sourceId, IlluminationSourceTechnology::Led, {channel}));

    const auto equipment = take(MicroscopeEquipmentDefinition::create(
        microscopeId,
        std::move(components),
        std::move(connections),
        {path},
        {objective},
        {camera},
        {condenser},
        {illumination},
        {}, {}, {lightPath},
        {}, 7));

    const auto calibrationRevision =
        id<CalibrationRevisionId>("workflow-calibration-r3");
    const auto modeSetting = take(LightPathModeSetting::create(
        microscopeId,
        equipment.revision(),
        modeId,
        2,
        lightPathId,
        calibrationRevision,
        sourceId,
        channelId,
        {objectiveId, condenserId},
        12.0));
    const auto operationMode = take(OperationMode::create(
        microscopeId,
        modeId,
        2,
        "Transmitted brightfield",
        ContrastMethod::Brightfield,
        {modeSetting}));

    const auto profile = take(ConfigurationProfile::create(
        profileId,
        ContrastMethod::Brightfield,
        {objectiveId},
        {condenserId},
        {pathId},
        {cameraId},
        {sourceId},
        {objectiveId}));
    const auto profiles = take(ConfigurationProfileCatalog::create({profile}));

    return Fixture{
        microscopeId, sourceId, condenserId, objectiveId, cameraId,
        channelId, pathId, lightPathId, cameraModeId, profileId, modeId,
        std::move(equipment), operationMode, profiles};
}

bool hasCode(const Diagnostics& diagnostics, const char* expected) {
    return std::any_of(
        diagnostics.begin(), diagnostics.end(),
        [expected](const Diagnostic& diagnostic) {
            return diagnostic.code() == expected;
        });
}

void testCompleteLightPathToDesiredConfigurationWorkflow() {
    const auto fixture = makeFixture();

    // 1. The camera candidate is discovered from the selected physical imaging route.
    const auto endpoints =
        fixture.equipment.cameraEndpointIds(fixture.lightPathId);
    check(endpoints.size() == 1 && endpoints.front() == fixture.cameraId,
          "the LightPath derives its camera only from the physical imaging-route terminal");

    const auto cameraResolution = LightPathCameraResolver::resolve(
        fixture.equipment,
        fixture.operationMode,
        fixture.lightPathId);
    check(cameraResolution.status() == LightPathCameraResolutionStatus::Resolved &&
          cameraResolution.selected() &&
          cameraResolution.selected()->cameraComponentId == fixture.cameraId &&
          cameraResolution.selected()->cameraModeId == fixture.cameraModeId,
          "the mode and LightPath resolve the physically connected camera and its supported output mode");

    // 2. The request factory transfers the resolved camera/route identity into the
    // legacy request shape; callers cannot substitute another camera or route list.
    const auto request = LightPathConfigurationRequestFactory::create(
        fixture.equipment,
        fixture.operationMode,
        fixture.lightPathId,
        cameraResolution,
        fixture.profileId,
        fixture.objectiveId,
        fixture.condenserId,
        fixture.sourceId,
        fixture.channelId,
        ExposurePolicy::Manual);
    check(bool(request),
          "the LightPath-first request factory creates a request from the resolved camera selection");
    if (!request) return;

    check(request.value().opticalPathIds() == std::vector<OpticalPathId>{fixture.pathId} &&
          request.value().cameraComponentId() == fixture.cameraId &&
          request.value().cameraModeId() == fixture.cameraModeId,
          "the generated request retains the selected LightPath routes and derived camera mode");
    check(request.value().lightPathBinding() &&
          request.value().lightPathBinding()->operationModeId == fixture.modeId &&
          request.value().lightPathBinding()->operationModeRevision == 2 &&
          request.value().lightPathBinding()->equipmentDefinitionRevision ==
              fixture.equipment.revision(),
          "the request preserves exact mode and equipment revision provenance");

    // 3. Full configuration compatibility is checked against the installed
    // equipment definition and the declarative profile.
    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profileCatalog, request.value());
    check(resolution.isResolved() && resolution.resolvedConfiguration(),
          "the generated LightPath request passes installed-component and profile compatibility validation");
    if (!resolution.resolvedConfiguration()) {
        for (const auto& diagnostic : resolution.diagnostics()) {
            std::cerr << "  diagnostic: " << diagnostic.code()
                      << " — " << diagnostic.message() << '\n';
        }
        return;
    }

    const auto& desired = *resolution.resolvedConfiguration();
    check(desired.microscopeId() == fixture.microscopeId &&
          desired.request().cameraComponentId() == fixture.cameraId &&
          desired.request().objectiveComponentId() == fixture.objectiveId &&
          desired.request().condenserComponentId() == fixture.condenserId &&
          desired.request().illuminationComponentId() == fixture.sourceId,
          "the resolved desired state retains one coherent instrument-wide component selection");

    // 4. The planner creates a dependency plan, not physical device commands.
    const auto transition = ConfigurationTransitionPlanner::plan(desired);
    check(bool(transition),
          "the resolved desired state can be represented as a validated transition dependency plan");
    if (transition) {
        const auto& layers = transition.value().executionLayers();
        check(!layers.empty() && layers.front().size() == 1 &&
              layers.back().size() == 1,
              "the dependency DAG has serialized precondition and final-verification boundary layers");
        check(std::any_of(
                  transition.value().steps().begin(),
                  transition.value().steps().end(),
                  [](const ConfigurationTransitionStep& step) {
                      return std::holds_alternative<CameraOutputModeMustMatch>(
                          step.requirement());
                  }),
              "the transition plan includes the derived camera output mode requirement");
        check(std::any_of(
                  transition.value().steps().begin(),
                  transition.value().steps().end(),
                  [](const ConfigurationTransitionStep& step) {
                      return std::holds_alternative<DesiredConfigurationMustBeVerified>(
                          step.requirement());
                  }),
              "the transition plan ends with an explicit desired-configuration verification requirement");
    }

    // 5. A manually supplied camera outside this LightPath cannot enter the
    // request pipeline merely because it exists elsewhere in the equipment.
    const auto disconnectedCameraId =
        id<EquipmentComponentId>("workflow-disconnected-camera");
    const auto invalidManualSelection = LightPathCameraResolver::resolve(
        fixture.equipment,
        fixture.operationMode,
        fixture.lightPathId,
        disconnectedCameraId);
    check(invalidManualSelection.status() == LightPathCameraResolutionStatus::Unsupported &&
          !invalidManualSelection.selected() &&
          hasCode(invalidManualSelection.diagnostics(),
                  "light_path_camera.manual_camera_not_on_path"),
          "the integrated workflow rejects a camera not connected to the selected LightPath");
}

void testEquipmentRevisionChangeInvalidatesModeBoundLightPath() {
    const auto fixture = makeFixture();

    const auto cameraResolution = LightPathCameraResolver::resolve(
        fixture.equipment, fixture.operationMode, fixture.lightPathId);
    check(cameraResolution.isResolved(),
          "precondition: camera resolves against the current equipment revision");
    if (!cameraResolution.isResolved()) return;

    // Reusing the mode after the equipment revision changes is invalid even
    // when the physical component identifiers happen to remain the same.
    const auto changedEquipment = take(MicroscopeEquipmentDefinition::create(
        fixture.equipment.microscopeId(),
        fixture.equipment.components(),
        fixture.equipment.connections(),
        fixture.equipment.opticalPaths(),
        fixture.equipment.objectiveSpecifications(),
        fixture.equipment.cameraSpecifications(),
        fixture.equipment.condenserSpecifications(),
        fixture.equipment.illuminationSpecifications(),
        fixture.equipment.stageSpecifications(),
        fixture.equipment.dualSlideCarrierSpecifications(),
        fixture.equipment.lightPaths(),
        fixture.equipment.filterSpecifications(),
        fixture.equipment.revision() + 1));

    const auto staleResolution = LightPathCameraResolver::resolve(
        changedEquipment, fixture.operationMode, fixture.lightPathId);
    check(staleResolution.status() == LightPathCameraResolutionStatus::Unsupported &&
          hasCode(staleResolution.diagnostics(),
                  "light_path_camera.equipment_revision_mismatch"),
          "the LightPath camera workflow rejects an OperationMode bound to an older equipment revision");
}

} // namespace

int main() {
    testCompleteLightPathToDesiredConfigurationWorkflow();
    testEquipmentRevisionChangeInvalidatesModeBoundLightPath();

    if (failures != 0) {
        std::cerr << failures << " optical configuration workflow integration test(s) failed\n";
        return 1;
    }
    std::cout << "Optical configuration workflow integration tests passed\n";
    return 0;
}
