#include "microscope/core/configuration/ConfigurationTransition.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace microscope::core {
namespace {

template <typename TValue>
[[nodiscard]] bool hasDuplicates(const std::vector<TValue>& values) {
    std::set<TValue> unique;
    for (const auto& value : values) {
        if (!unique.insert(value).second) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool isAcquisitionGate(
    const ConfigurationTransitionRequirement& requirement) noexcept {
    return std::holds_alternative<AcquisitionMustBeInactive>(requirement);
}

[[nodiscard]] bool isFinalVerification(
    const ConfigurationTransitionRequirement& requirement) noexcept {
    return std::holds_alternative<DesiredConfigurationMustBeVerified>(requirement);
}

} // namespace

ConfigurationTransitionStep::ConfigurationTransitionStep(
    ConfigurationTransitionStepId id,
    ConfigurationTransitionRequirement requirement)
    : id_(std::move(id)),
      requirement_(std::move(requirement)) {}

Result<
    ConfigurationTransitionStep,
    ConfigurationTransitionStepError> ConfigurationTransitionStep::create(
        ConfigurationTransitionStepId id,
        ConfigurationTransitionRequirement requirement) {
    if (const auto* paths = std::get_if<OpticalPathSetMustMatch>(&requirement)) {
        if (paths->pathIds.empty()) {
            return Result<
                ConfigurationTransitionStep,
                ConfigurationTransitionStepError>::failure(
                    ConfigurationTransitionStepError::EmptyOpticalPathSet);
        }
        if (hasDuplicates(paths->pathIds)) {
            return Result<
                ConfigurationTransitionStep,
                ConfigurationTransitionStepError>::failure(
                    ConfigurationTransitionStepError::DuplicateOpticalPath);
        }
    }

    return Result<
        ConfigurationTransitionStep,
        ConfigurationTransitionStepError>::success(
            ConfigurationTransitionStep(std::move(id), std::move(requirement)));
}

const ConfigurationTransitionStepId&
ConfigurationTransitionStep::id() const noexcept {
    return id_;
}

const ConfigurationTransitionRequirement&
ConfigurationTransitionStep::requirement() const noexcept {
    return requirement_;
}

ConfigurationTransitionDependency::ConfigurationTransitionDependency(
    ConfigurationTransitionStepId prerequisiteStepId,
    ConfigurationTransitionStepId dependentStepId)
    : prerequisiteStepId_(std::move(prerequisiteStepId)),
      dependentStepId_(std::move(dependentStepId)) {}

Result<
    ConfigurationTransitionDependency,
    ConfigurationTransitionDependencyError>
ConfigurationTransitionDependency::create(
    ConfigurationTransitionStepId prerequisiteStepId,
    ConfigurationTransitionStepId dependentStepId) {
    if (prerequisiteStepId == dependentStepId) {
        return Result<
            ConfigurationTransitionDependency,
            ConfigurationTransitionDependencyError>::failure(
                ConfigurationTransitionDependencyError::SelfDependency);
    }

    return Result<
        ConfigurationTransitionDependency,
        ConfigurationTransitionDependencyError>::success(
            ConfigurationTransitionDependency(
                std::move(prerequisiteStepId),
                std::move(dependentStepId)));
}

const ConfigurationTransitionStepId&
ConfigurationTransitionDependency::prerequisiteStepId() const noexcept {
    return prerequisiteStepId_;
}

const ConfigurationTransitionStepId&
ConfigurationTransitionDependency::dependentStepId() const noexcept {
    return dependentStepId_;
}

ConfigurationTransitionPlan::ConfigurationTransitionPlan(
    ResolvedConfiguration desiredConfiguration,
    std::vector<ConfigurationTransitionStep> steps,
    std::vector<ConfigurationTransitionDependency> dependencies,
    std::vector<std::vector<ConfigurationTransitionStepId>> executionLayers)
    : desiredConfiguration_(std::move(desiredConfiguration)),
      steps_(std::move(steps)),
      dependencies_(std::move(dependencies)),
      executionLayers_(std::move(executionLayers)) {}

Result<
    ConfigurationTransitionPlan,
    ConfigurationTransitionPlanError> ConfigurationTransitionPlan::create(
        ResolvedConfiguration desiredConfiguration,
        std::vector<ConfigurationTransitionStep> steps,
        std::vector<ConfigurationTransitionDependency> dependencies) {
    if (steps.empty()) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlanError>::failure(
                ConfigurationTransitionPlanError::EmptyStepSet);
    }

    std::map<ConfigurationTransitionStepId, const ConfigurationTransitionStep*>
        stepById;
    std::optional<ConfigurationTransitionStepId> acquisitionGateId;
    std::optional<ConfigurationTransitionStepId> finalVerificationId;

    for (const auto& step : steps) {
        if (!stepById.emplace(step.id(), &step).second) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::DuplicateStepId);
        }

        if (isAcquisitionGate(step.requirement())) {
            if (acquisitionGateId) {
                return Result<
                    ConfigurationTransitionPlan,
                    ConfigurationTransitionPlanError>::failure(
                        ConfigurationTransitionPlanError::DuplicateAcquisitionPrecondition);
            }
            acquisitionGateId = step.id();
        }

        if (isFinalVerification(step.requirement())) {
            if (finalVerificationId) {
                return Result<
                    ConfigurationTransitionPlan,
                    ConfigurationTransitionPlanError>::failure(
                        ConfigurationTransitionPlanError::DuplicateFinalVerification);
            }
            finalVerificationId = step.id();
        }
    }

    if (!acquisitionGateId) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlanError>::failure(
                ConfigurationTransitionPlanError::MissingAcquisitionPrecondition);
    }
    if (!finalVerificationId) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlanError>::failure(
                ConfigurationTransitionPlanError::MissingFinalVerification);
    }

    using StepIdSet = std::set<ConfigurationTransitionStepId>;
    std::map<ConfigurationTransitionStepId, StepIdSet> dependents;
    std::map<ConfigurationTransitionStepId, std::size_t> indegree;
    std::set<std::pair<
        ConfigurationTransitionStepId,
        ConfigurationTransitionStepId>> uniqueDependencies;

    for (const auto& step : steps) {
        indegree.emplace(step.id(), 0);
        dependents.emplace(step.id(), StepIdSet{});
    }

    for (const auto& dependency : dependencies) {
        if (stepById.find(dependency.prerequisiteStepId()) == stepById.end()) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::UnknownPrerequisiteStep);
        }
        if (stepById.find(dependency.dependentStepId()) == stepById.end()) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::UnknownDependentStep);
        }
        if (dependency.prerequisiteStepId() ==
            dependency.dependentStepId()) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::SelfDependency);
        }
        if (dependency.dependentStepId() == *acquisitionGateId) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::AcquisitionPreconditionMustPrecedeEveryStep);
        }
        if (dependency.prerequisiteStepId() == *finalVerificationId) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::FinalVerificationMustFollowEveryStep);
        }

        const auto edge = std::make_pair(
            dependency.prerequisiteStepId(),
            dependency.dependentStepId());
        if (!uniqueDependencies.insert(edge).second) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::DuplicateDependency);
        }

        dependents[dependency.prerequisiteStepId()].insert(
            dependency.dependentStepId());
        ++indegree[dependency.dependentStepId()];
    }

    // Kahn's algorithm produces deterministic topological layers. Each layer
    // contains requirements that have no dependency on one another.
    StepIdSet ready;
    for (const auto& entry : indegree) {
        if (entry.second == 0) {
            ready.insert(entry.first);
        }
    }

    std::vector<std::vector<ConfigurationTransitionStepId>> layers;
    std::size_t processedCount = 0;
    while (!ready.empty()) {
        const StepIdSet currentLayer = ready;
        ready.clear();

        std::vector<ConfigurationTransitionStepId> layer;
        layer.reserve(currentLayer.size());
        for (const auto& stepId : currentLayer) {
            layer.push_back(stepId);
            ++processedCount;

            for (const auto& dependentId : dependents[stepId]) {
                auto& remaining = indegree[dependentId];
                --remaining;
                if (remaining == 0) {
                    ready.insert(dependentId);
                }
            }
        }
        layers.push_back(std::move(layer));
    }

    if (processedCount != steps.size()) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlanError>::failure(
                ConfigurationTransitionPlanError::CyclicDependencies);
    }

    // The inactive-acquisition gate must directly precede every requirement;
    // final verification must directly follow every other requirement.
    const auto& gateDependents = dependents[*acquisitionGateId];
    const auto& verificationDependents = dependents[*finalVerificationId];
    if (!verificationDependents.empty()) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlanError>::failure(
                ConfigurationTransitionPlanError::FinalVerificationMustFollowEveryStep);
    }

    for (const auto& step : steps) {
        if (step.id() != *acquisitionGateId &&
            gateDependents.find(step.id()) == gateDependents.end()) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::AcquisitionPreconditionMustPrecedeEveryStep);
        }
        if (step.id() != *finalVerificationId &&
            dependents[step.id()].find(*finalVerificationId) ==
                dependents[step.id()].end()) {
            return Result<
                ConfigurationTransitionPlan,
                ConfigurationTransitionPlanError>::failure(
                    ConfigurationTransitionPlanError::FinalVerificationMustFollowEveryStep);
        }
    }

    return Result<
        ConfigurationTransitionPlan,
        ConfigurationTransitionPlanError>::success(
            ConfigurationTransitionPlan(
                std::move(desiredConfiguration),
                std::move(steps),
                std::move(dependencies),
                std::move(layers)));
}

const ResolvedConfiguration&
ConfigurationTransitionPlan::desiredConfiguration() const noexcept {
    return desiredConfiguration_;
}

const std::vector<ConfigurationTransitionStep>&
ConfigurationTransitionPlan::steps() const noexcept {
    return steps_;
}

const std::vector<ConfigurationTransitionDependency>&
ConfigurationTransitionPlan::dependencies() const noexcept {
    return dependencies_;
}

const std::vector<std::vector<ConfigurationTransitionStepId>>&
ConfigurationTransitionPlan::executionLayers() const noexcept {
    return executionLayers_;
}

Result<
    ConfigurationTransitionPlan,
    ConfigurationTransitionPlannerError> ConfigurationTransitionPlanner::plan(
        const ResolvedConfiguration& desiredConfiguration) {
    std::vector<ConfigurationTransitionStep> steps;
    std::vector<ConfigurationTransitionDependency> dependencies;
    std::optional<ConfigurationTransitionPlannerError> buildError;

    const auto appendStep = [&steps, &buildError](
        const std::string& idValue,
        ConfigurationTransitionRequirement requirement)
        -> std::optional<ConfigurationTransitionStepId> {
        auto id = ConfigurationTransitionStepId::create(idValue);
        if (!id) {
            buildError =
                ConfigurationTransitionPlannerError::InvalidGeneratedStepIdentity;
            return std::nullopt;
        }

        const auto stepId = id.value();
        auto step = ConfigurationTransitionStep::create(
            stepId, std::move(requirement));
        if (!step) {
            buildError =
                ConfigurationTransitionPlannerError::InvalidGeneratedStep;
            return std::nullopt;
        }

        steps.push_back(std::move(step).value());
        return stepId;
    };

    const auto appendDependency = [&dependencies, &buildError](
        const ConfigurationTransitionStepId& prerequisite,
        const ConfigurationTransitionStepId& dependent) {
        auto dependency =
            ConfigurationTransitionDependency::create(prerequisite, dependent);
        if (!dependency) {
            buildError =
                ConfigurationTransitionPlannerError::InvalidGeneratedDependency;
            return;
        }
        dependencies.push_back(std::move(dependency).value());
    };

    const auto& request = desiredConfiguration.request();
    const auto acquisitionGate = appendStep(
        "configuration.acquisition.inactive",
        AcquisitionMustBeInactive{});
    const auto pathSelection = appendStep(
        "configuration.optical-paths",
        OpticalPathSetMustMatch{request.opticalPathIds()});
    const auto objectiveSelection = appendStep(
        "configuration.objective",
        ObjectiveSelectionMustMatch{request.objectiveComponentId()});
    const auto condenserSelection = appendStep(
        "configuration.condenser",
        CondenserSelectionMustMatch{request.condenserComponentId()});

    std::vector<ConfigurationTransitionStepId> additionalOpticalSelections;
    for (const auto& componentId :
         request.selectedAdditionalOpticalComponentIds()) {
        const auto additional = appendStep(
            "configuration.optical-component." + componentId.value(),
            AdditionalOpticalComponentSelectionMustMatch{componentId});
        if (additional) {
            additionalOpticalSelections.push_back(*additional);
        }
    }

    const auto cameraModeSelection = appendStep(
        "configuration.camera-mode",
        CameraOutputModeMustMatch{
            request.cameraComponentId(),
            request.cameraModeId()});
    const auto illuminationChannelSelection = appendStep(
        "configuration.illumination-channel",
        IlluminationChannelMustMatch{
            request.illuminationComponentId(),
            request.illuminationChannelId()});
    const auto exposurePolicy = appendStep(
        "configuration.exposure-policy",
        ExposurePolicyMustBeHonored{
            request.cameraComponentId(),
            request.exposurePolicy()});

    std::optional<ConfigurationTransitionStepId> apertureTarget;
    if (desiredConfiguration.condenserApertureTarget()) {
        apertureTarget = appendStep(
            "configuration.condenser-aperture",
            CondenserApertureMustMatch{
                request.condenserComponentId(),
                *desiredConfiguration.condenserApertureTarget()});
    }

    std::optional<ConfigurationTransitionStepId> intensityTarget;
    if (desiredConfiguration.illuminationIntensityTarget()) {
        intensityTarget = appendStep(
            "configuration.illumination-intensity",
            IlluminationIntensityMustMatch{
                request.illuminationComponentId(),
                request.illuminationChannelId(),
                *desiredConfiguration.illuminationIntensityTarget()});
    }

    std::optional<ConfigurationTransitionStepId> exposurePreset;
    if (desiredConfiguration.exposurePreset()) {
        exposurePreset = appendStep(
            "configuration.initial-exposure-preset",
            InitialExposurePresetMustBeConsidered{
                request.cameraComponentId(),
                *desiredConfiguration.exposurePreset()});
    }

    const auto finalVerification = appendStep(
        "configuration.final-verification",
        DesiredConfigurationMustBeVerified{});

    if (buildError) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlannerError>::failure(*buildError);
    }
    if (!acquisitionGate || !pathSelection || !objectiveSelection ||
        !condenserSelection || !cameraModeSelection ||
        !illuminationChannelSelection || !exposurePolicy || !finalVerification) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlannerError>::failure(
                ConfigurationTransitionPlannerError::InvalidGeneratedPlan);
    }

    for (const auto& step : steps) {
        if (step.id() != *acquisitionGate &&
            step.id() != *finalVerification) {
            appendDependency(*acquisitionGate, step.id());
        }
    }

    appendDependency(*pathSelection, *cameraModeSelection);
    appendDependency(*objectiveSelection, *cameraModeSelection);
    appendDependency(*pathSelection, *illuminationChannelSelection);
    appendDependency(*objectiveSelection, *illuminationChannelSelection);
    appendDependency(*cameraModeSelection, *exposurePolicy);

    for (const auto& additional : additionalOpticalSelections) {
        appendDependency(additional, *cameraModeSelection);
        appendDependency(additional, *illuminationChannelSelection);
    }

    if (apertureTarget) {
        appendDependency(*condenserSelection, *apertureTarget);
    }
    if (intensityTarget) {
        appendDependency(*illuminationChannelSelection, *intensityTarget);
    }
    if (exposurePreset) {
        appendDependency(*exposurePolicy, *exposurePreset);
    }

    if (buildError) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlannerError>::failure(*buildError);
    }

    for (const auto& step : steps) {
        if (step.id() != *finalVerification) {
            appendDependency(step.id(), *finalVerification);
        }
    }

    if (buildError) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlannerError>::failure(*buildError);
    }

    auto plan = ConfigurationTransitionPlan::create(
        desiredConfiguration,
        std::move(steps),
        std::move(dependencies));
    if (!plan) {
        return Result<
            ConfigurationTransitionPlan,
            ConfigurationTransitionPlannerError>::failure(
                ConfigurationTransitionPlannerError::InvalidGeneratedPlan);
    }

    return Result<
        ConfigurationTransitionPlan,
        ConfigurationTransitionPlannerError>::success(
            std::move(plan).value());
}

} // namespace microscope::core
