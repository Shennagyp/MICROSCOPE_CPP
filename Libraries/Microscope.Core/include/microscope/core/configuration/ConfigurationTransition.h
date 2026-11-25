#pragma once

#include "microscope/core/configuration/Configuration.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

#include <optional>
#include <variant>
#include <vector>

namespace microscope::core {

// Requirements are desired-state facts, not adapter commands. The Phase 4
// runtime interprets them through installed adapter ports.
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

struct ExposurePolicyMustBeHonored final {
    EquipmentComponentId cameraComponentId;
    ExposurePolicy policy;
};

struct InitialExposurePresetMustBeConsidered final {
    EquipmentComponentId cameraComponentId;
    ExposurePreset preset;
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
    ExposurePolicyMustBeHonored,
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

// Edge direction is prerequisite -> dependent. Independent requirements may
// share an execution layer; the plan is not a universal linear script.
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

// Immutable, microscope-scoped dependency DAG. It conservatively describes
// all desired requirements; it does not diff unverified observations or emit
// device commands. Runtime reconciliation belongs to Phase 4.
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
