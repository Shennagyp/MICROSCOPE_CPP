#include "microscope/core/configuration/Configuration.h"

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
         pathStep(cameraId, cameraInput, std::nullopt)})));

    paths.push_back(take(OpticalPath::create(
        illuminationPathId,
        {pathStep(illuminationId, std::nullopt, illuminationOutput),
         pathStep(condenserId, condenserInput, condenserOutput),
         pathStep(makeId<EquipmentComponentId>("polarizer"),
                  polarizerInput, polarizerOutput),
         pathStep(makeId<EquipmentComponentId>("dic-prism-one"),
                  dicPrismOneInput, dicPrismOneOutput),
         pathStep(objectiveId, objectiveInput, std::nullopt)})));

    paths.push_back(take(OpticalPath::create(
        imagingPathId,
        {pathStep(objectiveId, std::nullopt, objectiveOutput),
         pathStep(makeId<EquipmentComponentId>("dic-prism-two"),
                  dicPrismTwoInput, dicPrismTwoOutput),
         pathStep(makeId<EquipmentComponentId>("analyzer"),
                  analyzerInput, analyzerOutput),
         pathStep(cameraId, cameraInput, std::nullopt)})));

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
            "Mono8"))}));

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
             OpticalElementFunction::PhasePlate, phaseCompatibilityId)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::PhaseAnnulus, phaseCompatibilityId))}));
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
        {fixture.fullPathId},
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

    const auto unrelatedOpticalComponentRequest = makeRequest(
        fixture,
        {fixture.fullPathId},
        fixture.cameraModeId,
        ContrastMethod::PhaseContrast,
        phaseProfile.id(),
        {makeId<EquipmentComponentId>("polarizer")});
    const unrelatedSelection = ConfigurationResolver::resolve(
        fixture.equipment, phaseCatalog, unrelatedOpticalComponentRequest);
    check(!unrelatedSelection.isResolved() &&
          hasDiagnosticCode(
              unrelatedSelection,
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
             OpticalElementFunction::Polarizer, dicCompatibilityId)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::Analyzer, dicCompatibilityId)),
         take(OpticalFunctionRequirement::create(
             OpticalElementFunction::DifferentialInterferencePrism,
             dicCompatibilityId,
             2)}}));
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
    testDicRequiresPolarizerAnalyzerAndTwoMatchedPrisms();
    testOpticalFunctionRequirementFactoriesAndSelections();
    testResolverRejectsUnavailableCameraMode();
    testResolverReportsProfileConflictAndMissingPath();
    testResolverEnforcesPathCompatibilityAndIlluminationChannel();
    testProfileRequiredComponentMustBeOnSelectedPaths();

    if (failures != 0) {
        std::cerr << failures << " configuration resolver contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All configuration resolver contract tests passed.\n";
    return 0;
}
