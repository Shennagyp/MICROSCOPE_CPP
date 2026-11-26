#include "microscope/core/configuration/Configuration.h"
#include "microscope/core/configuration/ConfigurationTransition.h"
#include "microscope/core/configuration/LightPathCameraResolver.h"
#include "microscope/core/configuration/LightPathConfigurationRequestFactory.h"
#include "microscope/core/configuration/OperationMode.h"
#include "microscope/core/configuration/SampleMicroscopyRequirementAssessor.h"
#include "microscope/core/samples/SampleModels.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
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
        throw std::runtime_error("A configuration test fixture factory rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

PositiveMicrometers micrometers(double value) {
    return take(PositiveMicrometers::create(value));
}

PositiveMicroseconds microseconds(double value) {
    return take(PositiveMicroseconds::create(value));
}

NonNegativeNumericalAperture nonNegativeAperture(double value) {
    return take(NonNegativeNumericalAperture::create(value));
}

NominalMagnification magnification(double value) {
    return take(NominalMagnification::create(value));
}

NumericalAperture aperture(double value) {
    return take(NumericalAperture::create(value));
}

OpticalPort port(const char* id, OpticalPortDirection direction) {
    return take(OpticalPort::create(makeId<OpticalPortId>(id), direction));
}

OpticalFunctionCapability opticalCapability(
    OpticalElementFunction function,
    const OpticalCompatibilityId& compatibilityId) {
    return take(OpticalFunctionCapability::create(function, compatibilityId));
}

EquipmentComponent opticalElement(
    const char* componentId,
    const char* inputPortId,
    const char* outputPortId,
    std::vector<OpticalFunctionCapability> opticalFunctions = {}) {
    return take(EquipmentComponent::create(
        makeId<EquipmentComponentId>(componentId),
        EquipmentComponentKind::OpticalElement,
        {port(inputPortId, OpticalPortDirection::Input),
         port(outputPortId, OpticalPortDirection::Output)},
        std::move(opticalFunctions)));
}

OpticalPathStep pathStep(
    const EquipmentComponentId& componentId,
    std::optional<OpticalPortId> entry,
    std::optional<OpticalPortId> exit) {
    return take(OpticalPathStep::create(
        componentId, std::move(entry), std::move(exit)));
}

OpticalConnection connection(
    const char* id,
    const OpticalPortId& first,
    const OpticalPortId& second) {
    return take(OpticalConnection::create(
        makeId<OpticalConnectionId>(id), first, second));
}

struct ConfigurationFixture final {
    MicroscopeId microscopeId;

    EquipmentComponentId illuminationId;
    EquipmentComponentId condenserId;
    EquipmentComponentId objectiveId;
    EquipmentComponentId cameraId;

    OpticalPortId illuminationOutput;
    OpticalPortId condenserInput;
    OpticalPortId condenserOutput;
    OpticalPortId objectiveInput;
    OpticalPortId objectiveOutput;
    OpticalPortId cameraInput;

    ConfigurationProfileId brightfieldProfileId;

    OpticalPathId fullPathId;
    OpticalPathId illuminationPathId;
    OpticalPathId imagingPathId;

    CameraModeId cameraModeId;
    IlluminationChannelId illuminationChannelId;

    MicroscopeEquipmentDefinition equipment;
    ConfigurationProfileCatalog profiles;
};

ConfigurationFixture makeConfigurationFixture() {
    const auto microscopeId = makeId<MicroscopeId>("configuration-microscope");
    const auto illuminationId = makeId<EquipmentComponentId>("illumination-transmitted");
    const auto condenserId = makeId<EquipmentComponentId>("condenser-abbe");
    const auto objectiveId = makeId<EquipmentComponentId>("objective-10x");
    const auto cameraId = makeId<EquipmentComponentId>("camera-main");

    const auto illuminationOutput = makeId<OpticalPortId>("illumination-output");
    const auto condenserInput = makeId<OpticalPortId>("condenser-input");
    const auto condenserOutput = makeId<OpticalPortId>("condenser-output");
    const auto objectiveInput = makeId<OpticalPortId>("objective-input");
    const auto objectiveOutput = makeId<OpticalPortId>("objective-output");
    const auto cameraInput = makeId<OpticalPortId>("camera-input");

    const auto fullPathId = makeId<OpticalPathId>("brightfield-full-path");
    const auto illuminationPathId = makeId<OpticalPathId>("illumination-path");
    const auto imagingPathId = makeId<OpticalPathId>("imaging-path");

    const auto cameraModeId = makeId<CameraModeId>("camera-mono8");
    const auto illuminationChannelId =
        makeId<IlluminationChannelId>("transmitted-white");

    std::vector<EquipmentComponent> components;
    components.push_back(take(EquipmentComponent::create(
        illuminationId,
        EquipmentComponentKind::IlluminationDevice,
        {take(OpticalPort::create(
            illuminationOutput, OpticalPortDirection::Output))})));
    const auto phaseCompatibilityId =
        makeId<OpticalCompatibilityId>("phase-compatibility-1");
    const auto dicCompatibilityId =
        makeId<OpticalCompatibilityId>("dic-compatibility-1");

    components.push_back(opticalElement(
        "condenser-abbe", "condenser-input", "condenser-output",
        {opticalCapability(
            OpticalElementFunction::PhaseAnnulus,
            phaseCompatibilityId)}));
    components.push_back(opticalElement(
        "polarizer", "polarizer-input", "polarizer-output",
        {opticalCapability(
            OpticalElementFunction::Polarizer,
            dicCompatibilityId)}));
    components.push_back(opticalElement(
        "dic-prism-one", "dic-prism-one-input", "dic-prism-one-output",
        {opticalCapability(
            OpticalElementFunction::DifferentialInterferencePrism,
            dicCompatibilityId)}));
    components.push_back(opticalElement(
        "objective-10x", "objective-input", "objective-output",
        {opticalCapability(
            OpticalElementFunction::PhasePlate,
            phaseCompatibilityId)}));
    components.push_back(opticalElement(
        "dic-prism-two", "dic-prism-two-input", "dic-prism-two-output",
        {opticalCapability(
            OpticalElementFunction::DifferentialInterferencePrism,
            dicCompatibilityId)}));
    components.push_back(opticalElement(
        "analyzer", "analyzer-input", "analyzer-output",
        {opticalCapability(
            OpticalElementFunction::Analyzer,
            dicCompatibilityId)}));
    components.push_back(take(EquipmentComponent::create(
        cameraId,
        EquipmentComponentKind::ImagingDevice,
        {take(OpticalPort::create(
            cameraInput, OpticalPortDirection::Input))})));

    const auto polarizerInput = makeId<OpticalPortId>("polarizer-input");
    const auto polarizerOutput = makeId<OpticalPortId>("polarizer-output");
    const auto dicPrismOneInput = makeId<OpticalPortId>("dic-prism-one-input");
    const auto dicPrismOneOutput = makeId<OpticalPortId>("dic-prism-one-output");
    const auto dicPrismTwoInput = makeId<OpticalPortId>("dic-prism-two-input");
    const auto dicPrismTwoOutput = makeId<OpticalPortId>("dic-prism-two-output");
    const auto analyzerInput = makeId<OpticalPortId>("analyzer-input");
    const auto analyzerOutput = makeId<OpticalPortId>("analyzer-output");

    std::vector<OpticalConnection> connections{
        connection("connection-source-condenser",
                   illuminationOutput, condenserInput),
        connection("connection-condenser-polarizer",
                   condenserOutput, polarizerInput),
        connection("connection-polarizer-prism-one",
                   polarizerOutput, dicPrismOneInput),
        connection("connection-prism-one-objective",
                   dicPrismOneOutput, objectiveInput),
        connection("connection-objective-prism-two",
                   objectiveOutput, dicPrismTwoInput),
        connection("connection-prism-two-analyzer",
                   dicPrismTwoOutput, analyzerInput),
        connection("connection-analyzer-camera",
                   analyzerOutput, cameraInput)};

    std::vector<OpticalPath> paths;
    paths.push_back(take(OpticalPath::create(
        fullPathId,
        {pathStep(illuminationId, std::nullopt, illuminationOutput),
         pathStep(condenserId, condenserInput, condenserOutput),
         pathStep(makeId<EquipmentComponentId>("polarizer"),
                  polarizerInput, polarizerOutput),
         pathStep(makeId<EquipmentComponentId>("dic-prism-one"),
                  dicPrismOneInput, dicPrismOneOutput),
         pathStep(objectiveId, objectiveInput, objectiveOutput),
         pathStep(makeId<EquipmentComponentId>("dic-prism-two"),
                  dicPrismTwoInput, dicPrismTwoOutput),
         pathStep(makeId<EquipmentComponentId>("analyzer"),
                  analyzerInput, analyzerOutput),
         pathStep(cameraId, cameraInput, std::nullopt)},
        OpticalPathRole::Imaging)));

    paths.push_back(take(OpticalPath::create(
        illuminationPathId,
        {pathStep(illuminationId, std::nullopt, illuminationOutput),
         pathStep(condenserId, condenserInput, condenserOutput),
         pathStep(makeId<EquipmentComponentId>("polarizer"),
                  polarizerInput, polarizerOutput),
         pathStep(makeId<EquipmentComponentId>("dic-prism-one"),
                  dicPrismOneInput, dicPrismOneOutput),
         pathStep(objectiveId, objectiveInput, std::nullopt)},
        OpticalPathRole::Illumination)));

    paths.push_back(take(OpticalPath::create(
        imagingPathId,
        {pathStep(objectiveId, std::nullopt, objectiveOutput),
         pathStep(makeId<EquipmentComponentId>("dic-prism-two"),
                  dicPrismTwoInput, dicPrismTwoOutput),
         pathStep(makeId<EquipmentComponentId>("analyzer"),
                  analyzerInput, analyzerOutput),
         pathStep(cameraId, cameraInput, std::nullopt)},
        OpticalPathRole::Imaging)));

    const auto objectiveSpecification = take(ObjectiveSpecification::create(
        objectiveId,
        magnification(10.0),
        aperture(0.25),
        micrometers(6'000.0),
        ObjectiveChromaticCorrection::Achromatic,
        ObjectiveFieldCorrection::Plan,
        ObjectiveImmersionMedium::Air));

    const auto cameraSpecification = take(CameraSpecification::create(
        cameraId,
        take(PixelDimensions::create(640, 480)),
        micrometers(3.45),
        micrometers(3.60),
        {take(CameraOutputMode::create(
            cameraModeId,
            take(SensorRegionOfInterest::create(0, 0, 640, 480)),
            take(PixelBinning::create(1, 1)),
            take(PixelDimensions::create(640, 480)),
            CameraPixelLayout::Monochrome,
            "Mono8"))},
        take(ExposureTimeRange::create(
            microseconds(100.0),
            microseconds(1'000'000.0)))));

    const auto condenserSpecification = take(CondenserSpecification::create(
        condenserId,
        take(NonNegativeNumericalAperture::create(0.0)),
        aperture(0.90),
        CondenserDesign::Abbe,
        CondenserImmersionMedium::Air));

    const auto spectralBand = take(SpectralBand::create(
        take(PositiveNanometers::create(400.0)),
        take(PositiveNanometers::create(700.0))));
    const auto intensityRange = take(IlluminationIntensityRange::create(
        0.0, 100.0, IlluminationIntensityUnit::Percent));
    const auto channel = take(IlluminationChannel::create(
        illuminationChannelId,
        spectralBand,
        IlluminationControlKind::Continuous,
        intensityRange));
    const auto illuminationSpecification = take(IlluminationSpecification::create(
        illuminationId,
        IlluminationSourceTechnology::Led,
        {channel}));

    auto equipment = take(MicroscopeEquipmentDefinition::create(
        microscopeId,
        std::move(components),
        std::move(connections),
        std::move(paths),
        {objectiveSpecification},
        {cameraSpecification},
        {condenserSpecification},
        {illuminationSpecification}));

    const auto profileId =
        makeId<ConfigurationProfileId>("profile-brightfield");
    const auto profile = take(ConfigurationProfile::create(
        profileId,
        ContrastMethod::Brightfield,
        {objectiveId},
        {condenserId},
        {fullPathId, illuminationPathId, imagingPathId},
        {cameraId},
        {illuminationId},
        {objectiveId}));
    auto profiles = take(ConfigurationProfileCatalog::create({profile}));

    return ConfigurationFixture{
        microscopeId,
        illuminationId,
        condenserId,
        objectiveId,
        cameraId,
        illuminationOutput,
        condenserInput,
        condenserOutput,
        objectiveInput,
        objectiveOutput,
        cameraInput,
        profileId,
        fullPathId,
        illuminationPathId,
        imagingPathId,
        cameraModeId,
        illuminationChannelId,
        std::move(equipment),
        std::move(profiles)};
}

ConfigurationRequest makeRequest(
    const ConfigurationFixture& fixture,
    std::vector<OpticalPathId> pathIds,
    CameraModeId cameraModeId,
    ContrastMethod contrastMethod = ContrastMethod::Brightfield,
    ConfigurationProfileId profileId =
        makeId<ConfigurationProfileId>("profile-brightfield"),
    std::vector<EquipmentComponentId> selectedAdditionalOpticalComponentIds = {}) {
    return take(ConfigurationRequest::create(
        profileId,
        contrastMethod,
        fixture.objectiveId,
        fixture.condenserId,
        std::move(pathIds),
        fixture.cameraId,
        cameraModeId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Manual,
        std::move(selectedAdditionalOpticalComponentIds)));
}

bool hasDiagnosticCode(
    const ConfigurationResolution& resolution,
    const std::string& code) {
    for (const auto& diagnostic : resolution.diagnostics()) {
        if (diagnostic.code() == code) {
            return true;
        }
    }
    return false;
}

void testConfigurationFactoriesRejectInvalidSets() {
    const auto fixture = makeConfigurationFixture();

    auto unknownContrast = ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("invalid-contrast-profile"),
        ContrastMethod::Unknown,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId},
        {fixture.cameraId},
        {fixture.illuminationId});
    check(!unknownContrast &&
          unknownContrast.error() == ConfigurationProfileError::InvalidContrastMethod,
          "rejects an unknown profile contrast method");

    auto duplicateObjective = ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("duplicate-objective-profile"),
        ContrastMethod::Brightfield,
        {fixture.objectiveId, fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId},
        {fixture.cameraId},
        {fixture.illuminationId});
    check(!duplicateObjective &&
          duplicateObjective.error() == ConfigurationProfileError::DuplicateObjective,
          "rejects duplicate objective compatibility entries");

    auto emptyCatalog = ConfigurationProfileCatalog::create({});
    check(!emptyCatalog &&
          emptyCatalog.error() == ConfigurationProfileCatalogError::EmptyCatalog,
          "rejects an empty configuration profile catalog");

    auto duplicateProfileCatalog = ConfigurationProfileCatalog::create(
        {fixture.profiles.profiles().front(),
         fixture.profiles.profiles().front()});
    check(!duplicateProfileCatalog &&
          duplicateProfileCatalog.error() ==
              ConfigurationProfileCatalogError::DuplicateProfileId,
          "rejects duplicate profile identities in one catalog");

    auto emptyPathRequest = ConfigurationRequest::create(
        fixture.brightfieldProfileId,
        ContrastMethod::Brightfield,
        fixture.objectiveId,
        fixture.condenserId,
        {},
        fixture.cameraId,
        fixture.cameraModeId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Manual);
    check(!emptyPathRequest &&
          emptyPathRequest.error() ==
              ConfigurationRequestError::EmptyOpticalPathSet,
          "requires at least one selected optical path");

    auto duplicatePathRequest = ConfigurationRequest::create(
        fixture.brightfieldProfileId,
        ContrastMethod::Brightfield,
        fixture.objectiveId,
        fixture.condenserId,
        {fixture.fullPathId, fixture.fullPathId},
        fixture.cameraId,
        fixture.cameraModeId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Manual);
    check(!duplicatePathRequest &&
          duplicatePathRequest.error() ==
              ConfigurationRequestError::DuplicateOpticalPath,
          "rejects duplicate selected optical paths");

    auto invalidExposurePolicy = ConfigurationRequest::create(
        fixture.brightfieldProfileId,
        ContrastMethod::Brightfield,
        fixture.objectiveId,
        fixture.condenserId,
        {fixture.fullPathId},
        fixture.cameraId,
        fixture.cameraModeId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        static_cast<ExposurePolicy>(255));
    check(!invalidExposurePolicy &&
          invalidExposurePolicy.error() ==
              ConfigurationRequestError::InvalidExposurePolicy,
          "rejects an invalid exposure policy enum value");

    auto unspecifiedExposurePolicy = ConfigurationRequest::create(
        fixture.brightfieldProfileId,
        ContrastMethod::Brightfield,
        fixture.objectiveId,
        fixture.condenserId,
        {fixture.fullPathId},
        fixture.cameraId,
        fixture.cameraModeId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Unknown);
    check(!unspecifiedExposurePolicy &&
          unspecifiedExposurePolicy.error() ==
              ConfigurationRequestError::InvalidExposurePolicy,
          "requires an explicit exposure policy rather than an unknown sentinel");
}

void testResolverAcceptsOneCompleteBrightfieldPath() {
    const auto fixture = makeConfigurationFixture();
    const auto request = makeRequest(
        fixture, {fixture.fullPathId}, fixture.cameraModeId);

    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, request);

    check(resolution.isResolved(),
          "resolves a compatible brightfield path with installed objective/camera/illumination specs");
    check(resolution.resolvedConfiguration().has_value() &&
          resolution.resolvedConfiguration()->microscopeId() == fixture.microscopeId &&
          resolution.resolvedConfiguration()->request().objectiveComponentId() == fixture.objectiveId &&
          resolution.resolvedConfiguration()->request().cameraModeId() == fixture.cameraModeId,
          "returns the immutable selected request as the resolved value");
    check(resolution.diagnostics().empty(),
          "compatible brightfield resolution produces no error diagnostics");
}

void testResolverAcceptsSeparateIlluminationAndImagingPaths() {
    const auto fixture = makeConfigurationFixture();
    const auto request = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId);

    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, request);

    check(resolution.isResolved(),
          "resolves illumination and imaging components across a selected path set");
    check(resolution.diagnostics().empty(),
          "valid split-path resolution produces no diagnostics");
}

void testPhaseContrastRequiresMatchedPhasePlateAndAnnulus() {
    const auto fixture = makeConfigurationFixture();
    const auto phaseCompatibilityId =
        makeId<OpticalCompatibilityId>("phase-compatibility-1");

    const auto phaseProfile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-phase-contrast"),
        ContrastMethod::PhaseContrast,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId, fixture.illuminationPathId, fixture.imagingPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhasePlate,
             phaseCompatibilityId,
             1,
             OpticalPathRole::Imaging)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhaseAnnulus,
             phaseCompatibilityId,
             1,
             OpticalPathRole::Illumination))}));
    const auto phaseCatalog =
        take(ConfigurationProfileCatalog::create({phaseProfile}));
    const auto phaseRequest = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::PhaseContrast,
        phaseProfile.id());

    const auto compatible = ConfigurationResolver::resolve(
        fixture.equipment, phaseCatalog, phaseRequest);
    check(compatible.isResolved(),
          "accepts phase contrast when selected objective phase plate and condenser annulus share a compatibility identity");

    const auto wrongRouteProfile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-phase-wrong-route"),
        ContrastMethod::PhaseContrast,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId, fixture.illuminationPathId, fixture.imagingPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhasePlate,
             phaseCompatibilityId,
             1,
             OpticalPathRole::Illumination)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhaseAnnulus,
             phaseCompatibilityId,
             1,
             OpticalPathRole::Imaging))}));
    const auto wrongRouteCatalog =
        take(ConfigurationProfileCatalog::create({wrongRouteProfile}));
    const auto wrongRouteRequest = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::PhaseContrast,
        wrongRouteProfile.id());
    const auto wrongRoute = ConfigurationResolver::resolve(
        fixture.equipment, wrongRouteCatalog, wrongRouteRequest);
    check(!wrongRoute.isResolved() &&
          hasDiagnosticCode(
              wrongRoute,
              "configuration.optical_function.requirement_not_satisfied"),
          "rejects phase hardware on incorrect optical path roles despite matching components and compatibility identity");

    const auto mismatchedCompatibilityId =
        makeId<OpticalCompatibilityId>("phase-compatibility-unsupported");
    const auto mismatchedProfile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-phase-mismatch"),
        ContrastMethod::PhaseContrast,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhasePlate, mismatchedCompatibilityId)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhaseAnnulus, mismatchedCompatibilityId))}));
    const auto mismatchedCatalog =
        take(ConfigurationProfileCatalog::create({mismatchedProfile}));
    const auto mismatchedRequest = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::PhaseContrast,
        mismatchedProfile.id());

    const auto incompatible = ConfigurationResolver::resolve(
        fixture.equipment, mismatchedCatalog, mismatchedRequest);
    check(!incompatible.isResolved() &&
          hasDiagnosticCode(
              incompatible,
              "configuration.optical_function.requirement_not_satisfied"),
          "rejects phase contrast when the selected phase components do not match the required compatibility identity");

}

void testAdditionalOpticalComponentMustSatisfyProfileRequirement() {
    const auto fixture = makeConfigurationFixture();
    const auto phaseCompatibilityId =
        makeId<OpticalCompatibilityId>("phase-compatibility-1");
    const auto phaseProfile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-phase-additional-component"),
        ContrastMethod::PhaseContrast,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId, fixture.illuminationPathId, fixture.imagingPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhasePlate, phaseCompatibilityId)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhaseAnnulus, phaseCompatibilityId))}));
    const auto phaseCatalog =
        take(ConfigurationProfileCatalog::create({phaseProfile}));
    const auto unrelatedRequest = makeRequest(
        fixture,
        {fixture.fullPathId},
        fixture.cameraModeId,
        ContrastMethod::PhaseContrast,
        phaseProfile.id(),
        {makeId<EquipmentComponentId>("polarizer")});
    const auto unrelatedResolution = ConfigurationResolver::resolve(
        fixture.equipment, phaseCatalog, unrelatedRequest);

    check(!unrelatedResolution.isResolved() &&
          hasDiagnosticCode(
              unrelatedResolution,
              "configuration.additional_optical_component.not_allowed_by_profile"),
          "rejects an additional optical component that does not satisfy any selected profile requirement");
}

void testDicRequiresPolarizerAnalyzerAndTwoMatchedPrisms() {
    const auto fixture = makeConfigurationFixture();
    const auto dicCompatibilityId =
        makeId<OpticalCompatibilityId>("dic-compatibility-1");

    const auto dicProfile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-dic"),
        ContrastMethod::DifferentialInterferenceContrast,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId, fixture.illuminationPathId, fixture.imagingPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {take(OpticalFunctionRequirement::create(
             OpticalElementFunction::Polarizer,
             dicCompatibilityId,
             1,
             OpticalPathRole::Illumination)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::Analyzer,
             dicCompatibilityId,
             1,
             OpticalPathRole::Imaging)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::DifferentialInterferencePrism,
             dicCompatibilityId,
             1,
             OpticalPathRole::Illumination)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::DifferentialInterferencePrism,
             dicCompatibilityId,
             1,
             OpticalPathRole::Imaging))}));
    const auto dicCatalog = take(ConfigurationProfileCatalog::create({dicProfile}));

    const auto polarizerId = makeId<EquipmentComponentId>("polarizer");
    const auto prismOneId = makeId<EquipmentComponentId>("dic-prism-one");
    const auto prismTwoId = makeId<EquipmentComponentId>("dic-prism-two");
    const auto analyzerId = makeId<EquipmentComponentId>("analyzer");

    const auto validDicRequest = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::DifferentialInterferenceContrast,
        dicProfile.id(),
        {polarizerId, prismOneId, prismTwoId, analyzerId});
    const auto validDic = ConfigurationResolver::resolve(
        fixture.equipment, dicCatalog, validDicRequest);
    check(validDic.isResolved(),
          "accepts a DIC configuration with matched polarizer, analyzer and two prism components");

    const auto incompleteDicRequest = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::DifferentialInterferenceContrast,
        dicProfile.id(),
        {polarizerId, prismOneId, analyzerId});
    const auto incompleteDic = ConfigurationResolver::resolve(
        fixture.equipment, dicCatalog, incompleteDicRequest);
    check(!incompleteDic.isResolved() &&
          hasDiagnosticCode(
              incompleteDic,
              "configuration.optical_function.requirement_not_satisfied"),
          "rejects a DIC configuration missing its second required prism");
}

void testPolarizedLightProfileRequiresMatchedPolarizerAndAnalyzer() {
    const auto fixture = makeConfigurationFixture();
    const auto compatibilityId =
        makeId<OpticalCompatibilityId>("dic-compatibility-1");
    const auto profile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-polarized-light"),
        ContrastMethod::PolarizedLight,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId, fixture.illuminationPathId, fixture.imagingPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {take(OpticalFunctionRequirement::create(
             OpticalElementFunction::Polarizer,
             compatibilityId,
             1,
             OpticalPathRole::Illumination)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::Analyzer,
             compatibilityId,
             1,
             OpticalPathRole::Imaging))}));
    const auto catalog = take(ConfigurationProfileCatalog::create({profile}));
    const auto polarizerId = makeId<EquipmentComponentId>("polarizer");
    const auto analyzerId = makeId<EquipmentComponentId>("analyzer");
    const auto validRequest = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::PolarizedLight,
        profile.id(),
        {polarizerId, analyzerId});

    const auto valid = ConfigurationResolver::resolve(
        fixture.equipment, catalog, validRequest);
    check(valid.isResolved(),
          "accepts polarized light with a compatible polarizer and analyzer across separate optical paths");

    const auto missingAnalyzerRequest = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::PolarizedLight,
        profile.id(),
        {polarizerId});
    const auto missingAnalyzer = ConfigurationResolver::resolve(
        fixture.equipment, catalog, missingAnalyzerRequest);
    check(!missingAnalyzer.isResolved() &&
          hasDiagnosticCode(
              missingAnalyzer,
              "configuration.optical_function.requirement_not_satisfied"),
          "rejects polarized light when the matching analyzer is not selected");
}

void testOpticalFunctionRequirementFactoriesAndSelections() {
    const auto fixture = makeConfigurationFixture();
    auto zeroCount = OpticalFunctionRequirement::create(
        OpticalElementFunction::DifferentialInterferencePrism,
        std::nullopt,
        0);
    check(!zeroCount &&
          zeroCount.error() ==
              OpticalFunctionRequirementError::ZeroMinimumComponentCount,
          "rejects an optical function requirement with zero minimum count");

    auto invalidPathRole = OpticalFunctionRequirement::create(
        OpticalElementFunction::PhasePlate,
        std::nullopt,
        1,
        static_cast<OpticalPathRole>(255));
    check(!invalidPathRole &&
          invalidPathRole.error() ==
              OpticalFunctionRequirementError::InvalidPathRole,
          "rejects an unknown required optical path role");

    auto invalidFunction = OpticalFunctionRequirement::create(
        static_cast<OpticalElementFunction>(255));
    check(!invalidFunction &&
          invalidFunction.error() ==
              OpticalFunctionRequirementError::InvalidFunction,
          "rejects an unknown optical function requirement");

    const auto phaseCompatibilityId =
        makeId<OpticalCompatibilityId>("phase-compatibility-1");
    const auto duplicateRequirement = ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("duplicate-function-requirement-profile"),
        ContrastMethod::Brightfield,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhasePlate, phaseCompatibilityId)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhasePlate, phaseCompatibilityId))});
    check(!duplicateRequirement &&
          duplicateRequirement.error() ==
              ConfigurationProfileError::DuplicateOpticalFunctionRequirement,
          "rejects duplicate optical function requirements");

    auto duplicateSelection = ConfigurationRequest::create(
        fixture.brightfieldProfileId,
        ContrastMethod::Brightfield,
        fixture.objectiveId,
        fixture.condenserId,
        {fixture.fullPathId},
        fixture.cameraId,
        fixture.cameraModeId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Manual,
        {makeId<EquipmentComponentId>("polarizer"),
         makeId<EquipmentComponentId>("polarizer")});
    check(!duplicateSelection &&
          duplicateSelection.error() ==
              ConfigurationRequestError::DuplicateAdditionalOpticalComponent,
          "rejects duplicate additional optical component selections");
}

void testProfileTargetsAreRetainedAndValidatedAgainstInstalledRanges() {
    const auto fixture = makeConfigurationFixture();

    const auto createTargetProfile = [&fixture](
        const char* id,
        std::optional<NonNegativeNumericalAperture> apertureTarget,
        std::optional<IlluminationIntensitySetting> intensityTarget,
        std::optional<ExposurePreset> exposurePreset) {
        return take(ConfigurationProfile::create(
            makeId<ConfigurationProfileId>(id),
            ContrastMethod::Brightfield,
            {fixture.objectiveId},
            {fixture.condenserId},
            {fixture.fullPathId, fixture.illuminationPathId, fixture.imagingPathId},
            {fixture.cameraId},
            {fixture.illuminationId},
            {},
            {},
            std::move(apertureTarget),
            std::move(intensityTarget),
            std::move(exposurePreset)));
    };

    const auto targetProfile = createTargetProfile(
        "profile-with-targets",
        nonNegativeAperture(0.65),
        take(IlluminationIntensitySetting::create(
            55.0, IlluminationIntensityUnit::Percent)),
        ExposurePreset::create(microseconds(20'000.0)));
    const auto targetCatalog =
        take(ConfigurationProfileCatalog::create({targetProfile}));
    const auto request = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::Brightfield,
        targetProfile.id());

    const auto resolved = ConfigurationResolver::resolve(
        fixture.equipment, targetCatalog, request);
    check(resolved.isResolved(),
          "accepts profile targets inside condenser, illumination and camera capability ranges");
    if (resolved.resolvedConfiguration()) {
        const auto& desired = *resolved.resolvedConfiguration();
        check(desired.microscopeId() == fixture.microscopeId,
              "keeps the resolved target scoped to the installed microscope identity");
        check(desired.condenserApertureTarget().has_value() &&
              desired.condenserApertureTarget()->value() == 0.65,
              "retains the resolved condenser aperture target");
        check(desired.illuminationIntensityTarget().has_value() &&
              desired.illuminationIntensityTarget()->value() == 55.0 &&
              desired.illuminationIntensityTarget()->unit() ==
                  IlluminationIntensityUnit::Percent,
              "retains the resolved illumination target and its explicit unit");
        check(desired.exposurePreset().has_value() &&
              desired.exposurePreset()->exposureTimeMicroseconds() == 20'000.0,
              "retains the initial exposure preset independently from exposure policy");
        check(desired.request().exposurePolicy() == ExposurePolicy::Manual,
              "does not rewrite the explicit manual exposure policy when a profile supplies a preset");
    }

    const auto apertureOutOfRangeProfile = createTargetProfile(
        "profile-aperture-out-of-range",
        nonNegativeAperture(1.0),
        std::nullopt,
        std::nullopt);
    const auto apertureOutOfRange = ConfigurationResolver::resolve(
        fixture.equipment,
        take(ConfigurationProfileCatalog::create({apertureOutOfRangeProfile})),
        makeRequest(
            fixture, {fixture.fullPathId}, fixture.cameraModeId,
            ContrastMethod::Brightfield, apertureOutOfRangeProfile.id()));
    check(!apertureOutOfRange.isResolved() &&
          hasDiagnosticCode(
              apertureOutOfRange,
              "configuration.condenser.aperture_target_out_of_range"),
          "rejects a condenser aperture target above the installed aperture NA maximum");

    const auto intensityOutOfRangeProfile = createTargetProfile(
        "profile-intensity-out-of-range",
        std::nullopt,
        take(IlluminationIntensitySetting::create(
            101.0, IlluminationIntensityUnit::Percent)),
        std::nullopt);
    const auto intensityOutOfRange = ConfigurationResolver::resolve(
        fixture.equipment,
        take(ConfigurationProfileCatalog::create({intensityOutOfRangeProfile})),
        makeRequest(
            fixture, {fixture.fullPathId}, fixture.cameraModeId,
            ContrastMethod::Brightfield, intensityOutOfRangeProfile.id()));
    check(!intensityOutOfRange.isResolved() &&
          hasDiagnosticCode(
              intensityOutOfRange,
              "configuration.illumination.intensity_target_out_of_range"),
          "rejects an illumination target above the selected channel's range");

    const auto intensityUnitMismatchProfile = createTargetProfile(
        "profile-intensity-unit-mismatch",
        std::nullopt,
        take(IlluminationIntensitySetting::create(
            20.0, IlluminationIntensityUnit::Milliwatts)),
        std::nullopt);
    const auto intensityUnitMismatch = ConfigurationResolver::resolve(
        fixture.equipment,
        take(ConfigurationProfileCatalog::create({intensityUnitMismatchProfile})),
        makeRequest(
            fixture, {fixture.fullPathId}, fixture.cameraModeId,
            ContrastMethod::Brightfield, intensityUnitMismatchProfile.id()));
    check(!intensityUnitMismatch.isResolved() &&
          hasDiagnosticCode(
              intensityUnitMismatch,
              "configuration.illumination.intensity_unit_mismatch"),
          "rejects an intensity target whose unit differs from the selected channel's unit");

    const auto exposureOutOfRangeProfile = createTargetProfile(
        "profile-exposure-out-of-range",
        std::nullopt,
        std::nullopt,
        ExposurePreset::create(microseconds(2'000'000.0)));
    const auto exposureOutOfRange = ConfigurationResolver::resolve(
        fixture.equipment,
        take(ConfigurationProfileCatalog::create({exposureOutOfRangeProfile})),
        makeRequest(
            fixture, {fixture.fullPathId}, fixture.cameraModeId,
            ContrastMethod::Brightfield, exposureOutOfRangeProfile.id()));
    check(!exposureOutOfRange.isResolved() &&
          hasDiagnosticCode(
              exposureOutOfRange,
              "configuration.camera.exposure_preset_out_of_range"),
          "rejects an exposure preset above the selected camera's declared capability range");

    const auto cameraWithoutExposureRange = take(CameraSpecification::create(
        fixture.cameraId,
        fixture.equipment.cameraSpecifications().front().activeSensorDimensions(),
        micrometers(
            fixture.equipment.cameraSpecifications().front().pixelPitchXMicrometers()),
        micrometers(
            fixture.equipment.cameraSpecifications().front().pixelPitchYMicrometers()),
        fixture.equipment.cameraSpecifications().front().outputModes()));
    auto equipmentWithoutExposureCapability = take(
        MicroscopeEquipmentDefinition::create(
            fixture.microscopeId,
            fixture.equipment.components(),
            fixture.equipment.connections(),
            fixture.equipment.opticalPaths(),
            fixture.equipment.objectiveSpecifications(),
            {cameraWithoutExposureRange},
            fixture.equipment.condenserSpecifications(),
            fixture.equipment.illuminationSpecifications(),
            fixture.equipment.stageSpecifications()));
    const auto missingExposureCapability = ConfigurationResolver::resolve(
        equipmentWithoutExposureCapability,
        targetCatalog,
        request);
    check(!missingExposureCapability.isResolved() &&
          hasDiagnosticCode(
              missingExposureCapability,
              "configuration.camera.exposure_capability_missing"),
          "requires explicit camera exposure-time capability data before accepting a profile preset");
}

void testTransitionPlannerProducesTypedRequirementsAndDependencyLayers() {
    const auto fixture = makeConfigurationFixture();
    const auto targetProfile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-transition-targets"),
        ContrastMethod::Brightfield,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId, fixture.illuminationPathId, fixture.imagingPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {},
        nonNegativeAperture(0.60),
        take(IlluminationIntensitySetting::create(
            50.0, IlluminationIntensityUnit::Percent)),
        ExposurePreset::create(microseconds(30'000.0))));
    const auto catalog =
        take(ConfigurationProfileCatalog::create({targetProfile}));
    const auto request = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::Brightfield,
        targetProfile.id());
    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, catalog, request);
    check(resolution.isResolved(),
          "precondition: transition planner receives a valid resolved configuration");
    if (!resolution.resolvedConfiguration()) {
        return;
    }

    const auto plan = take(ConfigurationTransitionPlanner::plan(
        *resolution.resolvedConfiguration()));
    check(plan.desiredConfiguration().microscopeId() == fixture.microscopeId,
          "keeps the transition plan scoped to the resolved microscope identity");

    const ConfigurationTransitionStep* gate = nullptr;
    const ConfigurationTransitionStep* paths = nullptr;
    const ConfigurationTransitionStep* objective = nullptr;
    const ConfigurationTransitionStep* condenser = nullptr;
    const ConfigurationTransitionStep* cameraMode = nullptr;
    const ConfigurationTransitionStep* illumination = nullptr;
    const ConfigurationTransitionStep* apertureTarget = nullptr;
    const ConfigurationTransitionStep* intensityTarget = nullptr;
    const ConfigurationTransitionStep* exposurePolicy = nullptr;
    const ConfigurationTransitionStep* exposurePreset = nullptr;
    const ConfigurationTransitionStep* verification = nullptr;

    for (const auto& step : plan.steps()) {
        const auto& requirement = step.requirement();
        if (std::holds_alternative<AcquisitionMustBeInactive>(requirement)) {
            gate = &step;
        } else if (std::holds_alternative<OpticalPathSetMustMatch>(requirement)) {
            paths = &step;
        } else if (std::holds_alternative<ObjectiveSelectionMustMatch>(requirement)) {
            objective = &step;
        } else if (std::holds_alternative<CondenserSelectionMustMatch>(requirement)) {
            condenser = &step;
        } else if (std::holds_alternative<CameraOutputModeMustMatch>(requirement)) {
            cameraMode = &step;
        } else if (std::holds_alternative<IlluminationChannelMustMatch>(requirement)) {
            illumination = &step;
        } else if (std::holds_alternative<CondenserApertureMustMatch>(requirement)) {
            apertureTarget = &step;
        } else if (std::holds_alternative<IlluminationIntensityMustMatch>(requirement)) {
            intensityTarget = &step;
        } else if (std::holds_alternative<ExposurePolicyMustBeHonored>(requirement)) {
            exposurePolicy = &step;
        } else if (std::holds_alternative<InitialExposurePresetMustBeConsidered>(requirement)) {
            exposurePreset = &step;
        } else if (std::holds_alternative<DesiredConfigurationMustBeVerified>(requirement)) {
            verification = &step;
        }
    }

    check(gate && paths && objective && condenser && cameraMode &&
          illumination && apertureTarget && intensityTarget &&
          exposurePolicy && exposurePreset && verification,
          "contains acquisition, selections, policy, profile targets and final verification");

    if (gate && paths && objective && condenser && cameraMode &&
        illumination && apertureTarget && intensityTarget &&
        exposurePolicy && exposurePreset && verification) {
        const auto hasDependency = [&plan](
            const ConfigurationTransitionStep& prerequisite,
            const ConfigurationTransitionStep& dependent) {
            return std::any_of(
                plan.dependencies().begin(),
                plan.dependencies().end(),
                [&prerequisite, &dependent](
                    const ConfigurationTransitionDependency& dependency) {
                    return dependency.prerequisiteStepId() == prerequisite.id() &&
                           dependency.dependentStepId() == dependent.id();
                });
        };

        check(hasDependency(*gate, *paths) &&
              hasDependency(*gate, *objective) &&
              hasDependency(*gate, *condenser),
              "gates configuration requirements on acquisition inactivity");
        check(hasDependency(*paths, *cameraMode) &&
              hasDependency(*objective, *cameraMode),
              "orders camera mode after path and objective requirements");
        check(hasDependency(*condenser, *apertureTarget),
              "orders condenser aperture target after condenser selection");
        check(hasDependency(*illumination, *intensityTarget),
              "orders illumination intensity after channel selection");
        check(hasDependency(*cameraMode, *exposurePolicy) &&
              hasDependency(*exposurePolicy, *exposurePreset),
              "preserves exposure policy as a prerequisite to considering the initial preset");
        check(hasDependency(*apertureTarget, *verification) &&
              hasDependency(*intensityTarget, *verification) &&
              hasDependency(*exposurePreset, *verification),
              "requires final verification after all optional target requirements");
    }

    check(!plan.executionLayers().empty() &&
          plan.executionLayers().front().size() == 1 &&
          gate && plan.executionLayers().front().front() == gate->id(),
          "places acquisition inactivity in the first execution layer");
    check(plan.executionLayers().size() >= 3 &&
          paths && objective && condenser &&
          std::find(plan.executionLayers()[1].begin(),
                    plan.executionLayers()[1].end(), paths->id()) !=
              plan.executionLayers()[1].end() &&
          std::find(plan.executionLayers()[1].begin(),
                    plan.executionLayers()[1].end(), objective->id()) !=
              plan.executionLayers()[1].end() &&
          std::find(plan.executionLayers()[1].begin(),
                    plan.executionLayers()[1].end(), condenser->id()) !=
              plan.executionLayers()[1].end(),
          "keeps independent path/objective/condenser selections in one parallel layer");
    check(!plan.executionLayers().empty() &&
          plan.executionLayers().back().size() == 1 &&
          verification &&
          plan.executionLayers().back().front() == verification->id(),
          "places final desired-configuration verification in the last execution layer");

    const auto* policyRequirement = exposurePolicy
        ? std::get_if<ExposurePolicyMustBeHonored>(&exposurePolicy->requirement())
        : nullptr;
    const auto* presetRequirement = exposurePreset
        ? std::get_if<InitialExposurePresetMustBeConsidered>(&exposurePreset->requirement())
        : nullptr;
    check(policyRequirement != nullptr &&
          policyRequirement->policy == ExposurePolicy::Manual &&
          presetRequirement != nullptr,
          "retains Manual policy separately from the optional initial preset");

    const auto noTargetsResolution = ConfigurationResolver::resolve(
        fixture.equipment,
        fixture.profiles,
        makeRequest(fixture, {fixture.fullPathId}, fixture.cameraModeId));
    check(noTargetsResolution.isResolved(),
          "precondition: no-target profile resolves");
    if (noTargetsResolution.resolvedConfiguration()) {
        const auto noTargetsPlan = take(ConfigurationTransitionPlanner::plan(
            *noTargetsResolution.resolvedConfiguration()));
        check(std::none_of(
                  noTargetsPlan.steps().begin(),
                  noTargetsPlan.steps().end(),
                  [](const ConfigurationTransitionStep& step) {
                      return std::holds_alternative<CondenserApertureMustMatch>(
                                 step.requirement()) ||
                             std::holds_alternative<IlluminationIntensityMustMatch>(
                                 step.requirement()) ||
                             std::holds_alternative<InitialExposurePresetMustBeConsidered>(
                                 step.requirement());
                  }),
              "does not invent optional target requirements when the profile has no targets");
    }
}

void testTransitionPlannerIncludesAdditionalOpticalSelections() {
    const auto fixture = makeConfigurationFixture();
    const auto compatibilityId =
        makeId<OpticalCompatibilityId>("dic-compatibility-1");
    const auto profile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-dic-transition"),
        ContrastMethod::DifferentialInterferenceContrast,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId, fixture.illuminationPathId, fixture.imagingPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {},
        {take(OpticalFunctionRequirement::create(
             OpticalElementFunction::Polarizer, compatibilityId)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::Analyzer, compatibilityId)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::DifferentialInterferencePrism,
             compatibilityId,
             2))}));
    const auto catalog = take(ConfigurationProfileCatalog::create({profile}));
    const std::vector<EquipmentComponentId> additionalComponents{
        makeId<EquipmentComponentId>("polarizer"),
        makeId<EquipmentComponentId>("dic-prism-one"),
        makeId<EquipmentComponentId>("dic-prism-two"),
        makeId<EquipmentComponentId>("analyzer")};
    const auto request = makeRequest(
        fixture,
        {fixture.illuminationPathId, fixture.imagingPathId},
        fixture.cameraModeId,
        ContrastMethod::DifferentialInterferenceContrast,
        profile.id(),
        additionalComponents);
    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, catalog, request);
    check(resolution.isResolved(),
          "precondition: valid DIC selection resolves before transition planning");
    if (!resolution.resolvedConfiguration()) {
        return;
    }

    const auto plan = take(ConfigurationTransitionPlanner::plan(
        *resolution.resolvedConfiguration()));
    const ConfigurationTransitionStep* cameraMode = nullptr;
    const ConfigurationTransitionStep* illuminationChannel = nullptr;
    std::vector<const ConfigurationTransitionStep*> additionalSteps;

    for (const auto& step : plan.steps()) {
        if (std::holds_alternative<CameraOutputModeMustMatch>(step.requirement())) {
            cameraMode = &step;
        } else if (std::holds_alternative<IlluminationChannelMustMatch>(
                       step.requirement())) {
            illuminationChannel = &step;
        } else if (const auto* additional =
                       std::get_if<AdditionalOpticalComponentSelectionMustMatch>(
                           &step.requirement())) {
            additionalSteps.push_back(&step);
            check(std::find(
                      additionalComponents.begin(),
                      additionalComponents.end(),
                      additional->componentId) != additionalComponents.end(),
                  "additional optical transition node retains its selected component identity");
        }
    }

    check(additionalSteps.size() == additionalComponents.size(),
          "emits one typed transition requirement for every selected additional optical component");

    if (cameraMode && illuminationChannel) {
        for (const auto* additional : additionalSteps) {
            const bool precedesCamera = std::any_of(
                plan.dependencies().begin(),
                plan.dependencies().end(),
                [additional, cameraMode](
                    const ConfigurationTransitionDependency& edge) {
                    return edge.prerequisiteStepId() == additional->id() &&
                           edge.dependentStepId() == cameraMode->id();
                });
            const bool precedesIllumination = std::any_of(
                plan.dependencies().begin(),
                plan.dependencies().end(),
                [additional, illuminationChannel](
                    const ConfigurationTransitionDependency& edge) {
                    return edge.prerequisiteStepId() == additional->id() &&
                           edge.dependentStepId() == illuminationChannel->id();
                });
            check(precedesCamera && precedesIllumination,
                  "orders downstream camera and illumination requirements after every additional optical selection");
        }
    } else {
        check(false,
              "transition plan contains camera and illumination requirement nodes");
    }
}

void testTransitionPlanFactoriesRejectMalformedDependencyGraphs() {
    const auto fixture = makeConfigurationFixture();
    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment,
        fixture.profiles,
        makeRequest(fixture, {fixture.fullPathId}, fixture.cameraModeId));
    check(resolution.isResolved(),
          "precondition: malformed-plan tests start from a valid resolved configuration");
    if (!resolution.resolvedConfiguration()) {
        return;
    }

    const auto invalidPaths = ConfigurationTransitionStep::create(
        makeId<ConfigurationTransitionStepId>("invalid-empty-path-set"),
        OpticalPathSetMustMatch{});
    check(!invalidPaths &&
          invalidPaths.error() ==
              ConfigurationTransitionStepError::EmptyOpticalPathSet,
          "rejects a transition path requirement with no paths");

    const auto duplicatePaths = ConfigurationTransitionStep::create(
        makeId<ConfigurationTransitionStepId>("invalid-duplicate-path-set"),
        OpticalPathSetMustMatch{
            {fixture.fullPathId, fixture.fullPathId}});
    check(!duplicatePaths &&
          duplicatePaths.error() ==
              ConfigurationTransitionStepError::DuplicateOpticalPath,
          "rejects duplicate paths in one transition requirement");

    const auto gateId = makeId<ConfigurationTransitionStepId>("cycle-gate");
    const auto objectiveId =
        makeId<ConfigurationTransitionStepId>("cycle-objective");
    const auto condenserId =
        makeId<ConfigurationTransitionStepId>("cycle-condenser");
    const auto verifyId = makeId<ConfigurationTransitionStepId>("cycle-verify");

    const auto gate = take(ConfigurationTransitionStep::create(
        gateId, AcquisitionMustBeInactive{}));
    const auto objective = take(ConfigurationTransitionStep::create(
        objectiveId, ObjectiveSelectionMustMatch{fixture.objectiveId}));
    const auto condenser = take(ConfigurationTransitionStep::create(
        condenserId, CondenserSelectionMustMatch{fixture.condenserId}));
    const auto verify = take(ConfigurationTransitionStep::create(
        verifyId, DesiredConfigurationMustBeVerified{}));

    const auto dependency = [](
        const ConfigurationTransitionStepId& prerequisite,
        const ConfigurationTransitionStepId& dependent) {
        return take(ConfigurationTransitionDependency::create(
            prerequisite, dependent));
    };

    const auto cyclicPlan = ConfigurationTransitionPlan::create(
        *resolution.resolvedConfiguration(),
        {gate, objective, condenser, verify},
        {dependency(gateId, objectiveId),
         dependency(gateId, condenserId),
         dependency(gateId, verifyId),
         dependency(objectiveId, condenserId),
         dependency(condenserId, objectiveId),
         dependency(objectiveId, verifyId),
         dependency(condenserId, verifyId)});
    check(!cyclicPlan &&
          cyclicPlan.error() ==
              ConfigurationTransitionPlanError::CyclicDependencies,
          "rejects a transition plan containing a dependency cycle");

    const auto selfDependency =
        ConfigurationTransitionDependency::create(gateId, gateId);
    check(!selfDependency &&
          selfDependency.error() ==
              ConfigurationTransitionDependencyError::SelfDependency,
          "rejects a dependency from a step to itself");
}

void testResolverRejectsUnavailableCameraMode() {
    const auto fixture = makeConfigurationFixture();
    const auto request = makeRequest(
        fixture,
        {fixture.fullPathId},
        makeId<CameraModeId>("camera-unsupported-mode"));

    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, request);

    check(!resolution.isResolved(),
          "does not resolve a camera output mode absent from the selected camera");
    check(hasDiagnosticCode(
              resolution, "configuration.camera.mode_not_available"),
          "reports a stable diagnostic code for an unavailable camera mode");
}

void testResolverReportsProfileConflictAndMissingPath() {
    const auto fixture = makeConfigurationFixture();
    const auto mismatchRequest = makeRequest(
        fixture,
        {fixture.fullPathId},
        fixture.cameraModeId,
        ContrastMethod::PhaseContrast);

    const auto mismatch = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, mismatchRequest);
    check(!mismatch.isResolved() &&
          hasDiagnosticCode(mismatch, "configuration.profile.contrast_mismatch"),
          "rejects a contrast method inconsistent with the selected profile");

    const auto missingPath = makeRequest(
        fixture,
        {makeId<OpticalPathId>("path-not-installed")},
        fixture.cameraModeId);
    const auto missingPathResolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, missingPath);
    check(!missingPathResolution.isResolved() &&
          hasDiagnosticCode(
              missingPathResolution, "configuration.optical_path.not_installed"),
          "rejects a selected path that is absent from the installed equipment definition");

    const auto missingProfileRequest = take(ConfigurationRequest::create(
        makeId<ConfigurationProfileId>("profile-not-installed"),
        ContrastMethod::Brightfield,
        fixture.objectiveId,
        fixture.condenserId,
        {fixture.fullPathId},
        fixture.cameraId,
        fixture.cameraModeId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Manual));
    const auto missingProfileResolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, missingProfileRequest);
    check(!missingProfileResolution.isResolved() &&
          hasDiagnosticCode(missingProfileResolution, "configuration.profile.not_found"),
          "rejects a profile identity absent from the profile catalog");
}

void testResolverEnforcesPathCompatibilityAndIlluminationChannel() {
    const auto fixture = makeConfigurationFixture();

    const auto restrictedProfile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-illumination-path-only"),
        ContrastMethod::Brightfield,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.illuminationPathId},
        {fixture.cameraId},
        {fixture.illuminationId}));
    const auto restrictedCatalog =
        take(ConfigurationProfileCatalog::create({restrictedProfile}));
    const auto disallowedPathRequest = makeRequest(
        fixture,
        {fixture.fullPathId},
        fixture.cameraModeId,
        ContrastMethod::Brightfield,
        restrictedProfile.id());
    const auto disallowedPathResolution = ConfigurationResolver::resolve(
        fixture.equipment, restrictedCatalog, disallowedPathRequest);
    check(!disallowedPathResolution.isResolved() &&
          hasDiagnosticCode(
              disallowedPathResolution,
              "configuration.optical_path.not_compatible"),
          "rejects an installed path that is not allowed by the selected profile");

    const auto missingChannelRequest = take(ConfigurationRequest::create(
        fixture.brightfieldProfileId,
        ContrastMethod::Brightfield,
        fixture.objectiveId,
        fixture.condenserId,
        {fixture.fullPathId},
        fixture.cameraId,
        fixture.cameraModeId,
        fixture.illuminationId,
        makeId<IlluminationChannelId>("channel-not-installed"),
        ExposurePolicy::Manual));
    const auto missingChannelResolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, missingChannelRequest);
    check(!missingChannelResolution.isResolved() &&
          hasDiagnosticCode(
              missingChannelResolution,
              "configuration.illumination.channel_not_available"),
          "rejects an illumination channel absent from the selected source");

    const auto incompatibleObjectiveId =
        makeId<EquipmentComponentId>("objective-not-in-profile");
    const auto incompatibleObjectiveRequest = take(ConfigurationRequest::create(
        fixture.brightfieldProfileId,
        ContrastMethod::Brightfield,
        incompatibleObjectiveId,
        fixture.condenserId,
        {fixture.fullPathId},
        fixture.cameraId,
        fixture.cameraModeId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Manual));
    const auto incompatibleObjectiveResolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, incompatibleObjectiveRequest);
    check(!incompatibleObjectiveResolution.isResolved() &&
          hasDiagnosticCode(
              incompatibleObjectiveResolution,
              "configuration.objective.not_compatible"),
          "rejects an installed or requested objective not allowed by the profile");
}

void testCameraProfileMismatchWarnsButDoesNotBlockManualSelection() {
    const auto fixture = makeConfigurationFixture();
    const auto otherPreferredCamera =
        makeId<EquipmentComponentId>("camera-preferred-by-profile");
    const auto profile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-other-camera-preferred"),
        ContrastMethod::Brightfield,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId},
        {otherPreferredCamera},
        {fixture.illuminationId}));
    const auto catalog = take(ConfigurationProfileCatalog::create({profile}));
    const auto request = makeRequest(
        fixture,
        {fixture.fullPathId},
        fixture.cameraModeId,
        ContrastMethod::Brightfield,
        profile.id());

    const auto resolution =
        ConfigurationResolver::resolve(fixture.equipment, catalog, request);
    check(resolution.isResolved(),
          "does not reject a physically connected manually selected camera outside the profile preference set");
    check(hasDiagnosticCode(
              resolution,
              "configuration.camera.not_preferred_by_profile"),
          "returns a warning when the selected camera is not the profile's preferred camera");

    bool warningSeverity = false;
    for (const auto& diagnostic : resolution.diagnostics()) {
        if (diagnostic.code() == "configuration.camera.not_preferred_by_profile") {
            warningSeverity =
                diagnostic.severity() == DiagnosticSeverity::Warning;
        }
    }
    check(warningSeverity,
          "camera preference mismatch is explicitly warning severity, not error severity");
}

void testResolverRejectsCameraNotConnectedToSelectedImagingPath() {
    const auto fixture = makeConfigurationFixture();
    const auto request = makeRequest(
        fixture,
        {fixture.illuminationPathId},
        fixture.cameraModeId);

    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment,
        fixture.profiles,
        request);

    check(!resolution.isResolved() &&
          hasDiagnosticCode(
              resolution,
              "configuration.camera.not_connected_to_selected_imaging_path"),
          "rejects a camera that is not the terminal endpoint of a selected Imaging route");
}

void testLightPathCameraDiagnosticsSurviveConfigurationResolution() {
    const auto fixture = makeConfigurationFixture();
    check(fixture.equipment.revision() == 1,
          "existing equipment fixtures receive explicit revision one by default");
    const auto zeroRevisionEquipment = MicroscopeEquipmentDefinition::create(
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
        {},
        fixture.equipment.filterSpecifications(),
        0);
    check(!zeroRevisionEquipment &&
          zeroRevisionEquipment.error() == EquipmentDefinitionError::ZeroRevision,
          "rejects an equipment constitution with revision zero");

    const auto lightPathId =
        makeId<LightPathId>("configuration-brightfield-light-path");
    const auto lightPath = take(LightPath::create(
        lightPathId,
        "Brightfield",
        {fixture.illuminationPathId, fixture.imagingPathId}));

    const auto equipment = take(MicroscopeEquipmentDefinition::create(
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
        {lightPath},
        fixture.equipment.filterSpecifications()));

    const auto operationModeId =
        makeId<OperationModeId>("configuration-brightfield-mode");
    const auto modeSetting = take(LightPathModeSetting::create(
        fixture.microscopeId,
        1,
        operationModeId,
        1,
        lightPathId,
        makeId<CalibrationRevisionId>("configuration-brightfield-calibration"),
        fixture.illuminationId,
        fixture.illuminationChannelId,
        {fixture.objectiveId, fixture.condenserId},
        12.0));
    const auto operationMode = take(OperationMode::create(
        fixture.microscopeId,
        operationModeId,
        1,
        "Fluorescence",
        ContrastMethod::Fluorescence,
        {modeSetting}));

    FluorescenceCameraRequirements fluorescenceRequirements{
        take(SpectralBand::create(
            take(PositiveNanometers::create(400.0)),
            take(PositiveNanometers::create(700.0)))),
        0.75,
        2.0,
        0.1,
        20000.0,
        30.0,
        true,
        true};

    const auto cameraResolution = LightPathCameraResolver::resolve(
        equipment,
        operationMode,
        lightPathId,
        fixture.cameraId,
        fixture.cameraModeId,
        fluorescenceRequirements);
    check(cameraResolution.isResolved(),
          "resolves the single camera endpoint on the selected LightPath");

    const auto newerEquipmentRevision = take(MicroscopeEquipmentDefinition::create(
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
        {lightPath},
        fixture.equipment.filterSpecifications(),
        2));
    const auto staleModeCameraResolution = LightPathCameraResolver::resolve(
        newerEquipmentRevision,
        operationMode,
        lightPathId,
        fixture.cameraId,
        fixture.cameraModeId,
        fluorescenceRequirements);
    check(staleModeCameraResolution.status() ==
              LightPathCameraResolutionStatus::Unsupported,
          "rejects a mode calibrated against an earlier equipment-definition revision");
    check(std::any_of(
              staleModeCameraResolution.diagnostics().begin(),
              staleModeCameraResolution.diagnostics().end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "light_path_camera.equipment_revision_mismatch";
              }),
          "reports the equipment revision mismatch explicitly");

    const auto fluorescenceProfileId =
        makeId<ConfigurationProfileId>("profile-lightpath-fluorescence");
    const auto fluorescenceProfile = take(ConfigurationProfile::create(
        fluorescenceProfileId,
        ContrastMethod::Fluorescence,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.illuminationPathId, fixture.imagingPathId, fixture.fullPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {fixture.objectiveId}));
    const auto fluorescenceProfiles = take(
        ConfigurationProfileCatalog::create({fluorescenceProfile}));

    const auto request = LightPathConfigurationRequestFactory::create(
        equipment,
        operationMode,
        lightPathId,
        cameraResolution,
        fluorescenceProfileId,
        fixture.objectiveId,
        fixture.condenserId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Manual);
    check(static_cast<bool>(request),
          "creates a LightPath-bound ConfigurationRequest");

    if (!request) {
        return;
    }
    check(request.value().lightPathBinding().has_value(),
          "generated request retains its resolved LightPath binding");
    check(!request.value().cameraSelectionDiagnostics().empty(),
          "generated request retains the camera assessor's warning diagnostics");

    const auto resolution =
        ConfigurationResolver::resolve(equipment, fluorescenceProfiles, request.value());
    check(resolution.isResolved(),
          "the existing profile resolver accepts a coherent LightPath-derived request");

    const auto staleEquipmentResolution =
        ConfigurationResolver::resolve(
            newerEquipmentRevision, fluorescenceProfiles, request.value());
    check(!staleEquipmentResolution.isResolved() &&
          hasDiagnosticCode(
              staleEquipmentResolution,
              "configuration.light_path.binding_invalid"),
          "rechecks the bound equipment revision after the request has been created");

    check(hasDiagnosticCode(
              resolution,
              "camera.fluorescence.sensor_profile_unknown"),
          "camera suitability warnings are retained in ConfigurationResolution");
    if (resolution.resolvedConfiguration()) {
        const auto& resolvedRequest =
            resolution.resolvedConfiguration()->request();
        check(resolvedRequest.lightPathBinding().has_value(),
              "resolved configuration retains its LightPath binding");
        if (resolvedRequest.lightPathBinding()) {
            const auto& binding = *resolvedRequest.lightPathBinding();
            check(binding.operationModeId == operationMode.id() &&
                  binding.operationModeRevision == operationMode.revision() &&
                  binding.lightPathId == lightPathId,
                  "resolved configuration retains the exact mode and LightPath identity");
            check(binding.cameraComponentId == fixture.cameraId &&
                  binding.cameraModeId == fixture.cameraModeId,
                  "resolved configuration retains the derived camera and selected output mode");
        }
    }
}

void testSampleTypeRequirementsAssessResolvedConfigurationAndPreserveCameraWarnings() {
    const auto fixture = makeConfigurationFixture();
    const auto objectiveRequirements = take(
        ObjectiveMicroscopyRequirements::create(
            10.0, 40.0, 0.25, {ObjectiveImmersionMedium::Air}));
    const auto cameraRequirements = take(
        CameraPerformanceRequirements::create(
            30.0, 1.0, std::nullopt, 15000.0));
    const auto microscopyRequirements = take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield,
          MicroscopyTechniqueConstraintKind::Required},
         {ContrastMethod::PhaseContrast,
          MicroscopyTechniqueConstraintKind::Preferred}},
        objectiveRequirements,
        cameraRequirements));
    const auto sampleType = take(SampleType::create(
        makeId<SampleTypeId>("sample-type-blood-film-test"),
        3,
        "Peripheral blood film",
        SampleMaterialClass::Biological,
        microscopyRequirements));
    const auto geometry = take(SlideGeometry::create(75000.0, 25000.0));
    const auto preparation = take(SlidePreparation::create(
        SpecimenPreparationMethod::Smear,
        SpecimenFixationState::Fixed,
        std::nullopt,
        170.0,
        "Synthetic mounting medium"));
    const auto slide = take(Slide::create(
        makeId<SlideId>("slide-blood-film-001"),
        1,
        sampleType.id(),
        sampleType.revision(),
        "Fixed stained smear",
        geometry,
        preparation));

    const auto request = makeRequest(
        fixture,
        {fixture.fullPathId},
        fixture.cameraModeId,
        ContrastMethod::Brightfield);
    const auto configuration = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, request);
    check(configuration.isResolved(),
          "builds a valid equipment configuration before Slide requirement assessment");
    if (!configuration.resolvedConfiguration()) {
        return;
    }

    const auto assessment = SampleMicroscopyRequirementAssessor::assess(
        fixture.equipment,
        sampleType,
        slide,
        *configuration.resolvedConfiguration());
    check(assessment.isCompatible(),
          "accepts a resolved Brightfield configuration satisfying SampleType technique and objective requirements");
    check(assessment.hasWarnings(),
          "reports unknown camera performance data without blocking the manually selected operable camera");
    check(hasDiagnosticCode(configuration, "configuration.profile.not_found") == false,
          "keeps SampleType requirements independent of the microscope equipment-specific profile catalog");

    const auto strictObjectiveRequirements = take(
        ObjectiveMicroscopyRequirements::create(
            10.0, 40.0, 0.90, {ObjectiveImmersionMedium::Air}));
    const auto strictRequirements = take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield,
          MicroscopyTechniqueConstraintKind::Required}},
        strictObjectiveRequirements));
    const auto strictSampleType = take(SampleType::create(
        makeId<SampleTypeId>("sample-type-blood-film-high-na"),
        1,
        "High-NA fluorescence cell detail",
        SampleMaterialClass::Biological,
        strictRequirements));
    const auto strictSlide = take(Slide::create(
        makeId<SlideId>("slide-blood-film-high-na"),
        1,
        strictSampleType.id(),
        strictSampleType.revision(),
        "High-NA preparation",
        geometry,
        preparation));
    const auto strictAssessment = SampleMicroscopyRequirementAssessor::assess(
        fixture.equipment,
        strictSampleType,
        strictSlide,
        *configuration.resolvedConfiguration());
    check(!strictAssessment.isCompatible() &&
          std::any_of(
              strictAssessment.diagnostics().begin(),
              strictAssessment.diagnostics().end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "sample.microscopy.objective_na_below_minimum";
              }),
          "rejects an objective that fails a hard SampleType numerical-aperture requirement");

    const auto staleRevisionSlide = take(Slide::create(
        makeId<SlideId>("slide-blood-film-stale-type"),
        1,
        sampleType.id(),
        sampleType.revision() - 1,
        "Slide pinned to an older SampleType revision",
        geometry,
        preparation));
    const auto staleRevisionAssessment = SampleMicroscopyRequirementAssessor::assess(
        fixture.equipment,
        sampleType,
        staleRevisionSlide,
        *configuration.resolvedConfiguration());
    check(!staleRevisionAssessment.isCompatible() &&
          std::any_of(
              staleRevisionAssessment.diagnostics().begin(),
              staleRevisionAssessment.diagnostics().end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "sample.microscopy.slide_sample_type_revision_mismatch";
              }),
          "rejects assessment when Slide pins a different SampleType revision");
}

void testSampleRequirementsUseModeBoundSlidePlaneIrradiance() {
    const auto fixture = makeConfigurationFixture();
    const auto lightPathId =
        makeId<LightPathId>("sample-assessment-brightfield-light-path");
    const auto lightPath = take(LightPath::create(
        lightPathId,
        "Transmitted brightfield",
        {fixture.fullPathId}));

    const auto equipment = take(MicroscopeEquipmentDefinition::create(
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
        {lightPath},
        fixture.equipment.filterSpecifications(),
        fixture.equipment.revision()));

    const auto operationModeId =
        makeId<OperationModeId>("sample-assessment-brightfield-mode");
    const auto calibrationId =
        makeId<CalibrationRevisionId>("sample-assessment-brightfield-cal-r1");
    const auto modeSetting = take(LightPathModeSetting::create(
        fixture.microscopeId,
        equipment.revision(),
        operationModeId,
        1,
        lightPathId,
        calibrationId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        {fixture.objectiveId, fixture.condenserId},
        100.0));
    const auto operationMode = take(OperationMode::create(
        fixture.microscopeId,
        operationModeId,
        1,
        "Blood film brightfield 40x",
        ContrastMethod::Brightfield,
        {modeSetting}));

    const auto cameraResolution = LightPathCameraResolver::resolve(
        equipment,
        operationMode,
        lightPathId,
        fixture.cameraId,
        fixture.cameraModeId);
    check(cameraResolution.isResolved(),
          "resolves the camera through the mode's selected LightPath");
    if (!cameraResolution.isResolved()) {
        return;
    }

    const auto request = take(LightPathConfigurationRequestFactory::create(
        equipment,
        operationMode,
        lightPathId,
        cameraResolution,
        fixture.brightfieldProfileId,
        fixture.objectiveId,
        fixture.condenserId,
        fixture.illuminationId,
        fixture.illuminationChannelId,
        ExposurePolicy::Manual));
    const auto configuration = ConfigurationResolver::resolve(
        equipment, fixture.profiles, request);
    check(configuration.isResolved(),
          "resolves the equipment configuration carrying the exact mode/LightPath binding");
    if (!configuration.resolvedConfiguration()) {
        return;
    }

    const auto condenserRequirements =
        take(CondenserMicroscopyRequirements::create(
            0.70, 0.90, {CondenserDesign::Abbe}, {CondenserImmersionMedium::Air}));
    const auto illuminationRequirements =
        take(IlluminationMicroscopyRequirements::create(80.0, 120.0));
    const auto compatibleRequirements = take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield,
          MicroscopyTechniqueConstraintKind::Required}},
        std::nullopt,
        std::nullopt,
        false,
        condenserRequirements,
        illuminationRequirements));
    const auto preparationRequirements =
        take(SlidePreparationRequirements::create(
            {SpecimenPreparationMethod::Smear},
            {SpecimenFixationState::Fixed},
            std::nullopt,
            std::nullopt,
            150.0,
            190.0));
    const auto sampleType = take(SampleType::create(
        makeId<SampleTypeId>("sample-type-calibrated-blood-film"),
        1,
        "Peripheral blood film",
        SampleMaterialClass::Biological,
        compatibleRequirements,
        std::nullopt,
        preparationRequirements));
    const auto geometry = take(SlideGeometry::create(75000.0, 25000.0));
    const auto preparation = take(SlidePreparation::create(
        SpecimenPreparationMethod::Smear,
        SpecimenFixationState::Fixed,
        std::nullopt,
        170.0));
    const auto slide = take(Slide::create(
        makeId<SlideId>("slide-calibrated-blood-film"),
        1,
        sampleType.id(),
        sampleType.revision(),
        "BLOOD-CAL-001",
        "Stained smear on glass",
        geometry,
        preparation));

    const auto compatible = SampleMicroscopyRequirementAssessor::assess(
        equipment,
        sampleType,
        slide,
        *configuration.resolvedConfiguration(),
        &operationMode);
    check(compatible.isCompatible(),
          "accepts condenser capability and the mode's 100 µW/mm² specimen-plane target inside the SampleType range");

    const auto strictIlluminationRequirements =
        take(IlluminationMicroscopyRequirements::create(120.0, 180.0));
    const auto strictRequirements = take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield,
          MicroscopyTechniqueConstraintKind::Required}},
        std::nullopt,
        std::nullopt,
        false,
        condenserRequirements,
        strictIlluminationRequirements));
    const auto strictSampleType = take(SampleType::create(
        makeId<SampleTypeId>("sample-type-high-irradiance"),
        1,
        "High irradiance specimen",
        SampleMaterialClass::Biological,
        strictRequirements));
    const auto strictSlide = take(Slide::create(
        makeId<SlideId>("slide-high-irradiance"),
        1,
        strictSampleType.id(),
        strictSampleType.revision(),
        "IRR-001",
        "High irradiance preparation",
        geometry,
        preparation));
    const auto incompatible = SampleMicroscopyRequirementAssessor::assess(
        equipment,
        strictSampleType,
        strictSlide,
        *configuration.resolvedConfiguration(),
        &operationMode);
    check(!incompatible.isCompatible() &&
          std::any_of(
              incompatible.diagnostics().begin(),
              incompatible.diagnostics().end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "sample.microscopy.specimen_irradiance_below_minimum";
              }),
          "rejects a mode whose calibrated specimen-plane irradiance falls below the SampleType requirement");

    const auto incompatiblePreparationRequirements =
        take(SlidePreparationRequirements::create(
            {SpecimenPreparationMethod::TissueSection},
            {SpecimenFixationState::Fixed}));
    const auto incompatiblePreparationType = take(SampleType::create(
        makeId<SampleTypeId>("sample-type-tissue-section-only"),
        1,
        "Thin tissue section",
        SampleMaterialClass::Biological,
        compatibleRequirements,
        std::nullopt,
        incompatiblePreparationRequirements));
    const auto incompatiblePreparationSlide = take(Slide::create(
        makeId<SlideId>("slide-type-tissue-section-only"),
        1,
        incompatiblePreparationType.id(),
        incompatiblePreparationType.revision(),
        "SECTION-001",
        "Smear used as section",
        geometry,
        preparation));
    const auto preparationMismatch =
        SampleMicroscopyRequirementAssessor::assess(
            equipment,
            incompatiblePreparationType,
            incompatiblePreparationSlide,
            *configuration.resolvedConfiguration(),
            &operationMode);
    check(!preparationMismatch.isCompatible() &&
          std::any_of(
              preparationMismatch.diagnostics().begin(),
              preparationMismatch.diagnostics().end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "sample.preparation.method_not_allowed";
              }),
          "rejects an actual Slide preparation method outside its SampleType envelope");

    const auto missingMode = SampleMicroscopyRequirementAssessor::assess(
        equipment,
        sampleType,
        slide,
        *configuration.resolvedConfiguration());
    check(!missingMode.isCompatible() &&
          std::any_of(
              missingMode.diagnostics().begin(),
              missingMode.diagnostics().end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "sample.microscopy.operation_mode_missing";
              }),
          "does not infer calibrated specimen-plane irradiance when authoritative OperationMode is unavailable");
}

void testProfileRequiredComponentMustBeOnSelectedPaths() {
    const auto fixture = makeConfigurationFixture();
    const auto missingRequiredComponent =
        makeId<EquipmentComponentId>("phase-selector-not-on-path");

    const auto profile = take(ConfigurationProfile::create(
        makeId<ConfigurationProfileId>("profile-requires-phase-selector"),
        ContrastMethod::Brightfield,
        {fixture.objectiveId},
        {fixture.condenserId},
        {fixture.fullPathId},
        {fixture.cameraId},
        {fixture.illuminationId},
        {missingRequiredComponent}));
    const auto catalog = take(ConfigurationProfileCatalog::create({profile}));
    const auto request = makeRequest(
        fixture,
        {fixture.fullPathId},
        fixture.cameraModeId,
        ContrastMethod::Brightfield,
        profile.id());

    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, catalog, request);

    check(!resolution.isResolved() &&
          hasDiagnosticCode(
              resolution,
              "configuration.profile.required_component_missing"),
          "rejects a configuration whose selected paths omit a required profile component");
}

} // namespace

int main() {
    testConfigurationFactoriesRejectInvalidSets();
    testResolverAcceptsOneCompleteBrightfieldPath();
    testResolverAcceptsSeparateIlluminationAndImagingPaths();
    testPhaseContrastRequiresMatchedPhasePlateAndAnnulus();
    testAdditionalOpticalComponentMustSatisfyProfileRequirement();
    testDicRequiresPolarizerAnalyzerAndTwoMatchedPrisms();
    testPolarizedLightProfileRequiresMatchedPolarizerAndAnalyzer();
    testOpticalFunctionRequirementFactoriesAndSelections();
    testProfileTargetsAreRetainedAndValidatedAgainstInstalledRanges();
    testTransitionPlannerProducesTypedRequirementsAndDependencyLayers();
    testTransitionPlannerIncludesAdditionalOpticalSelections();
    testTransitionPlanFactoriesRejectMalformedDependencyGraphs();
    testResolverRejectsUnavailableCameraMode();
    testResolverReportsProfileConflictAndMissingPath();
    testResolverEnforcesPathCompatibilityAndIlluminationChannel();
    testCameraProfileMismatchWarnsButDoesNotBlockManualSelection();
    testResolverRejectsCameraNotConnectedToSelectedImagingPath();
    testLightPathCameraDiagnosticsSurviveConfigurationResolution();
    testSampleTypeRequirementsAssessResolvedConfigurationAndPreserveCameraWarnings();
    testSampleRequirementsUseModeBoundSlidePlaneIrradiance();
    testProfileRequiredComponentMustBeOnSelectedPaths();

    if (failures != 0) {
        std::cerr << failures << " configuration resolver contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All configuration resolver contract tests passed.\n";
    return 0;
}
