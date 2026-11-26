#include "microscope/core/equipment/OpticalTopology.h"

#include <cstddef>
#include <map>
#include <set>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool isKnownComponentKind(EquipmentComponentKind kind) noexcept {
    switch (kind) {
    case EquipmentComponentKind::OpticalElement:
    case EquipmentComponentKind::ImagingDevice:
    case EquipmentComponentKind::IlluminationDevice:
    case EquipmentComponentKind::MechanicalAssembly:
    case EquipmentComponentKind::Other:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownPortDirection(OpticalPortDirection direction) noexcept {
    switch (direction) {
    case OpticalPortDirection::Input:
    case OpticalPortDirection::Output:
    case OpticalPortDirection::Bidirectional:
        return true;
    }
    return false;
}

[[nodiscard]] bool supportsInput(OpticalPortDirection direction) noexcept {
    return direction == OpticalPortDirection::Input ||
           direction == OpticalPortDirection::Bidirectional;
}

[[nodiscard]] bool supportsOutput(OpticalPortDirection direction) noexcept {
    return direction == OpticalPortDirection::Output ||
           direction == OpticalPortDirection::Bidirectional;
}

[[nodiscard]] bool isKnownOpticalPathRole(OpticalPathRole role) noexcept {
    switch (role) {
    case OpticalPathRole::Imaging:
    case OpticalPathRole::Illumination:
    case OpticalPathRole::Other:
        return true;
    }
    return false;
}

[[nodiscard]] bool hasOpticalFunction(
    const EquipmentComponent& component,
    OpticalElementFunction function) {
    for (const auto& capability : component.opticalFunctions()) {
        if (capability.function() == function) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool imagingRoutesShareDistinctBeamSplitterOutputs(
    const std::vector<const OpticalPath*>& imagingPaths,
    const std::map<EquipmentComponentId, const EquipmentComponent*>& componentIndex) {
    if (imagingPaths.size() < 2) {
        return false;
    }

    for (const auto& candidateStep : imagingPaths.front()->steps()) {
        const auto component = componentIndex.find(candidateStep.componentId());
        if (component == componentIndex.end() ||
            component->second->kind() != EquipmentComponentKind::OpticalElement ||
            !hasOpticalFunction(
                *component->second, OpticalElementFunction::BeamSplitter) ||
            !candidateStep.entryPortId() ||
            !candidateStep.exitPortId()) {
            continue;
        }

        const auto commonInput = *candidateStep.entryPortId();
        std::set<OpticalPortId> branchOutputs{*candidateStep.exitPortId()};
        bool sharedBranch = true;

        for (std::size_t pathIndex = 1;
             pathIndex < imagingPaths.size();
             ++pathIndex) {
            const OpticalPathStep* matchingStep = nullptr;
            for (const auto& step : imagingPaths[pathIndex]->steps()) {
                if (step.componentId() == candidateStep.componentId()) {
                    matchingStep = &step;
                    break;
                }
            }

            if (matchingStep == nullptr ||
                !matchingStep->entryPortId() ||
                !matchingStep->exitPortId() ||
                *matchingStep->entryPortId() != commonInput ||
                !branchOutputs.insert(*matchingStep->exitPortId()).second) {
                sharedBranch = false;
                break;
            }
        }

        if (sharedBranch) {
            return true;
        }
    }

    return false;
}

struct PortOwner final {
    EquipmentComponentId componentId;
    OpticalPortDirection direction;
};

} // namespace

OpticalFunctionCapability::OpticalFunctionCapability(
    OpticalElementFunction function,
    std::optional<OpticalCompatibilityId> compatibilityId)
    : function_(function),
      compatibilityId_(std::move(compatibilityId)) {}

Result<
    OpticalFunctionCapability,
    OpticalFunctionCapabilityError> OpticalFunctionCapability::create(
        OpticalElementFunction function,
        std::optional<OpticalCompatibilityId> compatibilityId) {
    switch (function) {
    case OpticalElementFunction::BeamSplitter:
    case OpticalElementFunction::PhasePlate:
    case OpticalElementFunction::PhaseAnnulus:
    case OpticalElementFunction::Polarizer:
    case OpticalElementFunction::Analyzer:
    case OpticalElementFunction::DifferentialInterferencePrism:
    case OpticalElementFunction::ExcitationFilter:
    case OpticalElementFunction::EmissionFilter:
    case OpticalElementFunction::DichroicMirror:
    case OpticalElementFunction::DarkfieldStop:
    case OpticalElementFunction::Other:
        break;
    default:
        return Result<
            OpticalFunctionCapability,
            OpticalFunctionCapabilityError>::failure(
                OpticalFunctionCapabilityError::InvalidFunction);
    }

    return Result<
        OpticalFunctionCapability,
        OpticalFunctionCapabilityError>::success(
            OpticalFunctionCapability(function, std::move(compatibilityId)));
}

OpticalElementFunction OpticalFunctionCapability::function() const noexcept {
    return function_;
}

const std::optional<OpticalCompatibilityId>&
OpticalFunctionCapability::compatibilityId() const noexcept {
    return compatibilityId_;
}

OpticalPort::OpticalPort(
    OpticalPortId id,
    OpticalPortDirection direction)
    : id_(std::move(id)),
      direction_(direction) {}

Result<OpticalPort, OpticalPortError> OpticalPort::create(
    OpticalPortId id,
    OpticalPortDirection direction) {
    if (!isKnownPortDirection(direction)) {
        return Result<OpticalPort, OpticalPortError>::failure(
            OpticalPortError::InvalidDirection);
    }

    return Result<OpticalPort, OpticalPortError>::success(
        OpticalPort(std::move(id), direction));
}

const OpticalPortId& OpticalPort::id() const noexcept {
    return id_;
}

OpticalPortDirection OpticalPort::direction() const noexcept {
    return direction_;
}

EquipmentComponent::EquipmentComponent(
    EquipmentComponentId id,
    EquipmentComponentKind kind,
    std::vector<OpticalPort> opticalPorts,
    std::vector<OpticalFunctionCapability> opticalFunctions)
    : id_(std::move(id)),
      kind_(kind),
      opticalPorts_(std::move(opticalPorts)),
      opticalFunctions_(std::move(opticalFunctions)) {}

Result<EquipmentComponent, EquipmentComponentError> EquipmentComponent::create(
    EquipmentComponentId id,
    EquipmentComponentKind kind,
    std::vector<OpticalPort> opticalPorts,
    std::vector<OpticalFunctionCapability> opticalFunctions) {
    if (!isKnownComponentKind(kind)) {
        return Result<EquipmentComponent, EquipmentComponentError>::failure(
            EquipmentComponentError::InvalidKind);
    }

    std::set<OpticalPortId> portIds;
    bool hasInput = false;
    bool hasOutput = false;
    for (const auto& port : opticalPorts) {
        if (!portIds.insert(port.id()).second) {
            return Result<EquipmentComponent, EquipmentComponentError>::failure(
                EquipmentComponentError::DuplicatePortId);
        }
        hasInput = hasInput || supportsInput(port.direction());
        hasOutput = hasOutput || supportsOutput(port.direction());
    }

    bool validPortConfiguration = true;
    switch (kind) {
    case EquipmentComponentKind::OpticalElement:
        validPortConfiguration =
            opticalPorts.size() >= 2 && hasInput && hasOutput;
        break;
    case EquipmentComponentKind::ImagingDevice:
        validPortConfiguration = hasInput;
        break;
    case EquipmentComponentKind::IlluminationDevice:
        validPortConfiguration = hasOutput;
        break;
    case EquipmentComponentKind::MechanicalAssembly:
        validPortConfiguration = opticalPorts.empty();
        break;
    case EquipmentComponentKind::Other:
        break;
    }
    if (!validPortConfiguration) {
        return Result<EquipmentComponent, EquipmentComponentError>::failure(
            EquipmentComponentError::InvalidPortConfiguration);
    }

    if (kind != EquipmentComponentKind::OpticalElement &&
        !opticalFunctions.empty()) {
        return Result<EquipmentComponent, EquipmentComponentError>::failure(
            EquipmentComponentError::OpticalFunctionsOnNonOpticalElement);
    }

    for (std::size_t i = 0; i < opticalFunctions.size(); ++i) {
        for (std::size_t j = i + 1; j < opticalFunctions.size(); ++j) {
            if (opticalFunctions[i].function() == opticalFunctions[j].function() &&
                opticalFunctions[i].compatibilityId() ==
                    opticalFunctions[j].compatibilityId()) {
                return Result<EquipmentComponent, EquipmentComponentError>::failure(
                    EquipmentComponentError::DuplicateOpticalFunctionCapability);
            }
        }
    }

    return Result<EquipmentComponent, EquipmentComponentError>::success(
        EquipmentComponent(
            std::move(id),
            kind,
            std::move(opticalPorts),
            std::move(opticalFunctions)));
}

const EquipmentComponentId& EquipmentComponent::id() const noexcept {
    return id_;
}

EquipmentComponentKind EquipmentComponent::kind() const noexcept {
    return kind_;
}

const std::vector<OpticalPort>& EquipmentComponent::opticalPorts() const noexcept {
    return opticalPorts_;
}

const std::vector<OpticalFunctionCapability>&
EquipmentComponent::opticalFunctions() const noexcept {
    return opticalFunctions_;
}

OpticalConnection::OpticalConnection(
    OpticalConnectionId id,
    OpticalPortId firstPortId,
    OpticalPortId secondPortId)
    : id_(std::move(id)),
      firstPortId_(std::move(firstPortId)),
      secondPortId_(std::move(secondPortId)) {}

Result<OpticalConnection, OpticalConnectionError> OpticalConnection::create(
    OpticalConnectionId id,
    OpticalPortId firstPortId,
    OpticalPortId secondPortId) {
    if (firstPortId == secondPortId) {
        return Result<OpticalConnection, OpticalConnectionError>::failure(
            OpticalConnectionError::SamePort);
    }

    return Result<OpticalConnection, OpticalConnectionError>::success(
        OpticalConnection(
            std::move(id),
            std::move(firstPortId),
            std::move(secondPortId)));
}

const OpticalConnectionId& OpticalConnection::id() const noexcept {
    return id_;
}

const OpticalPortId& OpticalConnection::firstPortId() const noexcept {
    return firstPortId_;
}

const OpticalPortId& OpticalConnection::secondPortId() const noexcept {
    return secondPortId_;
}

OpticalPathStep::OpticalPathStep(
    EquipmentComponentId componentId,
    std::optional<OpticalPortId> entryPortId,
    std::optional<OpticalPortId> exitPortId)
    : componentId_(std::move(componentId)),
      entryPortId_(std::move(entryPortId)),
      exitPortId_(std::move(exitPortId)) {}

Result<OpticalPathStep, OpticalPathStepError> OpticalPathStep::create(
    EquipmentComponentId componentId,
    std::optional<OpticalPortId> entryPortId,
    std::optional<OpticalPortId> exitPortId) {
    if (!entryPortId && !exitPortId) {
        return Result<OpticalPathStep, OpticalPathStepError>::failure(
            OpticalPathStepError::NoPort);
    }

    if (entryPortId && exitPortId && *entryPortId == *exitPortId) {
        return Result<OpticalPathStep, OpticalPathStepError>::failure(
            OpticalPathStepError::SameEntryAndExitPort);
    }

    return Result<OpticalPathStep, OpticalPathStepError>::success(
        OpticalPathStep(
            std::move(componentId),
            std::move(entryPortId),
            std::move(exitPortId)));
}

const EquipmentComponentId& OpticalPathStep::componentId() const noexcept {
    return componentId_;
}

const std::optional<OpticalPortId>& OpticalPathStep::entryPortId() const noexcept {
    return entryPortId_;
}

const std::optional<OpticalPortId>& OpticalPathStep::exitPortId() const noexcept {
    return exitPortId_;
}

OpticalPath::OpticalPath(
    OpticalPathId id,
    std::vector<OpticalPathStep> steps,
    OpticalPathRole role)
    : id_(std::move(id)),
      steps_(std::move(steps)),
      role_(role) {}

Result<OpticalPath, OpticalPathError> OpticalPath::create(
    OpticalPathId id,
    std::vector<OpticalPathStep> steps,
    OpticalPathRole role) {
    if (steps.size() < 2) {
        return Result<OpticalPath, OpticalPathError>::failure(
            OpticalPathError::TooFewSteps);
    }
    if (!isKnownOpticalPathRole(role)) {
        return Result<OpticalPath, OpticalPathError>::failure(
            OpticalPathError::InvalidRole);
    }

    return Result<OpticalPath, OpticalPathError>::success(
        OpticalPath(std::move(id), std::move(steps), role));
}

const OpticalPathId& OpticalPath::id() const noexcept {
    return id_;
}

OpticalPathRole OpticalPath::role() const noexcept {
    return role_;
}

const std::vector<OpticalPathStep>& OpticalPath::steps() const noexcept {
    return steps_;
}

LightPath::LightPath(
    LightPathId id,
    std::string name,
    std::vector<OpticalPathId> opticalPathIds)
    : id_(std::move(id)),
      name_(std::move(name)),
      opticalPathIds_(std::move(opticalPathIds)) {}

Result<LightPath, LightPathError> LightPath::create(
    LightPathId id,
    std::string name,
    std::vector<OpticalPathId> opticalPathIds) {
    if (name.empty()) {
        return Result<LightPath, LightPathError>::failure(
            LightPathError::EmptyName);
    }
    if (opticalPathIds.empty()) {
        return Result<LightPath, LightPathError>::failure(
            LightPathError::EmptyOpticalPathSet);
    }

    std::set<OpticalPathId> uniquePathIds;
    for (const auto& pathId : opticalPathIds) {
        if (!uniquePathIds.insert(pathId).second) {
            return Result<LightPath, LightPathError>::failure(
                LightPathError::DuplicateOpticalPath);
        }
    }

    return Result<LightPath, LightPathError>::success(
        LightPath(
            std::move(id),
            std::move(name),
            std::move(opticalPathIds)));
}

const LightPathId& LightPath::id() const noexcept {
    return id_;
}

const std::string& LightPath::name() const noexcept {
    return name_;
}

const std::vector<OpticalPathId>&
LightPath::opticalPathIds() const noexcept {
    return opticalPathIds_;
}

MicroscopeEquipmentDefinition::MicroscopeEquipmentDefinition(
    MicroscopeId microscopeId,
    std::vector<EquipmentComponent> components,
    std::vector<OpticalConnection> connections,
    std::vector<OpticalPath> opticalPaths,
    std::vector<ObjectiveSpecification> objectiveSpecifications,
    std::vector<CameraSpecification> cameraSpecifications,
    std::vector<CondenserSpecification> condenserSpecifications,
    std::vector<IlluminationSpecification> illuminationSpecifications,
    std::vector<StageSpecification> stageSpecifications,
    std::vector<DualSlideCarrierSpecification> dualSlideCarrierSpecifications,
    std::vector<LightPath> lightPaths)
    : microscopeId_(std::move(microscopeId)),
      components_(std::move(components)),
      connections_(std::move(connections)),
      opticalPaths_(std::move(opticalPaths)),
      lightPaths_(std::move(lightPaths)),
      objectiveSpecifications_(std::move(objectiveSpecifications)),
      cameraSpecifications_(std::move(cameraSpecifications)),
      condenserSpecifications_(std::move(condenserSpecifications)),
      illuminationSpecifications_(std::move(illuminationSpecifications)),
      stageSpecifications_(std::move(stageSpecifications)),
      dualSlideCarrierSpecifications_(std::move(dualSlideCarrierSpecifications)) {}

Result<
    MicroscopeEquipmentDefinition,
    EquipmentDefinitionError> MicroscopeEquipmentDefinition::create(
        MicroscopeId microscopeId,
        std::vector<EquipmentComponent> components,
        std::vector<OpticalConnection> connections,
        std::vector<OpticalPath> opticalPaths,
        std::vector<ObjectiveSpecification> objectiveSpecifications,
        std::vector<CameraSpecification> cameraSpecifications,
        std::vector<CondenserSpecification> condenserSpecifications,
        std::vector<IlluminationSpecification> illuminationSpecifications,
        std::vector<StageSpecification> stageSpecifications,
        std::vector<DualSlideCarrierSpecification> dualSlideCarrierSpecifications) {
    if (components.empty()) {
        return Result<
            MicroscopeEquipmentDefinition,
            EquipmentDefinitionError>::failure(
                EquipmentDefinitionError::EmptyComponentSet);
    }

    std::map<EquipmentComponentId, const EquipmentComponent*> componentIndex;
    std::map<OpticalPortId, PortOwner> portIndex;

    for (const auto& component : components) {
        if (!componentIndex.emplace(component.id(), &component).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateComponentId);
        }

        for (const auto& port : component.opticalPorts()) {
            if (!portIndex.emplace(
                    port.id(),
                    PortOwner{component.id(), port.direction()}).second) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::DuplicatePortId);
            }
        }
    }

    std::set<EquipmentComponentId> objectiveComponentIds;
    for (const auto& objective : objectiveSpecifications) {
        if (!objectiveComponentIds.insert(objective.componentId()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateObjectiveSpecification);
        }

        const auto component = componentIndex.find(objective.componentId());
        if (component == componentIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::ObjectiveReferencesUnknownComponent);
        }
        if (component->second->kind() != EquipmentComponentKind::OpticalElement) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::ObjectiveComponentWrongKind);
        }
    }

    std::set<EquipmentComponentId> cameraComponentIds;
    for (const auto& camera : cameraSpecifications) {
        if (!cameraComponentIds.insert(camera.componentId()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateCameraSpecification);
        }

        const auto component = componentIndex.find(camera.componentId());
        if (component == componentIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::CameraReferencesUnknownComponent);
        }
        if (component->second->kind() != EquipmentComponentKind::ImagingDevice) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::CameraComponentWrongKind);
        }
    }

    std::set<EquipmentComponentId> condenserComponentIds;
    for (const auto& condenser : condenserSpecifications) {
        if (!condenserComponentIds.insert(condenser.componentId()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateCondenserSpecification);
        }

        if (objectiveComponentIds.find(condenser.componentId()) !=
            objectiveComponentIds.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::ConflictingOpticalComponentSpecifications);
        }

        const auto component = componentIndex.find(condenser.componentId());
        if (component == componentIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::CondenserReferencesUnknownComponent);
        }
        if (component->second->kind() != EquipmentComponentKind::OpticalElement) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::CondenserComponentWrongKind);
        }
    }

    std::set<EquipmentComponentId> illuminationComponentIds;
    for (const auto& illumination : illuminationSpecifications) {
        if (!illuminationComponentIds.insert(illumination.componentId()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateIlluminationSpecification);
        }

        const auto component = componentIndex.find(illumination.componentId());
        if (component == componentIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::IlluminationReferencesUnknownComponent);
        }
        if (component->second->kind() != EquipmentComponentKind::IlluminationDevice) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::IlluminationComponentWrongKind);
        }
    }

    std::set<EquipmentComponentId> stageComponentIds;
    for (const auto& stage : stageSpecifications) {
        if (!stageComponentIds.insert(stage.componentId()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateStageSpecification);
        }

        const auto component = componentIndex.find(stage.componentId());
        if (component == componentIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::StageReferencesUnknownComponent);
        }
        if (component->second->kind() != EquipmentComponentKind::MechanicalAssembly) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::StageComponentWrongKind);
        }
    }

    std::set<EquipmentComponentId> carrierStageIds;
    for (const auto& carrier : dualSlideCarrierSpecifications) {
        if (!carrierStageIds.insert(carrier.stageComponentId()).second) {
            return Result<MicroscopeEquipmentDefinition, EquipmentDefinitionError>::failure(
                EquipmentDefinitionError::DuplicateDualSlideCarrierSpecification);
        }
        if (stageComponentIds.find(carrier.stageComponentId()) == stageComponentIds.end()) {
            return Result<MicroscopeEquipmentDefinition, EquipmentDefinitionError>::failure(
                EquipmentDefinitionError::DualSlideCarrierReferencesUnknownStage);
        }
    }

    std::set<OpticalConnectionId> connectionIds;
    std::set<OpticalPortId> connectedPorts;
    std::map<
        std::pair<OpticalPortId, OpticalPortId>,
        const OpticalConnection*> connectionIndex;

    for (const auto& connection : connections) {
        if (!connectionIds.insert(connection.id()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateConnectionId);
        }

        const auto first = portIndex.find(connection.firstPortId());
        if (first == portIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::UnknownConnectionFirstPort);
        }

        const auto second = portIndex.find(connection.secondPortId());
        if (second == portIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::UnknownConnectionSecondPort);
        }

        if (first->second.componentId == second->second.componentId) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::ConnectionWithinSameComponent);
        }

        if (!connectedPorts.insert(connection.firstPortId()).second ||
            !connectedPorts.insert(connection.secondPortId()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::PortAlreadyConnected);
        }

        // Physical adjacency is undirected. The ordered path determines which
        // endpoint is currently an entry and which is an exit.
        connectionIndex.emplace(
            std::make_pair(connection.firstPortId(), connection.secondPortId()),
            &connection);
        connectionIndex.emplace(
            std::make_pair(connection.secondPortId(), connection.firstPortId()),
            &connection);
    }

    std::set<OpticalPathId> pathIds;
    for (const auto& path : opticalPaths) {
        if (!pathIds.insert(path.id()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateOpticalPathId);
        }

        const auto& steps = path.steps();
        for (std::size_t index = 0; index < steps.size(); ++index) {
            const auto& step = steps[index];
            const auto component = componentIndex.find(step.componentId());
            if (component == componentIndex.end()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::UnknownPathComponent);
            }

            const bool isFirst = index == 0;
            const bool isLast = index + 1 == steps.size();

            if (isFirst && step.entryPortId()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::UnexpectedPathEntryPortAtSource);
            }
            if (isLast && step.exitPortId()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::UnexpectedPathExitPortAtTerminal);
            }

            if (!isFirst && !step.entryPortId()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::MissingPathEntryPort);
            }
            if (!isLast && !step.exitPortId()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::MissingPathExitPort);
            }

            if (step.entryPortId()) {
                const auto entry = portIndex.find(*step.entryPortId());
                if (entry == portIndex.end()) {
                    return Result<
                        MicroscopeEquipmentDefinition,
                        EquipmentDefinitionError>::failure(
                            EquipmentDefinitionError::UnknownPathEntryPort);
                }
                if (entry->second.componentId != step.componentId()) {
                    return Result<
                        MicroscopeEquipmentDefinition,
                        EquipmentDefinitionError>::failure(
                            EquipmentDefinitionError::PathEntryPortWrongOwner);
                }
                if (!supportsInput(entry->second.direction)) {
                    return Result<
                        MicroscopeEquipmentDefinition,
                        EquipmentDefinitionError>::failure(
                            EquipmentDefinitionError::PathEntryPortDirectionMismatch);
                }
            }

            if (step.exitPortId()) {
                const auto exit = portIndex.find(*step.exitPortId());
                if (exit == portIndex.end()) {
                    return Result<
                        MicroscopeEquipmentDefinition,
                        EquipmentDefinitionError>::failure(
                            EquipmentDefinitionError::UnknownPathExitPort);
                }
                if (exit->second.componentId != step.componentId()) {
                    return Result<
                        MicroscopeEquipmentDefinition,
                        EquipmentDefinitionError>::failure(
                            EquipmentDefinitionError::PathExitPortWrongOwner);
                }
                if (!supportsOutput(exit->second.direction)) {
                    return Result<
                        MicroscopeEquipmentDefinition,
                        EquipmentDefinitionError>::failure(
                            EquipmentDefinitionError::PathExitPortDirectionMismatch);
                }
            }

        }

        // Validate continuity only after every step has passed its own
        // component, port-ownership and direction checks. This keeps a
        // malformed next step from being reported merely as a missing edge.
        for (std::size_t index = 0; index + 1 < steps.size(); ++index) {
            const auto& currentStep = steps[index];
            const auto& nextStep = steps[index + 1];
            if (!currentStep.exitPortId()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::MissingPathExitPort);
            }
            if (!nextStep.entryPortId()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::MissingPathEntryPort);
            }

            const auto routeConnection = connectionIndex.find(
                std::make_pair(*currentStep.exitPortId(), *nextStep.entryPortId()));
            if (routeConnection == connectionIndex.end()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::MissingRouteConnection);
            }
        }

        if (path.role() == OpticalPathRole::Imaging) {
            const auto& terminalComponentId = steps.back().componentId();
            const auto terminalComponent = componentIndex.find(terminalComponentId);
            if (terminalComponent == componentIndex.end() ||
                terminalComponent->second->kind() !=
                    EquipmentComponentKind::ImagingDevice) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::ImagingPathDoesNotTerminateAtCamera);
            }
            if (cameraComponentIds.find(terminalComponentId) ==
                cameraComponentIds.end()) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::ImagingPathCameraSpecificationMissing);
            }
        }
    }

    std::set<LightPathId> lightPathIds;
    for (const auto& lightPath : lightPaths) {
        if (!lightPathIds.insert(lightPath.id()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::DuplicateLightPathId);
        }

        std::vector<const OpticalPath*> imagingPaths;
        std::set<EquipmentComponentId> cameraEndpointIds;
        for (const auto& opticalPathId : lightPath.opticalPathIds()) {
            const OpticalPath* referencedPath = nullptr;
            for (const auto& candidatePath : opticalPaths) {
                if (candidatePath.id() == opticalPathId) {
                    referencedPath = &candidatePath;
                    break;
                }
            }

            if (referencedPath == nullptr) {
                return Result<
                    MicroscopeEquipmentDefinition,
                    EquipmentDefinitionError>::failure(
                        EquipmentDefinitionError::UnknownLightPathOpticalPath);
            }

            if (referencedPath->role() == OpticalPathRole::Imaging) {
                imagingPaths.push_back(referencedPath);
                cameraEndpointIds.insert(
                    referencedPath->steps().back().componentId());
            }
        }

        if (imagingPaths.empty()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::LightPathHasNoImagingRoute);
        }

        if (cameraEndpointIds.size() > 1 &&
            !imagingRoutesShareDistinctBeamSplitterOutputs(
                imagingPaths, componentIndex)) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::LightPathMultipleCamerasRequireSplitter);
        }
    }

    return Result<
        MicroscopeEquipmentDefinition,
        EquipmentDefinitionError>::success(
            MicroscopeEquipmentDefinition(
                std::move(microscopeId),
                std::move(components),
                std::move(connections),
                std::move(opticalPaths),
                std::move(objectiveSpecifications),
                std::move(cameraSpecifications),
                std::move(condenserSpecifications),
                std::move(illuminationSpecifications),
                std::move(stageSpecifications),
                std::move(dualSlideCarrierSpecifications),
                std::move(lightPaths)));
}

const MicroscopeId& MicroscopeEquipmentDefinition::microscopeId() const noexcept {
    return microscopeId_;
}

const std::vector<EquipmentComponent>&
MicroscopeEquipmentDefinition::components() const noexcept {
    return components_;
}

const std::vector<OpticalConnection>&
MicroscopeEquipmentDefinition::connections() const noexcept {
    return connections_;
}

const std::vector<OpticalPath>&
MicroscopeEquipmentDefinition::opticalPaths() const noexcept {
    return opticalPaths_;
}

const std::vector<LightPath>&
MicroscopeEquipmentDefinition::lightPaths() const noexcept {
    return lightPaths_;
}

std::vector<EquipmentComponentId>
MicroscopeEquipmentDefinition::cameraEndpointIds(
    const LightPathId& lightPathId) const {
    std::vector<EquipmentComponentId> cameraIds;

    const LightPath* selectedLightPath = nullptr;
    for (const auto& candidate : lightPaths_) {
        if (candidate.id() == lightPathId) {
            selectedLightPath = &candidate;
            break;
        }
    }
    if (selectedLightPath == nullptr) {
        return cameraIds;
    }

    for (const auto& opticalPathId : selectedLightPath->opticalPathIds()) {
        for (const auto& path : opticalPaths_) {
            if (path.id() != opticalPathId ||
                path.role() != OpticalPathRole::Imaging) {
                continue;
            }

            const auto& cameraId = path.steps().back().componentId();
            bool alreadyPresent = false;
            for (const auto& existing : cameraIds) {
                if (existing == cameraId) {
                    alreadyPresent = true;
                    break;
                }
            }
            if (!alreadyPresent) {
                cameraIds.push_back(cameraId);
            }
            break;
        }
    }

    return cameraIds;
}


const std::vector<ObjectiveSpecification>&
MicroscopeEquipmentDefinition::objectiveSpecifications() const noexcept {
    return objectiveSpecifications_;
}

const std::vector<CameraSpecification>&
MicroscopeEquipmentDefinition::cameraSpecifications() const noexcept {
    return cameraSpecifications_;
}


const std::vector<CondenserSpecification>&
MicroscopeEquipmentDefinition::condenserSpecifications() const noexcept {
    return condenserSpecifications_;
}

const std::vector<IlluminationSpecification>&
MicroscopeEquipmentDefinition::illuminationSpecifications() const noexcept {
    return illuminationSpecifications_;
}


const std::vector<StageSpecification>&
MicroscopeEquipmentDefinition::stageSpecifications() const noexcept {
    return stageSpecifications_;
}

const std::vector<DualSlideCarrierSpecification>&
MicroscopeEquipmentDefinition::dualSlideCarrierSpecifications() const noexcept {
    return dualSlideCarrierSpecifications_;
}

} // namespace microscope::core
