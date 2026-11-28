#include "microscope/core/configuration/ConfigurationTransition.h"
#include "microscope/core/configuration/LightPathCameraResolver.h"
#include "microscope/core/configuration/LightPathConfigurationRequestFactory.h"
#include "microscope/core/configuration/OperationMode.h"
#include "microscope/core/configuration/LightPathCalibration.h"
#include "microscope/core/configuration/SampleMicroscopyRequirementAssessor.h"

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
    OperationModeHardwareConfiguration hardwareConfiguration;
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
            take(PositiveMicroseconds::create(1000000.0))))));
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
        {}, {lightPath},
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
        ContrastMethod::Brightfield));
    const auto hardwareConfiguration =
        take(OperationModeHardwareConfiguration::create(operationMode, {modeSetting}));

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
        std::move(equipment), operationMode, hardwareConfiguration, profiles};
}

bool hasCode(const Diagnostics& diagnostics, const char* expected) {
    return std::any_of(
        diagnostics.begin(), diagnostics.end(),
        [expected](const Diagnostic& diagnostic) {
            return diagnostic.code() == expected;
        });
}

void testSlideAssessmentUsesPinnedSampleTypeAndObservedSlideFacts() {
    const auto fixture = makeFixture();
    const auto cameraResolution = LightPathCameraResolver::resolve(
        fixture.equipment, fixture.operationMode, fixture.lightPathId);
    if (!cameraResolution.isResolved()) {
        check(false, "precondition: integration fixture resolves the physical LightPath camera");
        return;
    }
    const auto request = LightPathConfigurationRequestFactory::create(
        fixture.equipment,
        fixture.operationMode,
        fixture.hardwareConfiguration,
        fixture.lightPathId,
        cameraResolution,
        fixture.profileId,
        fixture.objectiveId,
        fixture.condenserId,
        fixture.sourceId,
        fixture.channelId,
        ExposurePolicy::Manual);
    if (!request) {
        check(false, "precondition: LightPath request is created");
        return;
    }
    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profileCatalog, request.value());
    if (!resolution.resolvedConfiguration()) {
        check(false, "precondition: microscope configuration resolves before SampleType assessment");
        return;
    }

    const auto preparationRequirements = take(SlidePreparationRequirements::create(
        {SpecimenPreparationMethod::TissueSection},
        {SpecimenFixationState::Fixed},
        3.0,
        8.0,
        150.0,
        190.0,
        true,
        {SpecimenLabelKind::Fluorescent}));
    const auto microscopyRequirements = take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Required}}));
    const auto sampleType = take(SampleType::create(
        id<SampleTypeId>("assessment-sample-type"),
        4,
        "Fixed immunofluorescence tissue section",
        SampleMaterialClass::Biological,
        microscopyRequirements,
        std::nullopt,
        preparationRequirements));
    const auto geometry = take(SlideGeometry::create(75000.0, 25000.0));
    const auto fluorescentLabel = take(SpecimenLabel::create(
        "FITC", SpecimenLabelKind::Fluorescent));
    const auto compatiblePreparation = take(SlidePreparation::create(
        SpecimenPreparationMethod::TissueSection,
        SpecimenFixationState::Fixed,
        5.0,
        170.0,
        "Antifade mounting medium",
        1.46,
        {fluorescentLabel}));
    const auto compatibleSlide = take(Slide::create(
        id<SlideId>("assessment-slide-compatible"),
        1,
        sampleType.id(),
        sampleType.revision(),
        "OPT-COMPAT-001",
        "Compatible slide",
        geometry,
        compatiblePreparation));

    const auto compatibleAssessment = SampleMicroscopyRequirementAssessor::assess(
        fixture.equipment,
        sampleType,
        compatibleSlide,
        *resolution.resolvedConfiguration());
    check(compatibleAssessment.isCompatible() &&
          !compatibleAssessment.hasWarnings(),
          "assesses the Slide's observed preparation against the pinned SampleType and resolved microscope");

    const auto incompatiblePreparation = take(SlidePreparation::create(
        SpecimenPreparationMethod::TissueSection,
        SpecimenFixationState::Fixed,
        10.0,
        170.0,
        "Antifade mounting medium",
        1.46));
    const auto incompatibleSlide = take(Slide::create(
        id<SlideId>("assessment-slide-incompatible"),
        1,
        sampleType.id(),
        sampleType.revision(),
        "OPT-INCOMPAT-001",
        "Out-of-spec slide",
        geometry,
        incompatiblePreparation));
    const auto incompatibleAssessment = SampleMicroscopyRequirementAssessor::assess(
        fixture.equipment,
        sampleType,
        incompatibleSlide,
        *resolution.resolvedConfiguration());
    check(!incompatibleAssessment.isCompatible() &&
          hasCode(incompatibleAssessment.diagnostics(),
                  "sample.preparation.section_thickness_above_maximum") &&
          hasCode(incompatibleAssessment.diagnostics(),
                  "sample.preparation.required_label_missing"),
          "fails the Slide when observed thickness and label facts violate SampleType preparation constraints");

    const auto staleTypeSlide = take(Slide::create(
        id<SlideId>("assessment-slide-stale-type"),
        1,
        sampleType.id(),
        sampleType.revision() - 1,
        "OPT-STALE-001",
        "Slide pinned to a stale SampleType revision",
        geometry,
        compatiblePreparation));
    const auto staleAssessment = SampleMicroscopyRequirementAssessor::assess(
        fixture.equipment,
        sampleType,
        staleTypeSlide,
        *resolution.resolvedConfiguration());
    check(!staleAssessment.isCompatible() &&
          hasCode(staleAssessment.diagnostics(),
                  "sample.microscopy.slide_sample_type_revision_mismatch"),
          "rejects an assessor call that supplies a SampleType revision different from the revision pinned by the Slide");
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
        fixture.hardwareConfiguration,
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
        fixture.equipment.lightPaths(),
        fixture.equipment.filterSpecifications(),
        fixture.equipment.revision() + 1));

    const auto staleResolution = LightPathCameraResolver::resolve(
        changedEquipment, fixture.operationMode, fixture.lightPathId);
    check(staleResolution.isResolved(),
          "camera discovery continues to use physical topology after equipment revision changes");

    const auto staleHardwareRequest = LightPathConfigurationRequestFactory::create(
        changedEquipment,
        fixture.operationMode,
        fixture.hardwareConfiguration,
        fixture.lightPathId,
        staleResolution,
        fixture.profileId,
        fixture.objectiveId,
        fixture.condenserId,
        fixture.sourceId,
        fixture.channelId,
        ExposurePolicy::Manual);
    check(!staleHardwareRequest &&
          staleHardwareRequest.error() ==
              LightPathConfigurationRequestFactoryError::HardwareConfigurationEquipmentRevisionMismatch,
          "request creation rejects a stale calibrated realization independently of camera discovery");
}

} // namespace

int main() {
    testCompleteLightPathToDesiredConfigurationWorkflow();
    testSlideAssessmentUsesPinnedSampleTypeAndObservedSlideFacts();
    testEquipmentRevisionChangeInvalidatesModeBoundLightPath();

    if (failures != 0) {
        std::cerr << failures << " optical configuration workflow integration test(s) failed\n";
        return 1;
    }
    std::cout << "Optical configuration workflow integration tests passed\n";
    return 0;
}
