#include "microscope/core/equipment/OpticalTopology.h"

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
         makePort("splitter-output-b", OpticalPortDirection::Output)})));

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

int main() {
    testStrongEquipmentIdentities();
    testSharedComponentsAndBranching();
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
    testPathAndPortFactoryValidation();

    if (failures != 0) {
        std::cerr << failures << " equipment topology contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All equipment topology contract tests passed.\n";
    return 0;
}
