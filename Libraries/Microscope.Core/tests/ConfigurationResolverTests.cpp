#include "microscope/core/configuration/Configuration.h"

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

EquipmentComponent opticalElement(
    const char* componentId,
    const char* inputPortId,
    const char* outputPortId) {
    return take(EquipmentComponent::create(
        makeId<EquipmentComponentId>(componentId),
        EquipmentComponentKind::OpticalElement,
        {port(inputPortId, OpticalPortDirection::Input),
         port(outputPortId, OpticalPortDirection::Output)}));
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
    components.push_back(opticalElement(
        "condenser-abbe", "condenser-input", "condenser-output"));
    components.push_back(opticalElement(
        "objective-10x", "objective-input", "objective-output"));
    components.push_back(take(EquipmentComponent::create(
        cameraId,
        EquipmentComponentKind::ImagingDevice,
        {take(OpticalPort::create(
            cameraInput, OpticalPortDirection::Input))})));

    std::vector<OpticalConnection> connections{
        connection("connection-source-condenser",
                   illuminationOutput, condenserInput),
        connection("connection-condenser-objective",
                   condenserOutput, objectiveInput),
        connection("connection-objective-camera",
                   objectiveOutput, cameraInput)};

    std::vector<OpticalPath> paths;
    paths.push_back(take(OpticalPath::create(
        fullPathId,
        {pathStep(illuminationId, std::nullopt, illuminationOutput),
         pathStep(condenserId, condenserInput, condenserOutput),
         pathStep(objectiveId, objectiveInput, objectiveOutput),
         pathStep(cameraId, cameraInput, std::nullopt)})));

    paths.push_back(take(OpticalPath::create(
        illuminationPathId,
        {pathStep(illuminationId, std::nullopt, illuminationOutput),
         pathStep(condenserId, condenserInput, condenserOutput),
         pathStep(objectiveId, objectiveInput, std::nullopt)})));

    paths.push_back(take(OpticalPath::create(
        imagingPathId,
        {pathStep(objectiveId, std::nullopt, objectiveOutput),
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
        makeId<ConfigurationProfileId>("profile-brightfield")) {
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
        ExposurePolicy::Manual));
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
          "rejects an unknown exposure policy value");
}

void testResolverAcceptsOneCompleteBrightfieldPath() {
    const auto fixture = makeConfigurationFixture();
    const auto request = makeRequest(
        fixture, {fixture.fullPathId}, fixture.cameraModeId);

    const auto resolution = ConfigurationResolver::resolve(
        fixture.equipment, fixture.profiles, request);

    check(resolution.isResolved(),
          "resolves a compatible brightfield path with installed objective/camera/illumination specs");
    check(resolution.resolvedRequest().has_value() &&
          resolution.resolvedRequest()->objectiveComponentId() == fixture.objectiveId &&
          resolution.resolvedRequest()->cameraModeId() == fixture.cameraModeId,
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
    testResolverRejectsUnavailableCameraMode();
    testResolverReportsProfileConflictAndMissingPath();
    testProfileRequiredComponentMustBeOnSelectedPaths();

    if (failures != 0) {
        std::cerr << failures << " configuration resolver contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All configuration resolver contract tests passed.\n";
    return 0;
}
