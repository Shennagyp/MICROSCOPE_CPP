#pragma once

#include "microscope/core/equipment/IlluminationSpecifications.h"
#include "microscope/core/equipment/OpticalSpecifications.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

#include <optional>
#include <vector>

namespace microscope::core {

enum class EquipmentComponentKind {
    OpticalElement,
    ImagingDevice,
    IlluminationDevice,
    MechanicalAssembly,
    Other
};

enum class OpticalPortDirection {
    Input,
    Output,
    Bidirectional
};

enum class OpticalPortError {
    InvalidDirection
};

class OpticalPort final {
public:
    [[nodiscard]] static Result<OpticalPort, OpticalPortError> create(
        OpticalPortId id,
        OpticalPortDirection direction);

    [[nodiscard]] const OpticalPortId& id() const noexcept;
    [[nodiscard]] OpticalPortDirection direction() const noexcept;

private:
    OpticalPort(OpticalPortId id, OpticalPortDirection direction);

    OpticalPortId id_;
    OpticalPortDirection direction_;
};

enum class EquipmentComponentError {
    InvalidKind,
    DuplicatePortId,
    InvalidPortConfiguration
};

class EquipmentComponent final {
public:
    [[nodiscard]] static Result<
        EquipmentComponent,
        EquipmentComponentError> create(
            EquipmentComponentId id,
            EquipmentComponentKind kind,
            std::vector<OpticalPort> opticalPorts);

    [[nodiscard]] const EquipmentComponentId& id() const noexcept;
    [[nodiscard]] EquipmentComponentKind kind() const noexcept;
    [[nodiscard]] const std::vector<OpticalPort>& opticalPorts() const noexcept;

private:
    EquipmentComponent(
        EquipmentComponentId id,
        EquipmentComponentKind kind,
        std::vector<OpticalPort> opticalPorts);

    EquipmentComponentId id_;
    EquipmentComponentKind kind_;
    std::vector<OpticalPort> opticalPorts_;
};

enum class OpticalConnectionError {
    SamePort
};

// A directed physical connection from one component's output-capable port
// to another component's input-capable port. Internal traversal through a
// component is represented by OpticalPathStep, not by a self-connection.
class OpticalConnection final {
public:
    [[nodiscard]] static Result<
        OpticalConnection,
        OpticalConnectionError> create(
            OpticalConnectionId id,
            OpticalPortId fromPortId,
            OpticalPortId toPortId);

    [[nodiscard]] const OpticalConnectionId& id() const noexcept;
    [[nodiscard]] const OpticalPortId& fromPortId() const noexcept;
    [[nodiscard]] const OpticalPortId& toPortId() const noexcept;

private:
    OpticalConnection(
        OpticalConnectionId id,
        OpticalPortId fromPortId,
        OpticalPortId toPortId);

    OpticalConnectionId id_;
    OpticalPortId fromPortId_;
    OpticalPortId toPortId_;
};

enum class OpticalPathStepError {
    NoPort,
    SameEntryAndExitPort
};

// Describes how light traverses one physical component. Entry is optional
// only at the beginning of a path; exit is optional only at the end.
class OpticalPathStep final {
public:
    [[nodiscard]] static Result<
        OpticalPathStep,
        OpticalPathStepError> create(
            EquipmentComponentId componentId,
            std::optional<OpticalPortId> entryPortId,
            std::optional<OpticalPortId> exitPortId);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] const std::optional<OpticalPortId>& entryPortId() const noexcept;
    [[nodiscard]] const std::optional<OpticalPortId>& exitPortId() const noexcept;

private:
    OpticalPathStep(
        EquipmentComponentId componentId,
        std::optional<OpticalPortId> entryPortId,
        std::optional<OpticalPortId> exitPortId);

    EquipmentComponentId componentId_;
    std::optional<OpticalPortId> entryPortId_;
    std::optional<OpticalPortId> exitPortId_;
};

enum class OpticalPathError {
    TooFewSteps
};

class OpticalPath final {
public:
    [[nodiscard]] static Result<OpticalPath, OpticalPathError> create(
        OpticalPathId id,
        std::vector<OpticalPathStep> steps);

    [[nodiscard]] const OpticalPathId& id() const noexcept;
    [[nodiscard]] const std::vector<OpticalPathStep>& steps() const noexcept;

private:
    OpticalPath(OpticalPathId id, std::vector<OpticalPathStep> steps);

    OpticalPathId id_;
    std::vector<OpticalPathStep> steps_;
};

enum class EquipmentDefinitionError {
    EmptyComponentSet,
    DuplicateComponentId,
    DuplicateObjectiveSpecification,
    ObjectiveReferencesUnknownComponent,
    ObjectiveComponentWrongKind,
    DuplicateCameraSpecification,
    CameraReferencesUnknownComponent,
    CameraComponentWrongKind,
    DuplicateCondenserSpecification,
    CondenserReferencesUnknownComponent,
    CondenserComponentWrongKind,
    DuplicateIlluminationSpecification,
    IlluminationReferencesUnknownComponent,
    IlluminationComponentWrongKind,
    DuplicatePortId,
    DuplicateConnectionId,
    UnknownConnectionFromPort,
    UnknownConnectionToPort,
    ConnectionPortDirectionMismatch,
    PortAlreadyConnected,
    ConnectionWithinSameComponent,
    DuplicateOpticalPathId,
    UnknownPathComponent,
    UnknownPathEntryPort,
    UnknownPathExitPort,
    PathEntryPortWrongOwner,
    PathExitPortWrongOwner,
    PathEntryPortDirectionMismatch,
    PathExitPortDirectionMismatch,
    MissingPathEntryPort,
    MissingPathExitPort,
    MissingRouteConnection
};

// Immutable definition of the equipment physically installed in one
// microscope. Connections describe physical adjacency; paths describe
// ordered traversal through components. Shared components are stored once
// and referenced by multiple paths.
class MicroscopeEquipmentDefinition final {
public:
    [[nodiscard]] static Result<
        MicroscopeEquipmentDefinition,
        EquipmentDefinitionError> create(
            MicroscopeId microscopeId,
            std::vector<EquipmentComponent> components,
            std::vector<OpticalConnection> connections,
            std::vector<OpticalPath> opticalPaths,
            std::vector<ObjectiveSpecification> objectiveSpecifications = {},
            std::vector<CameraSpecification> cameraSpecifications = {},
            std::vector<CondenserSpecification> condenserSpecifications = {},
            std::vector<IlluminationSpecification> illuminationSpecifications = {});

    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponent>& components() const noexcept;
    [[nodiscard]] const std::vector<OpticalConnection>& connections() const noexcept;
    [[nodiscard]] const std::vector<OpticalPath>& opticalPaths() const noexcept;
    [[nodiscard]] const std::vector<ObjectiveSpecification>&
    objectiveSpecifications() const noexcept;
    [[nodiscard]] const std::vector<CameraSpecification>&
    cameraSpecifications() const noexcept;
    [[nodiscard]] const std::vector<CondenserSpecification>&
    condenserSpecifications() const noexcept;
    [[nodiscard]] const std::vector<IlluminationSpecification>&
    illuminationSpecifications() const noexcept;

private:
    MicroscopeEquipmentDefinition(
        MicroscopeId microscopeId,
        std::vector<EquipmentComponent> components,
        std::vector<OpticalConnection> connections,
        std::vector<OpticalPath> opticalPaths,
        std::vector<ObjectiveSpecification> objectiveSpecifications,
        std::vector<CameraSpecification> cameraSpecifications,
        std::vector<CondenserSpecification> condenserSpecifications,
        std::vector<IlluminationSpecification> illuminationSpecifications);

    MicroscopeId microscopeId_;
    std::vector<EquipmentComponent> components_;
    std::vector<OpticalConnection> connections_;
    std::vector<OpticalPath> opticalPaths_;
    std::vector<ObjectiveSpecification> objectiveSpecifications_;
    std::vector<CameraSpecification> cameraSpecifications_;
    std::vector<CondenserSpecification> condenserSpecifications_;
    std::vector<IlluminationSpecification> illuminationSpecifications_;
};

} // namespace microscope::core
