#include "microscope/core/configuration/Configuration.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool isKnownContrastMethod(ContrastMethod value) noexcept {
    switch (value) {
    case ContrastMethod::Brightfield:
    case ContrastMethod::PhaseContrast:
    case ContrastMethod::DifferentialInterferenceContrast:
    case ContrastMethod::PolarizedLight:
    case ContrastMethod::Fluorescence:
    case ContrastMethod::Darkfield:
    case ContrastMethod::Other:
        return true;
    case ContrastMethod::Unknown:
        return false;
    }
    return false;
}

[[nodiscard]] bool isKnownOpticalElementFunction(
    OpticalElementFunction value) noexcept {
    switch (value) {
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
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownExposurePolicy(ExposurePolicy value) noexcept {
    switch (value) {
    case ExposurePolicy::Manual:
    case ExposurePolicy::Automatic:
    case ExposurePolicy::ConstrainedAutomatic:
        return true;
    case ExposurePolicy::Unknown:
        return false;
    }
    return false;
}

template <typename TValue>
[[nodiscard]] bool contains(
    const std::vector<TValue>& values,
    const TValue& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool containsOpticalPathComponent(
    const std::vector<const OpticalPath*>& paths,
    const EquipmentComponentId& componentId) {
    return std::any_of(
        paths.begin(),
        paths.end(),
        [&componentId](const OpticalPath* path) {
            return std::any_of(
                path->steps().begin(),
                path->steps().end(),
                [&componentId](const OpticalPathStep& step) {
                    return step.componentId() == componentId;
                });
        });
}

[[nodiscard]] const EquipmentComponent* findEquipmentComponent(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) {
    const auto it = std::find_if(
        equipment.components().begin(),
        equipment.components().end(),
        [&componentId](const EquipmentComponent& value) {
            return value.id() == componentId;
        });
    return it == equipment.components().end() ? nullptr : &*it;
}

[[nodiscard]] const ObjectiveSpecification* findObjective(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) {
    const auto it = std::find_if(
        equipment.objectiveSpecifications().begin(),
        equipment.objectiveSpecifications().end(),
        [&componentId](const ObjectiveSpecification& value) {
            return value.componentId() == componentId;
        });
    return it == equipment.objectiveSpecifications().end() ? nullptr : &*it;
}

[[nodiscard]] const CondenserSpecification* findCondenser(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) {
    const auto it = std::find_if(
        equipment.condenserSpecifications().begin(),
        equipment.condenserSpecifications().end(),
        [&componentId](const CondenserSpecification& value) {
            return value.componentId() == componentId;
        });
    return it == equipment.condenserSpecifications().end() ? nullptr : &*it;
}

[[nodiscard]] const CameraSpecification* findCamera(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) {
    const auto it = std::find_if(
        equipment.cameraSpecifications().begin(),
        equipment.cameraSpecifications().end(),
        [&componentId](const CameraSpecification& value) {
            return value.componentId() == componentId;
        });
    return it == equipment.cameraSpecifications().end() ? nullptr : &*it;
}

[[nodiscard]] const IlluminationSpecification* findIllumination(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) {
    const auto it = std::find_if(
        equipment.illuminationSpecifications().begin(),
        equipment.illuminationSpecifications().end(),
        [&componentId](const IlluminationSpecification& value) {
            return value.componentId() == componentId;
        });
    return it == equipment.illuminationSpecifications().end() ? nullptr : &*it;
}

[[nodiscard]] const OpticalPath* findOpticalPath(
    const MicroscopeEquipmentDefinition& equipment,
    const OpticalPathId& pathId) {
    const auto it = std::find_if(
        equipment.opticalPaths().begin(),
        equipment.opticalPaths().end(),
        [&pathId](const OpticalPath& value) {
            return value.id() == pathId;
        });
    return it == equipment.opticalPaths().end() ? nullptr : &*it;
}

[[nodiscard]] const ConfigurationProfile* findProfile(
    const ConfigurationProfileCatalog& catalog,
    const ConfigurationProfileId& profileId) {
    const auto it = std::find_if(
        catalog.profiles().begin(),
        catalog.profiles().end(),
        [&profileId](const ConfigurationProfile& value) {
            return value.id() == profileId;
        });
    return it == catalog.profiles().end() ? nullptr : &*it;
}

void appendError(
    Diagnostics& diagnostics,
    std::string code,
    std::string message,
    std::optional<std::string> entityType = std::nullopt,
    std::optional<std::string> entityId = std::nullopt) {
    auto diagnostic = Diagnostic::create(
        DiagnosticSeverity::Error,
        std::move(code),
        std::move(message),
        DiagnosticContext{
            std::move(entityType),
            std::move(entityId),
            std::nullopt});
    if (diagnostic) {
        diagnostics.push_back(std::move(diagnostic).value());
    }
}

} // namespace

OpticalFunctionRequirement::OpticalFunctionRequirement(
    OpticalElementFunction function,
    std::optional<OpticalCompatibilityId> compatibilityId,
    std::uint32_t minimumComponentCount)
    : function_(function),
      compatibilityId_(std::move(compatibilityId)),
      minimumComponentCount_(minimumComponentCount) {}

Result<
    OpticalFunctionRequirement,
    OpticalFunctionRequirementError> OpticalFunctionRequirement::create(
        OpticalElementFunction function,
        std::optional<OpticalCompatibilityId> compatibilityId,
        std::uint32_t minimumComponentCount) {
    if (!isKnownOpticalElementFunction(function)) {
        return Result<
            OpticalFunctionRequirement,
            OpticalFunctionRequirementError>::failure(
                OpticalFunctionRequirementError::InvalidFunction);
    }
    if (minimumComponentCount == 0) {
        return Result<
            OpticalFunctionRequirement,
            OpticalFunctionRequirementError>::failure(
                OpticalFunctionRequirementError::ZeroMinimumComponentCount);
    }

    return Result<
        OpticalFunctionRequirement,
        OpticalFunctionRequirementError>::success(
            OpticalFunctionRequirement(
                function,
                std::move(compatibilityId),
                minimumComponentCount));
}

OpticalElementFunction OpticalFunctionRequirement::function() const noexcept {
    return function_;
}

const std::optional<OpticalCompatibilityId>&
OpticalFunctionRequirement::compatibilityId() const noexcept {
    return compatibilityId_;
}

std::uint32_t
OpticalFunctionRequirement::minimumComponentCount() const noexcept {
    return minimumComponentCount_;
}

ConfigurationProfile::ConfigurationProfile(
    ConfigurationProfileId id,
    ContrastMethod contrastMethod,
    std::vector<EquipmentComponentId> compatibleObjectiveIds,
    std::vector<EquipmentComponentId> compatibleCondenserIds,
    std::vector<OpticalPathId> compatibleOpticalPathIds,
    std::vector<EquipmentComponentId> compatibleCameraIds,
    std::vector<EquipmentComponentId> compatibleIlluminationIds,
    std::vector<EquipmentComponentId> requiredPathComponentIds,
    std::vector<OpticalFunctionRequirement> requiredOpticalFunctions)
    : id_(std::move(id)),
      contrastMethod_(contrastMethod),
      compatibleObjectiveIds_(std::move(compatibleObjectiveIds)),
      compatibleCondenserIds_(std::move(compatibleCondenserIds)),
      compatibleOpticalPathIds_(std::move(compatibleOpticalPathIds)),
      compatibleCameraIds_(std::move(compatibleCameraIds)),
      compatibleIlluminationIds_(std::move(compatibleIlluminationIds)),
      requiredPathComponentIds_(std::move(requiredPathComponentIds)),
      requiredOpticalFunctions_(std::move(requiredOpticalFunctions)) {}

Result<
    ConfigurationProfile,
    ConfigurationProfileError> ConfigurationProfile::create(
        ConfigurationProfileId id,
        ContrastMethod contrastMethod,
        std::vector<EquipmentComponentId> compatibleObjectiveIds,
        std::vector<EquipmentComponentId> compatibleCondenserIds,
        std::vector<OpticalPathId> compatibleOpticalPathIds,
        std::vector<EquipmentComponentId> compatibleCameraIds,
        std::vector<EquipmentComponentId> compatibleIlluminationIds,
        std::vector<EquipmentComponentId> requiredPathComponentIds,
        std::vector<OpticalFunctionRequirement> requiredOpticalFunctions) {
    if (!isKnownContrastMethod(contrastMethod)) {
        return Result<
            ConfigurationProfile,
            ConfigurationProfileError>::failure(
                ConfigurationProfileError::InvalidContrastMethod);
    }

    if (compatibleObjectiveIds.empty() ||
        compatibleCondenserIds.empty() ||
        compatibleOpticalPathIds.empty() ||
        compatibleCameraIds.empty() ||
        compatibleIlluminationIds.empty()) {
        return Result<
            ConfigurationProfile,
            ConfigurationProfileError>::failure(
                ConfigurationProfileError::EmptyCompatibilitySet);
    }

    const auto hasDuplicates = [](const auto& values) {
        using TValue = typename std::decay_t<decltype(values)>::value_type;
        std::set<TValue> unique;
        for (const auto& value : values) {
            if (!unique.insert(value).second) {
                return true;
            }
        }
        return false;
    };

    if (hasDuplicates(compatibleObjectiveIds)) {
        return Result<
            ConfigurationProfile,
            ConfigurationProfileError>::failure(
                ConfigurationProfileError::DuplicateObjective);
    }
    if (hasDuplicates(compatibleCondenserIds)) {
        return Result<
            ConfigurationProfile,
            ConfigurationProfileError>::failure(
                ConfigurationProfileError::DuplicateCondenser);
    }
    if (hasDuplicates(compatibleOpticalPathIds)) {
        return Result<
            ConfigurationProfile,
            ConfigurationProfileError>::failure(
                ConfigurationProfileError::DuplicateOpticalPath);
    }
    if (hasDuplicates(compatibleCameraIds)) {
        return Result<
            ConfigurationProfile,
            ConfigurationProfileError>::failure(
                ConfigurationProfileError::DuplicateCamera);
    }
    if (hasDuplicates(compatibleIlluminationIds)) {
        return Result<
            ConfigurationProfile,
            ConfigurationProfileError>::failure(
                ConfigurationProfileError::DuplicateIllumination);
    }
    if (hasDuplicates(requiredPathComponentIds)) {
        return Result<
            ConfigurationProfile,
            ConfigurationProfileError>::failure(
                ConfigurationProfileError::DuplicateRequiredComponent);
    }

    for (std::size_t i = 0; i < requiredOpticalFunctions.size(); ++i) {
        for (std::size_t j = i + 1; j < requiredOpticalFunctions.size(); ++j) {
            if (requiredOpticalFunctions[i].function() ==
                    requiredOpticalFunctions[j].function() &&
                requiredOpticalFunctions[i].compatibilityId() ==
                    requiredOpticalFunctions[j].compatibilityId()) {
                return Result<
                    ConfigurationProfile,
                    ConfigurationProfileError>::failure(
                        ConfigurationProfileError::DuplicateOpticalFunctionRequirement);
            }
        }
    }

    return Result<
        ConfigurationProfile,
        ConfigurationProfileError>::success(
            ConfigurationProfile(
                std::move(id),
                contrastMethod,
                std::move(compatibleObjectiveIds),
                std::move(compatibleCondenserIds),
                std::move(compatibleOpticalPathIds),
                std::move(compatibleCameraIds),
                std::move(compatibleIlluminationIds),
                std::move(requiredPathComponentIds),
                std::move(requiredOpticalFunctions)));
}

const ConfigurationProfileId& ConfigurationProfile::id() const noexcept {
    return id_;
}

ContrastMethod ConfigurationProfile::contrastMethod() const noexcept {
    return contrastMethod_;
}

const std::vector<EquipmentComponentId>&
ConfigurationProfile::compatibleObjectiveIds() const noexcept {
    return compatibleObjectiveIds_;
}

const std::vector<EquipmentComponentId>&
ConfigurationProfile::compatibleCondenserIds() const noexcept {
    return compatibleCondenserIds_;
}

const std::vector<OpticalPathId>&
ConfigurationProfile::compatibleOpticalPathIds() const noexcept {
    return compatibleOpticalPathIds_;
}

const std::vector<EquipmentComponentId>&
ConfigurationProfile::compatibleCameraIds() const noexcept {
    return compatibleCameraIds_;
}

const std::vector<EquipmentComponentId>&
ConfigurationProfile::compatibleIlluminationIds() const noexcept {
    return compatibleIlluminationIds_;
}

const std::vector<EquipmentComponentId>&
ConfigurationProfile::requiredPathComponentIds() const noexcept {
    return requiredPathComponentIds_;
}

const std::vector<OpticalFunctionRequirement>&
ConfigurationProfile::requiredOpticalFunctions() const noexcept {
    return requiredOpticalFunctions_;
}

ConfigurationProfileCatalog::ConfigurationProfileCatalog(
    std::vector<ConfigurationProfile> profiles)
    : profiles_(std::move(profiles)) {}

Result<
    ConfigurationProfileCatalog,
    ConfigurationProfileCatalogError> ConfigurationProfileCatalog::create(
        std::vector<ConfigurationProfile> profiles) {
    if (profiles.empty()) {
        return Result<
            ConfigurationProfileCatalog,
            ConfigurationProfileCatalogError>::failure(
                ConfigurationProfileCatalogError::EmptyCatalog);
    }

    std::set<ConfigurationProfileId> profileIds;
    for (const auto& profile : profiles) {
        if (!profileIds.insert(profile.id()).second) {
            return Result<
                ConfigurationProfileCatalog,
                ConfigurationProfileCatalogError>::failure(
                    ConfigurationProfileCatalogError::DuplicateProfileId);
        }
    }

    return Result<
        ConfigurationProfileCatalog,
        ConfigurationProfileCatalogError>::success(
            ConfigurationProfileCatalog(std::move(profiles)));
}

const std::vector<ConfigurationProfile>&
ConfigurationProfileCatalog::profiles() const noexcept {
    return profiles_;
}

ConfigurationRequest::ConfigurationRequest(
    ConfigurationProfileId profileId,
    ContrastMethod contrastMethod,
    EquipmentComponentId objectiveComponentId,
    EquipmentComponentId condenserComponentId,
    std::vector<OpticalPathId> opticalPathIds,
    EquipmentComponentId cameraComponentId,
    CameraModeId cameraModeId,
    EquipmentComponentId illuminationComponentId,
    IlluminationChannelId illuminationChannelId,
    ExposurePolicy exposurePolicy,
    std::vector<EquipmentComponentId> selectedAdditionalOpticalComponentIds)
    : profileId_(std::move(profileId)),
      contrastMethod_(contrastMethod),
      objectiveComponentId_(std::move(objectiveComponentId)),
      condenserComponentId_(std::move(condenserComponentId)),
      opticalPathIds_(std::move(opticalPathIds)),
      cameraComponentId_(std::move(cameraComponentId)),
      cameraModeId_(std::move(cameraModeId)),
      illuminationComponentId_(std::move(illuminationComponentId)),
      illuminationChannelId_(std::move(illuminationChannelId)),
      exposurePolicy_(exposurePolicy),
      selectedAdditionalOpticalComponentIds_(
          std::move(selectedAdditionalOpticalComponentIds)) {}

Result<
    ConfigurationRequest,
    ConfigurationRequestError> ConfigurationRequest::create(
        ConfigurationProfileId profileId,
        ContrastMethod contrastMethod,
        EquipmentComponentId objectiveComponentId,
        EquipmentComponentId condenserComponentId,
        std::vector<OpticalPathId> opticalPathIds,
        EquipmentComponentId cameraComponentId,
        CameraModeId cameraModeId,
        EquipmentComponentId illuminationComponentId,
        IlluminationChannelId illuminationChannelId,
        ExposurePolicy exposurePolicy,
        std::vector<EquipmentComponentId> selectedAdditionalOpticalComponentIds) {
    if (!isKnownContrastMethod(contrastMethod)) {
        return Result<
            ConfigurationRequest,
            ConfigurationRequestError>::failure(
                ConfigurationRequestError::InvalidContrastMethod);
    }

    if (!isKnownExposurePolicy(exposurePolicy)) {
        return Result<
            ConfigurationRequest,
            ConfigurationRequestError>::failure(
                ConfigurationRequestError::InvalidExposurePolicy);
    }

    if (opticalPathIds.empty()) {
        return Result<
            ConfigurationRequest,
            ConfigurationRequestError>::failure(
                ConfigurationRequestError::EmptyOpticalPathSet);
    }

    std::set<OpticalPathId> uniqueOpticalPathIds;
    for (const auto& opticalPathId : opticalPathIds) {
        if (!uniqueOpticalPathIds.insert(opticalPathId).second) {
            return Result<
                ConfigurationRequest,
                ConfigurationRequestError>::failure(
                    ConfigurationRequestError::DuplicateOpticalPath);
        }
    }

    std::set<EquipmentComponentId> uniqueAdditionalOpticalComponentIds;
    for (const auto& componentId : selectedAdditionalOpticalComponentIds) {
        if (componentId == objectiveComponentId ||
            componentId == condenserComponentId ||
            !uniqueAdditionalOpticalComponentIds.insert(componentId).second) {
            return Result<
                ConfigurationRequest,
                ConfigurationRequestError>::failure(
                    ConfigurationRequestError::DuplicateAdditionalOpticalComponent);
        }
    }

    return Result<
        ConfigurationRequest,
        ConfigurationRequestError>::success(
            ConfigurationRequest(
                std::move(profileId),
                contrastMethod,
                std::move(objectiveComponentId),
                std::move(condenserComponentId),
                std::move(opticalPathIds),
                std::move(cameraComponentId),
                std::move(cameraModeId),
                std::move(illuminationComponentId),
                std::move(illuminationChannelId),
                exposurePolicy,
                std::move(selectedAdditionalOpticalComponentIds)));
}

const ConfigurationProfileId& ConfigurationRequest::profileId() const noexcept {
    return profileId_;
}

ContrastMethod ConfigurationRequest::contrastMethod() const noexcept {
    return contrastMethod_;
}

const EquipmentComponentId& ConfigurationRequest::objectiveComponentId() const noexcept {
    return objectiveComponentId_;
}

const EquipmentComponentId& ConfigurationRequest::condenserComponentId() const noexcept {
    return condenserComponentId_;
}

const std::vector<OpticalPathId>& ConfigurationRequest::opticalPathIds() const noexcept {
    return opticalPathIds_;
}

const std::vector<EquipmentComponentId>&
ConfigurationRequest::selectedAdditionalOpticalComponentIds() const noexcept {
    return selectedAdditionalOpticalComponentIds_;
}

const EquipmentComponentId& ConfigurationRequest::cameraComponentId() const noexcept {
    return cameraComponentId_;
}

const CameraModeId& ConfigurationRequest::cameraModeId() const noexcept {
    return cameraModeId_;
}

const EquipmentComponentId& ConfigurationRequest::illuminationComponentId() const noexcept {
    return illuminationComponentId_;
}

const IlluminationChannelId& ConfigurationRequest::illuminationChannelId() const noexcept {
    return illuminationChannelId_;
}

ExposurePolicy ConfigurationRequest::exposurePolicy() const noexcept {
    return exposurePolicy_;
}

ConfigurationResolution::ConfigurationResolution(
    std::optional<ConfigurationRequest> resolvedRequest,
    Diagnostics diagnostics)
    : resolvedRequest_(std::move(resolvedRequest)),
      diagnostics_(std::move(diagnostics)) {}

const std::optional<ConfigurationRequest>&
ConfigurationResolution::resolvedRequest() const noexcept {
    return resolvedRequest_;
}

const Diagnostics& ConfigurationResolution::diagnostics() const noexcept {
    return diagnostics_;
}

bool ConfigurationResolution::isResolved() const noexcept {
    return resolvedRequest_.has_value();
}

ConfigurationResolution ConfigurationResolver::resolve(
    const MicroscopeEquipmentDefinition& equipment,
    const ConfigurationProfileCatalog& catalog,
    const ConfigurationRequest& request) {
    Diagnostics diagnostics;
    const auto* profile = findProfile(catalog, request.profileId());
    if (profile == nullptr) {
        appendError(
            diagnostics,
            "configuration.profile.not_found",
            "The requested configuration profile is not present in the profile catalog.",
            "ConfigurationProfile",
            request.profileId().value());
        return ConfigurationResolution(std::nullopt, std::move(diagnostics));
    }

    if (profile->contrastMethod() != request.contrastMethod()) {
        appendError(
            diagnostics,
            "configuration.profile.contrast_mismatch",
            "The requested contrast method does not match the selected profile.",
            "ConfigurationProfile",
            profile->id().value());
    }

    if (!contains(profile->compatibleObjectiveIds(), request.objectiveComponentId())) {
        appendError(
            diagnostics,
            "configuration.objective.not_compatible",
            "The selected objective is not allowed by the configuration profile.",
            "EquipmentComponent",
            request.objectiveComponentId().value());
    }

    if (!contains(profile->compatibleCondenserIds(), request.condenserComponentId())) {
        appendError(
            diagnostics,
            "configuration.condenser.not_compatible",
            "The selected condenser is not allowed by the configuration profile.",
            "EquipmentComponent",
            request.condenserComponentId().value());
    }

    std::vector<const OpticalPath*> selectedPaths;
    selectedPaths.reserve(request.opticalPathIds().size());
    for (const auto& opticalPathId : request.opticalPathIds()) {
        if (!contains(profile->compatibleOpticalPathIds(), opticalPathId)) {
            appendError(
                diagnostics,
                "configuration.optical_path.not_compatible",
                "A selected optical path is not allowed by the configuration profile.",
                "OpticalPath",
                opticalPathId.value());
        }

        const auto* path = findOpticalPath(equipment, opticalPathId);
        if (path == nullptr) {
            appendError(
                diagnostics,
                "configuration.optical_path.not_installed",
                "A selected optical path is not present in the installed equipment definition.",
                "OpticalPath",
                opticalPathId.value());
        } else {
            selectedPaths.push_back(path);
        }
    }

    if (!contains(profile->compatibleCameraIds(), request.cameraComponentId())) {
        appendError(
            diagnostics,
            "configuration.camera.not_compatible",
            "The selected camera is not allowed by the configuration profile.",
            "EquipmentComponent",
            request.cameraComponentId().value());
    }

    if (!contains(
            profile->compatibleIlluminationIds(),
            request.illuminationComponentId())) {
        appendError(
            diagnostics,
            "configuration.illumination.not_compatible",
            "The selected illumination source is not allowed by the configuration profile.",
            "EquipmentComponent",
            request.illuminationComponentId().value());
    }

    const auto* objective = findObjective(equipment, request.objectiveComponentId());
    if (objective == nullptr) {
        appendError(
            diagnostics,
            "configuration.objective.specification_missing",
            "The selected objective has no installed objective specification.",
            "EquipmentComponent",
            request.objectiveComponentId().value());
    }

    const auto* condenser = findCondenser(equipment, request.condenserComponentId());
    if (condenser == nullptr) {
        appendError(
            diagnostics,
            "configuration.condenser.specification_missing",
            "The selected condenser has no installed condenser specification.",
            "EquipmentComponent",
            request.condenserComponentId().value());
    }

    const auto* camera = findCamera(equipment, request.cameraComponentId());
    if (camera == nullptr) {
        appendError(
            diagnostics,
            "configuration.camera.specification_missing",
            "The selected camera has no installed camera specification.",
            "EquipmentComponent",
            request.cameraComponentId().value());
    } else {
        const bool modeExists = std::any_of(
            camera->outputModes().begin(),
            camera->outputModes().end(),
            [&request](const CameraOutputMode& mode) {
                return mode.id() == request.cameraModeId();
            });
        if (!modeExists) {
            appendError(
                diagnostics,
                "configuration.camera.mode_not_available",
                "The selected camera output mode is not supported by the selected camera.",
                "EquipmentComponent",
                request.cameraComponentId().value());
        }
    }

    const auto* illumination = findIllumination(
        equipment, request.illuminationComponentId());
    if (illumination == nullptr) {
        appendError(
            diagnostics,
            "configuration.illumination.specification_missing",
            "The selected illumination source has no installed illumination specification.",
            "EquipmentComponent",
            request.illuminationComponentId().value());
    } else {
        const bool channelExists = std::any_of(
            illumination->channels().begin(),
            illumination->channels().end(),
            [&request](const IlluminationChannel& channel) {
                return channel.id() == request.illuminationChannelId();
            });
        if (!channelExists) {
            appendError(
                diagnostics,
                "configuration.illumination.channel_not_available",
                "The selected illumination channel is not present on the selected source.",
                "EquipmentComponent",
                request.illuminationComponentId().value());
        }
    }

    if (!selectedPaths.empty()) {
        const auto requireOnSelectedPaths = [&diagnostics, &selectedPaths](
            const EquipmentComponentId& componentId,
            const char* code,
            const char* message) {
            if (!containsOpticalPathComponent(selectedPaths, componentId)) {
                appendError(
                    diagnostics,
                    code,
                    message,
                    "EquipmentComponent",
                    componentId.value());
            }
        };

        requireOnSelectedPaths(
            request.objectiveComponentId(),
            "configuration.objective.not_on_selected_paths",
            "The selected objective is not traversed by any selected optical path.");
        requireOnSelectedPaths(
            request.condenserComponentId(),
            "configuration.condenser.not_on_selected_paths",
            "The selected condenser is not traversed by any selected optical path.");
        requireOnSelectedPaths(
            request.cameraComponentId(),
            "configuration.camera.not_on_selected_paths",
            "The selected camera is not traversed by any selected optical path.");
        requireOnSelectedPaths(
            request.illuminationComponentId(),
            "configuration.illumination.not_on_selected_paths",
            "The selected illumination source is not traversed by any selected optical path.");

        for (const auto& requiredComponent :
             profile->requiredPathComponentIds()) {
            requireOnSelectedPaths(
                requiredComponent,
                "configuration.profile.required_component_missing",
                "A required optical component is not present on the selected optical paths.");
        }

        for (const auto& additionalComponentId :
             request.selectedAdditionalOpticalComponentIds()) {
            const auto* component = findEquipmentComponent(
                equipment, additionalComponentId);
            if (component == nullptr) {
                appendError(
                    diagnostics,
                    "configuration.additional_optical_component.not_installed",
                    "A selected additional optical component is not installed.",
                    "EquipmentComponent",
                    additionalComponentId.value());
                continue;
            }
            if (component->kind() != EquipmentComponentKind::OpticalElement) {
                appendError(
                    diagnostics,
                    "configuration.additional_optical_component.wrong_kind",
                    "A selected additional optical component is not an optical element.",
                    "EquipmentComponent",
                    additionalComponentId.value());
            }
            requireOnSelectedPaths(
                additionalComponentId,
                "configuration.additional_optical_component.not_on_selected_paths",
                "A selected additional optical component is not traversed by any selected optical path.");
        }
    } else if (!request.selectedAdditionalOpticalComponentIds().empty()) {
        for (const auto& additionalComponentId :
             request.selectedAdditionalOpticalComponentIds()) {
            appendError(
                diagnostics,
                "configuration.additional_optical_component.path_unavailable",
                "An additional optical component cannot be validated because no selected optical path is installed.",
                "EquipmentComponent",
                additionalComponentId.value());
        }
    }

    std::vector<EquipmentComponentId> selectedOpticalComponentIds{
        request.objectiveComponentId(),
        request.condenserComponentId()};
    selectedOpticalComponentIds.insert(
        selectedOpticalComponentIds.end(),
        request.selectedAdditionalOpticalComponentIds().begin(),
        request.selectedAdditionalOpticalComponentIds().end());

    std::set<EquipmentComponentId> distinctSelectedOpticalComponentIds(
        selectedOpticalComponentIds.begin(),
        selectedOpticalComponentIds.end());

    for (const auto& requirement : profile->requiredOpticalFunctions()) {
        std::uint32_t matchingComponentCount = 0;
        for (const auto& selectedComponentId :
             distinctSelectedOpticalComponentIds) {
            const auto* component = findEquipmentComponent(
                equipment, selectedComponentId);
            if (component == nullptr ||
                component->kind() != EquipmentComponentKind::OpticalElement) {
                continue;
            }
            if (!selectedPaths.empty() &&
                !containsOpticalPathComponent(selectedPaths, selectedComponentId)) {
                continue;
            }

            const bool componentMatches = std::any_of(
                component->opticalFunctions().begin(),
                component->opticalFunctions().end(),
                [&requirement](const OpticalFunctionCapability& capability) {
                    return capability.function() == requirement.function() &&
                           (!requirement.compatibilityId() ||
                            capability.compatibilityId() ==
                                requirement.compatibilityId());
                });
            if (componentMatches) {
                ++matchingComponentCount;
            }
        }

        if (matchingComponentCount < requirement.minimumComponentCount()) {
            appendError(
                diagnostics,
                "configuration.optical_function.requirement_not_satisfied",
                "The selected components do not satisfy a required optical function and compatibility count.",
                "OpticalElementFunction",
                requirement.compatibilityId()
                    ? std::optional<std::string>(requirement.compatibilityId()->value())
                    : std::nullopt);
        }
    }

    const bool hasErrors = std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity() == DiagnosticSeverity::Error;
        });

    if (hasErrors) {
        return ConfigurationResolution(std::nullopt, std::move(diagnostics));
    }

    return ConfigurationResolution(request, std::move(diagnostics));
}

} // namespace microscope::core
