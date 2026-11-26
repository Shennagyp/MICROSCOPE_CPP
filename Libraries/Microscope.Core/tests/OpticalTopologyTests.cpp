#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/configuration/LightPathCameraResolver.h"
#include "microscope/core/configuration/LightPathConfigurationRequestFactory.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <type_traits>
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
        throw std::runtime_error("A test fixture factory rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

OpticalPort makePort(const char* value, OpticalPortDirection direction) {
    return take(OpticalPort::create(
        makeId<OpticalPortId>(value),
        direction));
}

OpticalPathStep makeStep(
    const EquipmentComponentId& componentId,
    std::optional<OpticalPortId> entry,
    std::optional<OpticalPortId> exit) {
    return take(OpticalPathStep::create(
        componentId,
        std::move(entry),
        std::move(exit)));
}

PixelDimensions dimensions(std::uint32_t width, std::uint32_t height) {
    return take(PixelDimensions::create(width, height));
}

PositiveMicrometers micrometers(double value) {
    return take(PositiveMicrometers::create(value));
}

CameraSpecification cameraSpecification(
    const EquipmentComponentId& componentId,
    const char* modeId) {
    const auto mode = take(CameraOutputMode::create(
        makeId<CameraModeId>(modeId),
        take(SensorRegionOfInterest::create(0, 0, 640, 480)),
        take(PixelBinning::create(1, 1)),
        dimensions(640, 480),
        CameraPixelLayout::Monochrome,
        "Mono8"));

    return take(CameraSpecification::create(
        componentId,
        dimensions(640, 480),
        micrometers(3.45),
        micrometers(3.45),
        {mode}));
}

OpticalConnection makeConnection(
    const char* id,
    const OpticalPortId& from,
    const OpticalPortId& to) {
    return take(OpticalConnection::create(
        makeId<OpticalConnectionId>(id),
        from,
        to));
}

struct EquipmentFixture final {
    MicroscopeId microscopeId;
    EquipmentComponentId sourceId;
    EquipmentComponentId condenserId;
    EquipmentComponentId splitterId;
    EquipmentComponentId objectiveAId;
    EquipmentComponentId cameraAId;
    EquipmentComponentId objectiveBId;
    EquipmentComponentId cameraBId;

    OpticalPortId sourceOutput;
    OpticalPortId condenserInput;
    OpticalPortId condenserOutput;
    OpticalPortId splitterInput;
    OpticalPortId splitterOutputA;
    OpticalPortId splitterOutputB;
    OpticalPortId objectiveAInput;
    OpticalPortId objectiveAOutput;
    OpticalPortId objectiveBInput;
    OpticalPortId objectiveBOutput;
    OpticalPortId cameraAInput;
    OpticalPortId cameraBInput;

    std::vector<EquipmentComponent> components;
    std::vector<OpticalConnection> connections;
    std::vector<OpticalPath> paths;
};

EquipmentFixture makeSharedSplitterFixture() {
    EquipmentFixture fixture{
        makeId<MicroscopeId>("microscope-optical-fixture"),
        makeId<EquipmentComponentId>("source-transmitted"),
        makeId<EquipmentComponentId>("condenser-shared"),
        makeId<EquipmentComponentId>("splitter-shared"),
        makeId<EquipmentComponentId>("objective-path-a"),
        makeId<EquipmentComponentId>("camera-path-a"),
        makeId<EquipmentComponentId>("objective-path-b"),
        makeId<EquipmentComponentId>("camera-path-b"),
        makeId<OpticalPortId>("source-output"),
        makeId<OpticalPortId>("condenser-input"),
        makeId<OpticalPortId>("condenser-output"),
        makeId<OpticalPortId>("splitter-input"),
        makeId<OpticalPortId>("splitter-output-a"),
        makeId<OpticalPortId>("splitter-output-b"),
        makeId<OpticalPortId>("objective-a-input"),
        makeId<OpticalPortId>("objective-a-output"),
        makeId<OpticalPortId>("objective-b-input"),
        makeId<OpticalPortId>("objective-b-output"),
        makeId<OpticalPortId>("camera-a-input"),
        makeId<OpticalPortId>("camera-b-input"),
        {},
        {},
        {}};

    fixture.components.push_back(take(EquipmentComponent::create(
        fixture.sourceId,
        EquipmentComponentKind::IlluminationDevice,
        {makePort("source-output", OpticalPortDirection::Output)})));

    fixture.components.push_back(take(EquipmentComponent::create(
        fixture.condenserId,
        EquipmentComponentKind::OpticalElement,
        {makePort("condenser-input", OpticalPortDirection::Input),
         makePort("condenser-output", OpticalPortDirection::Output)})));

    fixture.components.push_back(take(EquipmentComponent::create(
        fixture.splitterId,
        EquipmentComponentKind::OpticalElement,
        {makePort("splitter-input", OpticalPortDirection::Input),
         makePort("splitter-output-a", OpticalPortDirection::Output),
         makePort("splitter-output-b", OpticalPortDirection::Output)},
        {take(OpticalFunctionCapability::create(
            OpticalElementFunction::BeamSplitter))})));

    fixture.components.push_back(take(EquipmentComponent::create(
        fixture.objectiveAId,
        EquipmentComponentKind::OpticalElement,
        {makePort("objective-a-input", OpticalPortDirection::Input),
         makePort("objective-a-output", OpticalPortDirection::Output)})));

    fixture.components.push_back(take(EquipmentComponent::create(
        fixture.cameraAId,
        EquipmentComponentKind::ImagingDevice,
        {makePort("camera-a-input", OpticalPortDirection::Input)})));

    fixture.components.push_back(take(EquipmentComponent::create(
        fixture.objectiveBId,
        EquipmentComponentKind::OpticalElement,
        {makePort("objective-b-input", OpticalPortDirection::Input),
         makePort("objective-b-output", OpticalPortDirection::Output)})));

    fixture.components.push_back(take(EquipmentComponent::create(
        fixture.cameraBId,
        EquipmentComponentKind::ImagingDevice,
        {makePort("camera-b-input", OpticalPortDirection::Input)})));

    fixture.connections = {
        makeConnection("connection-source-condenser",
                       fixture.sourceOutput, fixture.condenserInput),
        makeConnection("connection-condenser-splitter",
                       fixture.condenserOutput, fixture.splitterInput),
        makeConnection("connection-splitter-objective-a",
                       fixture.splitterOutputA, fixture.objectiveAInput),
        makeConnection("connection-objective-a-camera-a",
                       fixture.objectiveAOutput, fixture.cameraAInput),
        makeConnection("connection-splitter-objective-b",
                       fixture.splitterOutputB, fixture.objectiveBInput),
        makeConnection("connection-objective-b-camera-b",
                       fixture.objectiveBOutput, fixture.cameraBInput)};

    fixture.paths.push_back(take(OpticalPath::create(
        makeId<OpticalPathId>("transmitted-path-a"),
        {makeStep(fixture.sourceId, std::nullopt, fixture.sourceOutput),
         makeStep(fixture.condenserId, fixture.condenserInput, fixture.condenserOutput),
         makeStep(fixture.splitterId, fixture.splitterInput, fixture.splitterOutputA),
         makeStep(fixture.objectiveAId, fixture.objectiveAInput, fixture.objectiveAOutput),
         makeStep(fixture.cameraAId, fixture.cameraAInput, std::nullopt)})));

    fixture.paths.push_back(take(OpticalPath::create(
        makeId<OpticalPathId>("transmitted-path-b"),
        {makeStep(fixture.sourceId, std::nullopt, fixture.sourceOutput),
         makeStep(fixture.condenserId, fixture.condenserInput, fixture.condenserOutput),
         makeStep(fixture.splitterId, fixture.splitterInput, fixture.splitterOutputB),
         makeStep(fixture.objectiveBId, fixture.objectiveBInput, fixture.objectiveBOutput),
         makeStep(fixture.cameraBId, fixture.cameraBInput, std::nullopt)})));

    return fixture;
}

void testLightPathDerivesOnlyConnectedCameraEndpoints() {
    auto fixture = makeSharedSplitterFixture();

    const auto cameraCId = makeId<EquipmentComponentId>("camera-disconnected");
    fixture.components.push_back(take(EquipmentComponent::create(
        cameraCId,
        EquipmentComponentKind::ImagingDevice,
        {makePort("camera-disconnected-input", OpticalPortDirection::Input)})));

    const auto imagingPathA = take(OpticalPath::create(
        fixture.paths[0].id(),
        fixture.paths[0].steps(),
        OpticalPathRole::Imaging));
    const auto imagingPathB = take(OpticalPath::create(
        fixture.paths[1].id(),
        fixture.paths[1].steps(),
        OpticalPathRole::Imaging));
    const auto lightPath = take(LightPath::create(
        makeId<LightPathId>("light-path-dual-camera"),
        "Dual camera splitter path",
        {imagingPathA.id(), imagingPathB.id()}));

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        {imagingPathA, imagingPathB},
        {},
        {cameraSpecification(fixture.cameraAId, "camera-a-mode"),
         cameraSpecification(fixture.cameraBId, "camera-b-mode"),
         cameraSpecification(cameraCId, "camera-disconnected-mode")},
        {},
        {},
        {},
        {},
        {lightPath});

    check(static_cast<bool>(definition),
          "accepts an imaging LightPath with two cameras connected through a declared beam splitter");
    if (!definition) {
        return;
    }

    const auto endpoints = definition.value().cameraEndpointIds(lightPath.id());
    check(endpoints.size() == 2,
          "derives two camera endpoints from the imaging route terminal steps");
    check(endpoints[0] == fixture.cameraAId &&
          endpoints[1] == fixture.cameraBId,
          "preserves the camera identities connected to the two splitter branches");
    check(endpoints[0] != cameraCId && endpoints[1] != cameraCId,
          "does not include an installed camera that is not connected to the selected LightPath");
    check(definition.value().cameraEndpointIds(
              makeId<LightPathId>("unknown-light-path")).empty(),
          "returns no camera endpoints for an unknown LightPath identity");
}

void testImagingRouteMustTerminateAtSpecifiedCamera() {
    auto fixture = makeSharedSplitterFixture();
    auto truncatedImagingPath = take(OpticalPath::create(
        makeId<OpticalPathId>("invalid-imaging-route"),
        {makeStep(fixture.sourceId, std::nullopt, fixture.sourceOutput),
         makeStep(fixture.condenserId, fixture.condenserInput, fixture.condenserOutput),
         makeStep(fixture.splitterId, fixture.splitterInput, fixture.splitterOutputA),
         makeStep(fixture.objectiveAId, fixture.objectiveAInput, std::nullopt)},
        OpticalPathRole::Imaging));

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        {truncatedImagingPath},
        {},
        {cameraSpecification(fixture.cameraAId, "camera-a-mode")});

    check(!definition &&
          definition.error() ==
              EquipmentDefinitionError::ImagingPathDoesNotTerminateAtCamera,
          "rejects an imaging route whose terminal component is an objective rather than a connected camera");
}

void testLightPathMustContainAnImagingRoute() {
    auto fixture = makeSharedSplitterFixture();
    const auto illuminationOnlyLightPath = take(LightPath::create(
        makeId<LightPathId>("illumination-only-light-path"),
        "Illumination-only route",
        {fixture.paths.front().id()}));

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths,
        {},
        {},
        {},
        {},
        {},
        {},
        {illuminationOnlyLightPath});

    check(!definition &&
          definition.error() ==
              EquipmentDefinitionError::LightPathHasNoImagingRoute,
          "requires a selectable acquisition LightPath to include an imaging route");
}

void testMultipleCameraLightPathRequiresPhysicalSplitter() {
    auto fixture = makeSharedSplitterFixture();
    const auto splitter = std::find_if(
        fixture.components.begin(),
        fixture.components.end(),
        [&fixture](const EquipmentComponent& component) {
            return component.id() == fixture.splitterId;
        });
    if (splitter == fixture.components.end()) {
        check(false, "test fixture contains its splitter component");
        return;
    }

    *splitter = take(EquipmentComponent::create(
        fixture.splitterId,
        EquipmentComponentKind::OpticalElement,
        {makePort("splitter-input", OpticalPortDirection::Input),
         makePort("splitter-output-a", OpticalPortDirection::Output),
         makePort("splitter-output-b", OpticalPortDirection::Output)}));

    const auto imagingPathA = take(OpticalPath::create(
        fixture.paths[0].id(),
        fixture.paths[0].steps(),
        OpticalPathRole::Imaging));
    const auto imagingPathB = take(OpticalPath::create(
        fixture.paths[1].id(),
        fixture.paths[1].steps(),
        OpticalPathRole::Imaging));
    const auto lightPath = take(LightPath::create(
        makeId<LightPathId>("light-path-without-splitter"),
        "Invalid dual camera path",
        {imagingPathA.id(), imagingPathB.id()}));

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        {imagingPathA, imagingPathB},
        {},
        {cameraSpecification(fixture.cameraAId, "camera-a-mode"),
         cameraSpecification(fixture.cameraBId, "camera-b-mode")},
        {},
        {},
        {},
        {},
        {lightPath});

    check(!definition &&
          definition.error() ==
              EquipmentDefinitionError::LightPathMultipleCamerasRequireSplitter,
          "rejects multiple camera endpoints unless imaging routes branch through an explicitly declared beam splitter");
}

void testLightPathFactoryRejectsDuplicateRoutes() {
    const auto routeId = makeId<OpticalPathId>("duplicate-light-path-route");
    const auto duplicateRoutes = LightPath::create(
        makeId<LightPathId>("duplicate-routes-light-path"),
        "Duplicate routes",
        {routeId, routeId});
    check(!duplicateRoutes &&
          duplicateRoutes.error() == LightPathError::DuplicateOpticalPath,
          "rejects duplicate route references inside one LightPath");

    const auto emptyRoutes = LightPath::create(
        makeId<LightPathId>("empty-routes-light-path"),
        "Empty routes",
        {});
    check(!emptyRoutes &&
          emptyRoutes.error() == LightPathError::EmptyOpticalPathSet,
          "rejects an empty route set");

    const auto emptyName = LightPath::create(
        makeId<LightPathId>("empty-name-light-path"),
        "",
        {routeId});
    check(!emptyName && emptyName.error() == LightPathError::EmptyName,
          "requires a non-empty selectable LightPath name");
}

void testInstalledFilterSpecificationsMatchOpticalFunctionsAndCompatibility() {
    auto fixture = makeSharedSplitterFixture();
    const auto filterId =
        makeId<EquipmentComponentId>("installed-excitation-filter");
    const auto compatibilityId =
        makeId<OpticalCompatibilityId>("fluorescence-cube-set-A");
    const auto filterComponent = take(EquipmentComponent::create(
        filterId,
        EquipmentComponentKind::OpticalElement,
        {makePort("installed-filter-input", OpticalPortDirection::Input),
         makePort("installed-filter-output", OpticalPortDirection::Output)},
        {take(OpticalFunctionCapability::create(
            OpticalElementFunction::ExcitationFilter, compatibilityId))}));
    fixture.components.push_back(filterComponent);

    const auto matchingFilter = take(OpticalFilterSpecification::create(
        filterId,
        OpticalFilterRole::Excitation,
        {{take(PositiveNanometers::create(400.0)), 0.1, std::nullopt},
         {take(PositiveNanometers::create(500.0)), 0.9, std::nullopt}},
        compatibilityId));

    auto valid = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths,
        {}, {}, {}, {}, {}, {}, {},
        {matchingFilter});
    check(static_cast<bool>(valid),
          "accepts an installed excitation filter whose role and compatibility match its optical function");

    const auto wrongCompatibility = take(OpticalFilterSpecification::create(
        filterId,
        OpticalFilterRole::Excitation,
        {{take(PositiveNanometers::create(400.0)), 0.1, std::nullopt},
         {take(PositiveNanometers::create(500.0)), 0.9, std::nullopt}},
        makeId<OpticalCompatibilityId>("fluorescence-cube-set-B")));
    auto mismatch = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths,
        {}, {}, {}, {}, {}, {}, {},
        {wrongCompatibility});
    check(!mismatch &&
          mismatch.error() == EquipmentDefinitionError::FilterCompatibilityMismatch,
          "rejects a filter profile whose compatibility identity differs from the installed optical capability");

    const auto wrongRole = take(OpticalFilterSpecification::create(
        filterId,
        OpticalFilterRole::Emission,
        {{take(PositiveNanometers::create(400.0)), 0.1, std::nullopt},
         {take(PositiveNanometers::create(500.0)), 0.9, std::nullopt}},
        compatibilityId));
    auto roleMismatch = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths,
        {}, {}, {}, {}, {}, {}, {},
        {wrongRole});
    check(!roleMismatch &&
          roleMismatch.error() == EquipmentDefinitionError::FilterOpticalFunctionMismatch,
          "rejects an emission-filter record attached to an excitation-filter component");
}

void testStrongEquipmentIdentities() {
    static_assert(!std::is_convertible<
        EquipmentComponentId, DeviceId>::value,
        "equipment component and device identities must remain distinct");
    static_assert(!std::is_convertible<
        OpticalPortId, OpticalConnectionId>::value,
        "optical port and connection identities must remain distinct");
    static_assert(!std::is_convertible<
        OpticalPathId, EquipmentComponentId>::value,
        "optical path and physical component identities must remain distinct");
    static_assert(!std::is_convertible<
        LightPathId, OpticalPathId>::value,
        "light path and physical optical route identities must remain distinct");
}

void testSharedComponentsAndBranching() {
    auto fixture = makeSharedSplitterFixture();
    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(static_cast<bool>(definition),
          "accepts two optical paths sharing one condenser and beam splitter");
    if (!definition) {
        return;
    }

    check(definition.value().components().size() == 7,
          "stores each physical component exactly once");
    check(definition.value().connections().size() == 6,
          "stores each undirected physical adjacency exactly once");
    check(definition.value().opticalPaths().size() == 2,
          "retains both camera output paths");
    check(definition.value().opticalPaths()[0].steps()[1].componentId() ==
              definition.value().opticalPaths()[1].steps()[1].componentId(),
          "both paths reference the same shared condenser identity");
    check(definition.value().opticalPaths()[0].steps()[2].componentId() ==
              definition.value().opticalPaths()[1].steps()[2].componentId(),
          "both paths reference the same shared splitter identity");
    check(definition.value().opticalPaths()[0].steps()[2].exitPortId() !=
              definition.value().opticalPaths()[1].steps()[2].exitPortId(),
          "branch selection uses distinct physical splitter output ports");
}

void testSharedPhysicalConnectionSupportsOppositePathTraversal() {
    const auto microscopeId =
        makeId<MicroscopeId>("microscope-reversible-optical-route");
    const auto firstComponentId =
        makeId<EquipmentComponentId>("reversible-optic-a");
    const auto secondComponentId =
        makeId<EquipmentComponentId>("reversible-optic-b");

    const auto firstInput = makePort(
        "reversible-a-input", OpticalPortDirection::Bidirectional);
    const auto firstOutput = makePort(
        "reversible-a-output", OpticalPortDirection::Bidirectional);
    const auto secondInput = makePort(
        "reversible-b-input", OpticalPortDirection::Bidirectional);
    const auto secondOutput = makePort(
        "reversible-b-output", OpticalPortDirection::Bidirectional);

    const auto firstComponent = take(EquipmentComponent::create(
        firstComponentId,
        EquipmentComponentKind::OpticalElement,
        {firstInput, firstOutput}));
    const auto secondComponent = take(EquipmentComponent::create(
        secondComponentId,
        EquipmentComponentKind::OpticalElement,
        {secondInput, secondOutput}));

    const auto sharedConnection = makeConnection(
        "reversible-shared-connection",
        firstOutput.id(),
        secondInput.id());

    const auto forwardPath = take(OpticalPath::create(
        makeId<OpticalPathId>("reversible-forward-path"),
        {makeStep(firstComponentId, std::nullopt, firstOutput.id()),
         makeStep(secondComponentId, secondInput.id(), std::nullopt)}));

    const auto reversePath = take(OpticalPath::create(
        makeId<OpticalPathId>("reversible-reverse-path"),
        {makeStep(secondComponentId, std::nullopt, secondInput.id()),
         makeStep(firstComponentId, firstOutput.id(), std::nullopt)}));

    auto definition = MicroscopeEquipmentDefinition::create(
        microscopeId,
        {firstComponent, secondComponent},
        {sharedConnection},
        {forwardPath, reversePath});

    check(static_cast<bool>(definition),
          "reuses one physical connection in two paths with opposite traversal direction");
    if (definition) {
        check(definition.value().connections().size() == 1 &&
              definition.value().opticalPaths().size() == 2,
              "stores the shared physical adjacency once while retaining both path directions");
    }
}

void testDuplicatePhysicalIdentitiesAreRejected() {
    auto fixture = makeSharedSplitterFixture();
    fixture.components.push_back(fixture.components.front());

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(!definition &&
          definition.error() == EquipmentDefinitionError::DuplicateComponentId,
          "rejects duplicate physical component identity");
}

void testUnknownConnectionPortIsRejected() {
    auto fixture = makeSharedSplitterFixture();
    fixture.connections[0] = makeConnection(
        "connection-unknown-source",
        makeId<OpticalPortId>("unknown-port"),
        fixture.condenserInput);

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(!definition &&
          definition.error() ==
              EquipmentDefinitionError::UnknownConnectionFirstPort,
          "rejects a connection that references an unknown port");
}

void testConnectionEndpointOrderDoesNotDefinePropagationDirection() {
    auto fixture = makeSharedSplitterFixture();
    fixture.connections[0] = makeConnection(
        "connection-condenser-source-reversed",
        fixture.condenserInput,
        fixture.sourceOutput);

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(static_cast<bool>(definition),
          "accepts the same physical adjacency regardless of endpoint storage order");
}

void testAPhysicalPortCannotBeConnectedTwice() {
    auto fixture = makeSharedSplitterFixture();
    fixture.connections.push_back(makeConnection(
        "connection-source-second-use",
        fixture.sourceOutput,
        fixture.cameraAInput));

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(!definition &&
          definition.error() == EquipmentDefinitionError::PortAlreadyConnected,
          "rejects one physical port being assigned to multiple connections");
}

void testPathTraversalDirectionIsValidatedIndependently() {
    auto fixture = makeSharedSplitterFixture();
    fixture.paths[0] = take(OpticalPath::create(
        makeId<OpticalPathId>("transmitted-path-a"),
        {makeStep(fixture.sourceId, std::nullopt, fixture.sourceOutput),
         makeStep(fixture.condenserId, fixture.condenserOutput, fixture.condenserInput),
         makeStep(fixture.splitterId, fixture.splitterInput, fixture.splitterOutputA),
         makeStep(fixture.objectiveAId, fixture.objectiveAInput, fixture.objectiveAOutput),
         makeStep(fixture.cameraAId, fixture.cameraAInput, std::nullopt)}));

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(!definition &&
          definition.error() ==
              EquipmentDefinitionError::PathEntryPortDirectionMismatch,
          "path traversal still requires an input-capable entry port");
}

void testDisconnectedPathIsRejected() {
    auto fixture = makeSharedSplitterFixture();
    fixture.connections.erase(fixture.connections.begin() + 2);

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(!definition &&
          definition.error() == EquipmentDefinitionError::MissingRouteConnection,
          "rejects an ordered optical path with a missing physical connection");
}

void testPathPortMustBelongToItsComponent() {
    auto fixture = makeSharedSplitterFixture();
    fixture.paths[0] = take(OpticalPath::create(
        makeId<OpticalPathId>("transmitted-path-a"),
        {makeStep(fixture.sourceId, std::nullopt, fixture.sourceOutput),
         makeStep(fixture.condenserId, fixture.splitterInput, fixture.condenserOutput),
         makeStep(fixture.splitterId, fixture.splitterInput, fixture.splitterOutputA),
         makeStep(fixture.objectiveAId, fixture.objectiveAInput, fixture.objectiveAOutput),
         makeStep(fixture.cameraAId, fixture.cameraAInput, std::nullopt)}));

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(!definition &&
          definition.error() == EquipmentDefinitionError::PathEntryPortWrongOwner,
          "rejects a path step whose input port belongs to another component");
}

void testMissingIntermediateEntryIsRejected() {
    auto fixture = makeSharedSplitterFixture();
    fixture.paths[0] = take(OpticalPath::create(
        makeId<OpticalPathId>("transmitted-path-a"),
        {makeStep(fixture.sourceId, std::nullopt, fixture.sourceOutput),
         makeStep(fixture.condenserId, std::nullopt, fixture.condenserOutput),
         makeStep(fixture.splitterId, fixture.splitterInput, fixture.splitterOutputA),
         makeStep(fixture.objectiveAId, fixture.objectiveAInput, fixture.objectiveAOutput),
         makeStep(fixture.cameraAId, fixture.cameraAInput, std::nullopt)}));

    auto definition = MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        fixture.paths);

    check(!definition &&
          definition.error() == EquipmentDefinitionError::MissingPathEntryPort,
          "requires every non-source path step to declare its entry port");
}

void testComponentKindConstrainsOpticalPortConfiguration() {
    const auto invalidOpticalElement = EquipmentComponent::create(
        makeId<EquipmentComponentId>("invalid-optical-element"),
        EquipmentComponentKind::OpticalElement,
        {makePort("optical-out-a", OpticalPortDirection::Output),
         makePort("optical-out-b", OpticalPortDirection::Output)});
    check(!invalidOpticalElement &&
          invalidOpticalElement.error() ==
              EquipmentComponentError::InvalidPortConfiguration,
          "requires an optical element to expose input and output capability");

    const auto invalidImagingDevice = EquipmentComponent::create(
        makeId<EquipmentComponentId>("invalid-imaging-device"),
        EquipmentComponentKind::ImagingDevice,
        {makePort("imaging-output-only", OpticalPortDirection::Output)});
    check(!invalidImagingDevice &&
          invalidImagingDevice.error() ==
              EquipmentComponentError::InvalidPortConfiguration,
          "requires an imaging device to expose an optical input");

    const auto invalidIlluminationDevice = EquipmentComponent::create(
        makeId<EquipmentComponentId>("invalid-illumination-device"),
        EquipmentComponentKind::IlluminationDevice,
        {makePort("illumination-input-only", OpticalPortDirection::Input)});
    check(!invalidIlluminationDevice &&
          invalidIlluminationDevice.error() ==
              EquipmentComponentError::InvalidPortConfiguration,
          "requires an illumination device to expose an optical output");

    const auto invalidMechanicalAssembly = EquipmentComponent::create(
        makeId<EquipmentComponentId>("invalid-mechanical-assembly"),
        EquipmentComponentKind::MechanicalAssembly,
        {makePort("mechanical-optical-port", OpticalPortDirection::Bidirectional)});
    check(!invalidMechanicalAssembly &&
          invalidMechanicalAssembly.error() ==
              EquipmentComponentError::InvalidPortConfiguration,
          "does not treat a mechanical assembly as an optical endpoint");

    const auto validMechanicalAssembly = EquipmentComponent::create(
        makeId<EquipmentComponentId>("valid-mechanical-assembly"),
        EquipmentComponentKind::MechanicalAssembly,
        {});
    check(static_cast<bool>(validMechanicalAssembly),
          "allows a mechanical assembly without optical ports");
}

void testEmptyEquipmentDefinitionIsRejected() {
    auto definition = MicroscopeEquipmentDefinition::create(
        makeId<MicroscopeId>("empty-equipment-microscope"),
        {},
        {},
        {});
    check(!definition &&
          definition.error() == EquipmentDefinitionError::EmptyComponentSet,
          "rejects an equipment definition with no installed components");
}

void testOpticalFunctionCapabilitiesAreTypedAndUnique() {
    const auto compatibilityId =
        makeId<OpticalCompatibilityId>("optical-compatibility-phase-1");
    const auto phasePlate = take(OpticalFunctionCapability::create(
        OpticalElementFunction::PhasePlate, compatibilityId));

    const auto invalidOnImagingDevice = EquipmentComponent::create(
        makeId<EquipmentComponentId>("imaging-device-with-optical-function"),
        EquipmentComponentKind::ImagingDevice,
        {makePort("imaging-input", OpticalPortDirection::Input)},
        {phasePlate});
    check(!invalidOnImagingDevice &&
          invalidOnImagingDevice.error() ==
              EquipmentComponentError::OpticalFunctionsOnNonOpticalElement,
          "allows optical function capabilities only on optical elements");

    const auto duplicateCapability = EquipmentComponent::create(
        makeId<EquipmentComponentId>("duplicate-phase-capability"),
        EquipmentComponentKind::OpticalElement,
        {makePort("duplicate-phase-input", OpticalPortDirection::Input),
         makePort("duplicate-phase-output", OpticalPortDirection::Output)},
        {phasePlate, phasePlate});
    check(!duplicateCapability &&
          duplicateCapability.error() ==
              EquipmentComponentError::DuplicateOpticalFunctionCapability,
          "rejects duplicate function/compatibility capability pairs");

    const auto otherCompatibility = take(OpticalFunctionCapability::create(
        OpticalElementFunction::PhasePlate,
        makeId<OpticalCompatibilityId>("optical-compatibility-phase-2")));
    const auto multipleVariants = EquipmentComponent::create(
        makeId<EquipmentComponentId>("multi-variant-phase-component"),
        EquipmentComponentKind::OpticalElement,
        {makePort("multi-phase-input", OpticalPortDirection::Input),
         makePort("multi-phase-output", OpticalPortDirection::Output)},
        {phasePlate, otherCompatibility});
    check(static_cast<bool>(multipleVariants),
          "allows one component to declare distinct compatibility variants when physically supported");

    auto invalidFunction = OpticalFunctionCapability::create(
        static_cast<OpticalElementFunction>(255));
    check(!invalidFunction &&
          invalidFunction.error() == OpticalFunctionCapabilityError::InvalidFunction,
          "rejects unknown optical function capability values");
}

void testPathAndPortFactoryValidation() {
    auto oneStepPath = OpticalPath::create(
        makeId<OpticalPathId>("one-step-path"),
        {makeStep(
            makeId<EquipmentComponentId>("one-component"),
            std::nullopt,
            makeId<OpticalPortId>("only-port"))});
    check(!oneStepPath &&
          oneStepPath.error() == OpticalPathError::TooFewSteps,
          "rejects an optical path with fewer than two component steps");

    auto noPortStep = OpticalPathStep::create(
        makeId<EquipmentComponentId>("no-port-component"),
        std::nullopt,
        std::nullopt);
    check(!noPortStep &&
          noPortStep.error() == OpticalPathStepError::NoPort,
          "rejects a path step without an entry or exit port");

    auto samePortStep = OpticalPathStep::create(
        makeId<EquipmentComponentId>("same-port-component"),
        makeId<OpticalPortId>("same-port"),
        makeId<OpticalPortId>("same-port"));
    check(!samePortStep &&
          samePortStep.error() == OpticalPathStepError::SameEntryAndExitPort,
          "rejects a path step that uses one physical port as both entry and exit");

    auto invalidDirection = OpticalPort::create(
        makeId<OpticalPortId>("invalid-direction"),
        static_cast<OpticalPortDirection>(255));
    check(!invalidDirection &&
          invalidDirection.error() == OpticalPortError::InvalidDirection,
          "rejects an unknown optical port direction");
}

} // namespace

struct ImagingLightPathFixture final {
    std::vector<OpticalPath> imagingPaths;
    LightPath lightPath;
};

ImagingLightPathFixture makeImagingLightPath(
    const EquipmentFixture& fixture,
    const char* lightPathId,
    bool includeSecondCamera) {
    auto pathA = take(OpticalPath::create(
        fixture.paths[0].id(),
        fixture.paths[0].steps(),
        OpticalPathRole::Imaging));
    auto pathB = take(OpticalPath::create(
        fixture.paths[1].id(),
        fixture.paths[1].steps(),
        OpticalPathRole::Imaging));

    std::vector<OpticalPath> paths{pathA};
    std::vector<OpticalPathId> pathIds{pathA.id()};
    if (includeSecondCamera) {
        paths.push_back(pathB);
        pathIds.push_back(pathB.id());
    }

    auto lightPath = take(LightPath::create(
        makeId<LightPathId>(lightPathId),
        "Test camera LightPath",
        std::move(pathIds)));
    return ImagingLightPathFixture{std::move(paths), std::move(lightPath)};
}

OperationMode makeCameraResolverMode(
    const EquipmentFixture& fixture,
    const LightPathId& lightPathId,
    ContrastMethod contrastMethod,
    const char* modeId = "camera-resolver-mode") {
    const auto operationModeId = makeId<OperationModeId>(modeId);
    auto setting = take(LightPathModeSetting::create(
        fixture.microscopeId,
        1,
        operationModeId,
        1,
        lightPathId,
        makeId<CalibrationRevisionId>("camera-resolver-calibration"),
        fixture.sourceId,
        makeId<IlluminationChannelId>("camera-resolver-channel"),
        {fixture.objectiveAId},
        6.0));
    return take(OperationMode::create(
        fixture.microscopeId,
        operationModeId,
        1,
        "Camera resolver mode",
        contrastMethod,
        {std::move(setting)}));
}

CameraSpecification cameraSpecificationWithSensor(
    const EquipmentComponentId& componentId,
    const char* cameraModeId,
    CameraShutterType shutterType,
    double readNoise,
    double qeAt550) {
    const auto base = cameraSpecification(componentId, cameraModeId);
    CameraSensorCharacteristics sensor;
    sensor.technology = CameraSensorTechnology::ScientificCMOS;
    sensor.shutterType = shutterType;
    sensor.colorMode = CameraSensorColorMode::Monochrome;
    sensor.readNoiseElectronsRms = readNoise;
    sensor.darkCurrentElectronsPerPixelPerSecond = 0.02;
    sensor.fullWellCapacityElectrons = 30000.0;
    sensor.maximumFrameRateFramesPerSecond = 100.0;
    sensor.quantumEfficiency = {
        {500.0, 0.85},
        {550.0, qeAt550},
        {600.0, 0.80}};
    sensor.provenance = CameraSpecificationProvenance::ManufacturerDatasheet;
    sensor.provenanceReference = "test-camera-datasheet";

    return take(CameraSpecification::create(
        componentId,
        base.activeSensorDimensions(),
        micrometers(base.pixelPitchXMicrometers()),
        micrometers(base.pixelPitchYMicrometers()),
        base.outputModes(),
        std::nullopt,
        sensor));
}

FluorescenceCameraRequirements cameraResolverFluorescenceRequirements() {
    return FluorescenceCameraRequirements{
        take(SpectralBand::create(
            take(PositiveNanometers::create(500.0)),
            take(PositiveNanometers::create(600.0)))),
        0.75,
        2.0,
        0.1,
        20000.0,
        30.0,
        true,
        true};
}

void testLightPathCameraResolverDerivesSingleConnectedEndpoint() {
    auto fixture = makeSharedSplitterFixture();
    const auto imaging = makeImagingLightPath(
        fixture, "camera-resolver-single-path", false);
    const auto cameraCId =
        makeId<EquipmentComponentId>("camera-outside-selected-light-path");
    fixture.components.push_back(take(EquipmentComponent::create(
        cameraCId,
        EquipmentComponentKind::ImagingDevice,
        {makePort("camera-outside-selected-path-input", OpticalPortDirection::Input)})));

    const auto equipment = take(MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        imaging.imagingPaths,
        {},
        {cameraSpecification(fixture.cameraAId, "camera-resolver-mode-a"),
         cameraSpecification(fixture.cameraBId, "camera-resolver-mode-b"),
         cameraSpecification(cameraCId, "camera-resolver-mode-c")},
        {}, {}, {}, {}, {imaging.lightPath}));

    const auto mode = makeCameraResolverMode(
        fixture, imaging.lightPath.id(), ContrastMethod::Brightfield);
    const auto resolution = LightPathCameraResolver::resolve(
        equipment, mode, imaging.lightPath.id());

    check(resolution.status() == LightPathCameraResolutionStatus::Resolved,
          "resolves the only camera endpoint on the selected LightPath");
    check(resolution.selected() &&
          resolution.selected()->cameraComponentId == fixture.cameraAId,
          "derives the camera identity from the selected imaging route terminal");
    check(resolution.selected() &&
          resolution.selected()->cameraModeId ==
              makeId<CameraModeId>("camera-resolver-mode-a"),
          "auto-selects the camera's only available output mode");
    check(resolution.candidates().size() == 1 &&
          resolution.candidates().front().cameraComponentId != cameraCId,
          "does not include an installed camera outside the selected LightPath");
}

void testManualConnectedCameraRemainsSelectedWithSuitabilityWarnings() {
    auto fixture = makeSharedSplitterFixture();
    const auto imaging = makeImagingLightPath(
        fixture, "camera-resolver-manual-path", false);
    const auto equipment = take(MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        imaging.imagingPaths,
        {},
        {cameraSpecification(fixture.cameraAId, "camera-resolver-manual-mode")},
        {}, {}, {}, {}, {imaging.lightPath}));

    const auto mode = makeCameraResolverMode(
        fixture, imaging.lightPath.id(), ContrastMethod::Fluorescence);
    const auto resolution = LightPathCameraResolver::resolve(
        equipment,
        mode,
        imaging.lightPath.id(),
        fixture.cameraAId,
        std::nullopt,
        cameraResolverFluorescenceRequirements());

    check(resolution.status() == LightPathCameraResolutionStatus::Resolved,
          "keeps a manually selected connected camera executable when suitability data are unknown");
    check(resolution.selected() &&
          resolution.selected()->cameraComponentId == fixture.cameraAId,
          "does not silently replace the user's camera selection");
    check(!resolution.diagnostics().empty() &&
          resolution.diagnostics().front().severity() == DiagnosticSeverity::Warning,
          "reports unknown fluorescence characteristics as warnings");
    check(resolution.selected() &&
          resolution.selected()->cameraModeId ==
              makeId<CameraModeId>("camera-resolver-manual-mode"),
          "still resolves the connected camera's supported output mode");
}

void testManualCameraOutsideSelectedLightPathIsUnsupported() {
    auto fixture = makeSharedSplitterFixture();
    const auto imaging = makeImagingLightPath(
        fixture, "camera-resolver-reject-path", false);
    const auto equipment = take(MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        imaging.imagingPaths,
        {},
        {cameraSpecification(fixture.cameraAId, "camera-resolver-reject-mode"),
         cameraSpecification(fixture.cameraBId, "camera-resolver-other-mode")},
        {}, {}, {}, {}, {imaging.lightPath}));

    const auto mode = makeCameraResolverMode(
        fixture, imaging.lightPath.id(), ContrastMethod::Brightfield);
    const auto resolution = LightPathCameraResolver::resolve(
        equipment,
        mode,
        imaging.lightPath.id(),
        fixture.cameraBId);

    check(resolution.status() == LightPathCameraResolutionStatus::Unsupported &&
          !resolution.selected(),
          "rejects a manual camera that is not an endpoint of the selected LightPath");
    check(std::any_of(
              resolution.diagnostics().begin(),
              resolution.diagnostics().end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "light_path_camera.manual_camera_not_on_path" &&
                      diagnostic.severity() == DiagnosticSeverity::Error;
              }),
          "explains the physical LightPath connectivity mismatch");
}

void testMultipleConnectedCamerasRequireSelectionWhenNoUniqueRecommendationExists() {
    auto fixture = makeSharedSplitterFixture();
    const auto imaging = makeImagingLightPath(
        fixture, "camera-resolver-dual-path", true);
    const auto equipment = take(MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        imaging.imagingPaths,
        {},
        {cameraSpecification(fixture.cameraAId, "camera-resolver-dual-mode-a"),
         cameraSpecification(fixture.cameraBId, "camera-resolver-dual-mode-b")},
        {}, {}, {}, {}, {imaging.lightPath}));

    const auto mode = makeCameraResolverMode(
        fixture, imaging.lightPath.id(), ContrastMethod::Brightfield);
    const auto resolution = LightPathCameraResolver::resolve(
        equipment, mode, imaging.lightPath.id());

    check(resolution.status() ==
              LightPathCameraResolutionStatus::SelectionRequired,
          "asks the user to choose when two connected camera endpoints are equally unresolved");
    check(resolution.candidates().size() == 2,
          "returns exactly the cameras connected to the selected splitter-backed LightPath");
    check(std::none_of(
              resolution.candidates().begin(),
              resolution.candidates().end(),
              [](const LightPathCameraCandidate& candidate) {
                  return candidate.recommended;
              }),
          "does not invent a recommendation when sensor evidence is unavailable");
}

void testGlobalShutterIsRecommendedOnlyWhenOtherSuitabilityMetricsTie() {
    auto fixture = makeSharedSplitterFixture();
    const auto imaging = makeImagingLightPath(
        fixture, "camera-resolver-shutter-tie", true);
    const auto equipment = take(MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        imaging.imagingPaths,
        {},
        {cameraSpecificationWithSensor(
             fixture.cameraAId, "camera-resolver-global-mode",
             CameraShutterType::Global, 1.2, 0.90),
         cameraSpecificationWithSensor(
             fixture.cameraBId, "camera-resolver-rolling-mode",
             CameraShutterType::Rolling, 1.2, 0.90)},
        {}, {}, {}, {}, {imaging.lightPath}));

    const auto mode = makeCameraResolverMode(
        fixture, imaging.lightPath.id(), ContrastMethod::Fluorescence);
    const auto resolution = LightPathCameraResolver::resolve(
        equipment,
        mode,
        imaging.lightPath.id(),
        std::nullopt,
        std::nullopt,
        cameraResolverFluorescenceRequirements());

    check(resolution.status() == LightPathCameraResolutionStatus::Resolved &&
          resolution.selected() &&
          resolution.selected()->cameraComponentId == fixture.cameraAId,
          "recommends the global-shutter camera when the fluorescence suitability evidence is otherwise equal");
    check(resolution.candidates().size() == 2 &&
          std::count_if(
              resolution.candidates().begin(),
              resolution.candidates().end(),
              [](const LightPathCameraCandidate& candidate) {
                  return candidate.recommended;
              }) == 1,
          "marks exactly one recommended camera while retaining both connected alternatives");
}

void testLightPathRequestFactoryDerivesCameraAndAllRouteIds() {
    auto fixture = makeSharedSplitterFixture();
    const auto imaging = makeImagingLightPath(
        fixture, "camera-request-factory-path", true);
    const auto cameraModeA =
        makeId<CameraModeId>("camera-request-factory-mode-a");
    const auto cameraModeB =
        makeId<CameraModeId>("camera-request-factory-mode-b");
    const auto equipment = take(MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        imaging.imagingPaths,
        {},
        {cameraSpecification(fixture.cameraAId, "camera-request-factory-mode-a"),
         cameraSpecification(fixture.cameraBId, "camera-request-factory-mode-b")},
        {}, {}, {}, {}, {imaging.lightPath}));

    const auto mode = makeCameraResolverMode(
        fixture, imaging.lightPath.id(), ContrastMethod::Brightfield);
    const auto cameraResolution = LightPathCameraResolver::resolve(
        equipment,
        mode,
        imaging.lightPath.id(),
        fixture.cameraBId,
        cameraModeB);
    check(cameraResolution.isResolved(),
          "resolves a manual camera endpoint before creating a configuration request");

    const auto request = LightPathConfigurationRequestFactory::create(
        equipment,
        mode,
        imaging.lightPath.id(),
        cameraResolution,
        makeId<ConfigurationProfileId>("camera-request-factory-profile"),
        fixture.objectiveAId,
        fixture.condenserId,
        fixture.sourceId,
        makeId<IlluminationChannelId>("camera-request-factory-channel"),
        ExposurePolicy::Automatic);
    check(static_cast<bool>(request),
          "creates the legacy request only from a resolved LightPath camera choice");
    if (request) {
        check(request.value().opticalPathIds() ==
                  imaging.lightPath.opticalPathIds(),
              "derives every selected optical route ID from the LightPath rather than caller input");
        check(request.value().cameraComponentId() == fixture.cameraBId,
              "uses only the resolved camera endpoint identity");
        check(request.value().cameraModeId() == cameraModeB,
              "uses only the resolved camera output mode");
        check(request.value().contrastMethod() == mode.contrastMethod(),
              "uses the operation mode contrast semantics");
    }

    const auto otherPathId = makeId<LightPathId>("different-light-path");
    const auto wrongPathMode = makeCameraResolverMode(
        fixture, otherPathId, ContrastMethod::Brightfield,
        "camera-request-factory-wrong-path-mode");
    const auto mismatch = LightPathConfigurationRequestFactory::create(
        equipment,
        wrongPathMode,
        imaging.lightPath.id(),
        cameraResolution,
        makeId<ConfigurationProfileId>("camera-request-factory-profile-2"),
        fixture.objectiveAId,
        fixture.condenserId,
        fixture.sourceId,
        makeId<IlluminationChannelId>("camera-request-factory-channel"),
        ExposurePolicy::Automatic);
    check(!mismatch &&
          mismatch.error() ==
              LightPathConfigurationRequestFactoryError::OperationModeDoesNotContainLightPath,
          "rejects a mode that does not configure the selected LightPath");
}

void testOperationModeWithoutSelectedLightPathSettingIsUnsupported() {
    auto fixture = makeSharedSplitterFixture();
    const auto imaging = makeImagingLightPath(
        fixture, "camera-resolver-unconfigured-path", false);
    const auto equipment = take(MicroscopeEquipmentDefinition::create(
        fixture.microscopeId,
        fixture.components,
        fixture.connections,
        imaging.imagingPaths,
        {},
        {cameraSpecification(fixture.cameraAId, "camera-resolver-unconfigured-mode")},
        {}, {}, {}, {}, {imaging.lightPath}));

    const auto otherLightPathId =
        makeId<LightPathId>("camera-resolver-another-mode-path");
    const auto mode = makeCameraResolverMode(
        fixture, otherLightPathId, ContrastMethod::Brightfield);
    const auto resolution = LightPathCameraResolver::resolve(
        equipment, mode, imaging.lightPath.id());

    check(resolution.status() == LightPathCameraResolutionStatus::Unsupported,
          "requires the selected LightPath to be configured by the operation mode");
}

int main() {
    testStrongEquipmentIdentities();
    testSharedComponentsAndBranching();
    testInstalledFilterSpecificationsMatchOpticalFunctionsAndCompatibility();
    testLightPathDerivesOnlyConnectedCameraEndpoints();
    testImagingRouteMustTerminateAtSpecifiedCamera();
    testLightPathMustContainAnImagingRoute();
    testMultipleCameraLightPathRequiresPhysicalSplitter();
    testLightPathFactoryRejectsDuplicateRoutes();
    testLightPathCameraResolverDerivesSingleConnectedEndpoint();
    testManualConnectedCameraRemainsSelectedWithSuitabilityWarnings();
    testManualCameraOutsideSelectedLightPathIsUnsupported();
    testMultipleConnectedCamerasRequireSelectionWhenNoUniqueRecommendationExists();
    testGlobalShutterIsRecommendedOnlyWhenOtherSuitabilityMetricsTie();
    testLightPathRequestFactoryDerivesCameraAndAllRouteIds();
    testOperationModeWithoutSelectedLightPathSettingIsUnsupported();
    testSharedPhysicalConnectionSupportsOppositePathTraversal();
    testDuplicatePhysicalIdentitiesAreRejected();
    testUnknownConnectionPortIsRejected();
    testConnectionEndpointOrderDoesNotDefinePropagationDirection();
    testPathTraversalDirectionIsValidatedIndependently();
    testAPhysicalPortCannotBeConnectedTwice();
    testDisconnectedPathIsRejected();
    testPathPortMustBelongToItsComponent();
    testMissingIntermediateEntryIsRejected();
    testComponentKindConstrainsOpticalPortConfiguration();
    testEmptyEquipmentDefinitionIsRejected();
    testOpticalFunctionCapabilitiesAreTypedAndUnique();
    testPathAndPortFactoryValidation();

    if (failures != 0) {
        std::cerr << failures << " equipment topology contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All equipment topology contract tests passed.\n";
    return 0;
}
