#include "microscope/core/equipment/OpticalTopology.h"

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

struct PortOwner final {
    EquipmentComponentId componentId;
    OpticalPortDirection direction;
};

} // namespace

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
    std::vector<OpticalPort> opticalPorts)
    : id_(std::move(id)),
      kind_(kind),
      opticalPorts_(std::move(opticalPorts)) {}

Result<EquipmentComponent, EquipmentComponentError> EquipmentComponent::create(
    EquipmentComponentId id,
    EquipmentComponentKind kind,
    std::vector<OpticalPort> opticalPorts) {
    if (!isKnownComponentKind(kind)) {
        return Result<EquipmentComponent, EquipmentComponentError>::failure(
            EquipmentComponentError::InvalidKind);
    }

    std::set<OpticalPortId> portIds;
    for (const auto& port : opticalPorts) {
        if (!portIds.insert(port.id()).second) {
            return Result<EquipmentComponent, EquipmentComponentError>::failure(
                EquipmentComponentError::DuplicatePortId);
        }
    }

    return Result<EquipmentComponent, EquipmentComponentError>::success(
        EquipmentComponent(
            std::move(id),
            kind,
            std::move(opticalPorts)));
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

OpticalConnection::OpticalConnection(
    OpticalConnectionId id,
    OpticalPortId fromPortId,
    OpticalPortId toPortId)
    : id_(std::move(id)),
      fromPortId_(std::move(fromPortId)),
      toPortId_(std::move(toPortId)) {}

Result<OpticalConnection, OpticalConnectionError> OpticalConnection::create(
    OpticalConnectionId id,
    OpticalPortId fromPortId,
    OpticalPortId toPortId) {
    if (fromPortId == toPortId) {
        return Result<OpticalConnection, OpticalConnectionError>::failure(
            OpticalConnectionError::SamePort);
    }

    return Result<OpticalConnection, OpticalConnectionError>::success(
        OpticalConnection(
            std::move(id),
            std::move(fromPortId),
            std::move(toPortId)));
}

const OpticalConnectionId& OpticalConnection::id() const noexcept {
    return id_;
}

const OpticalPortId& OpticalConnection::fromPortId() const noexcept {
    return fromPortId_;
}

const OpticalPortId& OpticalConnection::toPortId() const noexcept {
    return toPortId_;
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
    std::vector<OpticalPathStep> steps)
    : id_(std::move(id)),
      steps_(std::move(steps)) {}

Result<OpticalPath, OpticalPathError> OpticalPath::create(
    OpticalPathId id,
    std::vector<OpticalPathStep> steps) {
    if (steps.size() < 2) {
        return Result<OpticalPath, OpticalPathError>::failure(
            OpticalPathError::TooFewSteps);
    }

    return Result<OpticalPath, OpticalPathError>::success(
        OpticalPath(std::move(id), std::move(steps)));
}

const OpticalPathId& OpticalPath::id() const noexcept {
    return id_;
}

const std::vector<OpticalPathStep>& OpticalPath::steps() const noexcept {
    return steps_;
}

MicroscopeEquipmentDefinition::MicroscopeEquipmentDefinition(
    MicroscopeId microscopeId,
    std::vector<EquipmentComponent> components,
    std::vector<OpticalConnection> connections,
    std::vector<OpticalPath> opticalPaths)
    : microscopeId_(std::move(microscopeId)),
      components_(std::move(components)),
      connections_(std::move(connections)),
      opticalPaths_(std::move(opticalPaths)) {}

Result<
    MicroscopeEquipmentDefinition,
    EquipmentDefinitionError> MicroscopeEquipmentDefinition::create(
        MicroscopeId microscopeId,
        std::vector<EquipmentComponent> components,
        std::vector<OpticalConnection> connections,
        std::vector<OpticalPath> opticalPaths) {
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

        const auto from = portIndex.find(connection.fromPortId());
        if (from == portIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::UnknownConnectionFromPort);
        }

        const auto to = portIndex.find(connection.toPortId());
        if (to == portIndex.end()) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::UnknownConnectionToPort);
        }

        if (from->second.componentId == to->second.componentId) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::ConnectionWithinSameComponent);
        }

        if (!supportsOutput(from->second.direction) ||
            !supportsInput(to->second.direction)) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::ConnectionPortDirectionMismatch);
        }

        if (!connectedPorts.insert(connection.fromPortId()).second ||
            !connectedPorts.insert(connection.toPortId()).second) {
            return Result<
                MicroscopeEquipmentDefinition,
                EquipmentDefinitionError>::failure(
                    EquipmentDefinitionError::PortAlreadyConnected);
        }

        connectionIndex.emplace(
            std::make_pair(connection.fromPortId(), connection.toPortId()),
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

            if (!isLast) {
                const auto& nextStep = steps[index + 1];
                if (!nextStep.entryPortId()) {
                    return Result<
                        MicroscopeEquipmentDefinition,
                        EquipmentDefinitionError>::failure(
                            EquipmentDefinitionError::MissingPathEntryPort);
                }
                const auto routeConnection = connectionIndex.find(
                    std::make_pair(*step.exitPortId(), *nextStep.entryPortId()));
                if (routeConnection == connectionIndex.end()) {
                    return Result<
                        MicroscopeEquipmentDefinition,
                        EquipmentDefinitionError>::failure(
                            EquipmentDefinitionError::MissingRouteConnection);
                }
            }
        }
    }

    return Result<
        MicroscopeEquipmentDefinition,
        EquipmentDefinitionError>::success(
            MicroscopeEquipmentDefinition(
                std::move(microscopeId),
                std::move(components),
                std::move(connections),
                std::move(opticalPaths)));
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

} // namespace microscope::core
