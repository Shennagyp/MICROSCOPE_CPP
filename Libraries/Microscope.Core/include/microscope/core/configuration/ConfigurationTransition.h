#pragma once

#include "microscope/core/configuration/Configuration.h"
#include "microscope/core/ids/Ids.h"

#include <optional>
#include <variant>
#include <vector>

namespace microscope::core {

// Typed desired-state requirements. They deliberately describe no SDK command,
// control transport, motorization assumption or device-specific operation.
struct AcquisitionMustBeInactive final {};
struct OpticalPathSetMustMatch final {
    std::vector<OpticalPathId> pathIds;
};
struct ObjectiveSelectionMustMatch final {
    EquipmentComponentId objectiveComponentId;
};
struct CondenserSelectionMustMatch final {
    EquipmentComponentId condenserComponentId;
};
struct AdditionalOpticalComponentSelectionMustMatch final {
    EquipmentComponentId componentId;
};
struct CameraOutputModeMustMatch final {
    EquipmentComponentId cameraComponentId;
    CameraModeId cameraModeId;
};
struct IlluminationChannelMustMatch final {
    EquipmentComponentId illuminationComponentId;
    IlluminationChannelId channelId;
};
struct CondenserApertureMustMatch final {
    EquipmentComponentId condenserComponentId;
    NonNegativeNumericalAperture target;
};
struct IlluminationIntensityMustMatch final {
    EquipmentComponentId illuminationComponentId;
    IlluminationChannelId channelId;
    IlluminationIntensitySetting target;
};
struct InitialExposurePresetMustBeConsidered final {
    EquipmentComponentId cameraComponentId;
    ExposurePreset preset;
    ExposurePolicy policy;
};
struct DesiredConfigurationMustBeVerified final {};

using ConfigurationTransitionRequirement = std::variant<
    AcquisitionMustBeInactive,
    OpticalPathSetMustMatch,
    ObjectiveSelectionMustMatch,
    CondenserSelectionMustMatch,
    AdditionalOpticalComponentSelectionMustMatch,
    CameraOutputModeMustMatch,
    IlluminationChannelMustMatch,
    CondenserApertureMustMatch,
    IlluminationIntensityMustMatch,
    InitialExposurePresetMustBeConsidered,
    DesiredConfigurationMustBeVerified>;

enum class ConfigurationTransitionStepError {
    EmptyOpticalPathSet,
    DuplicateOpticalPath
};

class ConfigurationTransitionStep final {
public:
    [[nodiscard]] static Result<
        ConfigurationTransitionStep,
        ConfigurationTransitionStepError> create(
            ConfigurationTransitionStepId id,
            ConfigurationTransitionRequirement requirement);

    [[nodiscard]] const ConfigurationTransitionStepId& id() const noexcept;
    [[nodiscard]] const ConfigurationTransitionRequirement&
    requirement() const noexcept;

private:
    ConfigurationTransitionStep(
        ConfigurationTransitionStepId id,
        ConfigurationTransitionRequirement requirement);

    ConfigurationTransitionStepId id_;
    ConfigurationTransitionRequirement requirement_;
};

enum class ConfigurationTransitionDependencyError {
    SelfDependency
};

// Edge direction is prerequisite -> dependent. The graph expresses ordering
// constraints; execution layers expose requirements that may be considered
// concurrently by a later runtime.
class ConfigurationTransitionDependency final {
public:
    [[nodiscard]] static Result<
        ConfigurationTransitionDependency,
        ConfigurationTransitionDependencyError> create(
            ConfigurationTransitionStepId prerequisiteStepId,
            ConfigurationTransitionStepId dependentStepId);

    [[nodiscard]] const ConfigurationTransitionStepId&
    prerequisiteStepId() const noexcept;
    [[nodiscard]] const ConfigurationTransitionStepId&
    dependentStepId() const noexcept;

private:
    ConfigurationTransitionDependency(
        ConfigurationTransitionStepId prerequisiteStepId,
        ConfigurationTransitionStepId dependentStepId);

    ConfigurationTransitionStepId prerequisiteStepId_;
    ConfigurationTransitionStepId dependentStepId_;
};

enum class ConfigurationTransitionPlanError {
    EmptyStepSet,
    DuplicateStepId,
    MissingAcquisitionPrecondition,
    DuplicateAcquisitionPrecondition,
    MissingFinalVerification,
    DuplicateFinalVerification,
    UnknownPrerequisiteStep,
    UnknownDependentStep,
    SelfDependency,
    DuplicateDependency,
    CyclicDependencies,
    AcquisitionPreconditionMustPrecedeEveryStep,
    FinalVerificationMustFollowEveryStep
};

// The plan is bound to a resolved microscope target. It contains requirements,
// not commands, and is intentionally conservative: it does not diff against
// observations or infer that unverified state already matches the target.
class ConfigurationTransitionPlan final {
public:
    [[nodiscard]] static Result<
        ConfigurationTransitionPlan,
        ConfigurationTransitionPlanError> create(
            ResolvedConfiguration desiredConfiguration,
            std::vector<ConfigurationTransitionStep> steps,
            std::vector<ConfigurationTransitionDependency> dependencies);

    [[nodiscard]] const ResolvedConfiguration&
    desiredConfiguration() const noexcept;
    [[nodiscard]] const std::vector<ConfigurationTransitionStep>&
    steps() const noexcept;
    [[nodiscard]] const std::vector<ConfigurationTransitionDependency>&
    dependencies() const noexcept;
    [[nodiscard]] const std::vector<std::vector<ConfigurationTransitionStepId>>&
    executionLayers() const noexcept;

private:
    ConfigurationTransitionPlan(
        ResolvedConfiguration desiredConfiguration,
        std::vector<ConfigurationTransitionStep> steps,
        std::vector<ConfigurationTransitionDependency> dependencies,
        std::vector<std::vector<ConfigurationTransitionStepId>> executionLayers);

    ResolvedConfiguration desiredConfiguration_;
    std::vector<ConfigurationTransitionStep> steps_;
    std::vector<ConfigurationTransitionDependency> dependencies_;
    std::vector<std::vector<ConfigurationTransitionStepId>> executionLayers_;
};

enum class ConfigurationTransitionPlannerError {
    InvalidGeneratedStepIdentity,
    InvalidGeneratedStep,
    InvalidGeneratedDependency,
    InvalidGeneratedPlan
};

class ConfigurationTransitionPlanner final {
public:
    [[nodiscard]] static Result<
        ConfigurationTransitionPlan,
        ConfigurationTransitionPlannerError> plan(
            const ResolvedConfiguration& desiredConfiguration);
};

} // namespace microscope::core
