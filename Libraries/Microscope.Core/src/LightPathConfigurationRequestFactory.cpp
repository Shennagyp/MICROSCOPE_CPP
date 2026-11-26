#include "microscope/core/configuration/LightPathConfigurationRequestFactory.h"

#include <algorithm>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] const LightPath* findInstalledLightPath(
    const MicroscopeEquipmentDefinition& equipment,
    const LightPathId& lightPathId) noexcept {
    const auto& lightPaths = equipment.lightPaths();
    const auto found = std::find_if(
        lightPaths.begin(), lightPaths.end(),
        [&lightPathId](const LightPath& lightPath) {
            return lightPath.id() == lightPathId;
        });
    return found == lightPaths.end() ? nullptr : &*found;
}

[[nodiscard]] const CameraSpecification* findCameraSpecification(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) noexcept {
    const auto& specifications = equipment.cameraSpecifications();
    const auto found = std::find_if(
        specifications.begin(), specifications.end(),
        [&componentId](const CameraSpecification& specification) {
            return specification.componentId() == componentId;
        });
    return found == specifications.end() ? nullptr : &*found;
}

[[nodiscard]] bool cameraSupportsMode(
    const CameraSpecification& camera,
    const CameraModeId& cameraModeId) noexcept {
    const auto& modes = camera.outputModes();
    return std::any_of(
        modes.begin(), modes.end(),
        [&cameraModeId](const CameraOutputMode& mode) {
            return mode.id() == cameraModeId;
        });
}

} // namespace

Result<
    ConfigurationRequest,
    LightPathConfigurationRequestFactoryError>
LightPathConfigurationRequestFactory::create(
    const MicroscopeEquipmentDefinition& equipment,
    const OperationMode& operationMode,
    const LightPathId& lightPathId,
    const LightPathCameraResolution& cameraResolution,
    ConfigurationProfileId profileId,
    EquipmentComponentId objectiveComponentId,
    EquipmentComponentId condenserComponentId,
    EquipmentComponentId illuminationComponentId,
    IlluminationChannelId illuminationChannelId,
    ExposurePolicy exposurePolicy,
    std::vector<EquipmentComponentId> selectedAdditionalOpticalComponentIds) {
    if (operationMode.microscopeId() != equipment.microscopeId()) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::MicroscopeMismatch);
    }

    const auto* lightPath = findInstalledLightPath(equipment, lightPathId);
    if (lightPath == nullptr) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::LightPathNotInstalled);
    }

    if (operationMode.findLightPathSetting(lightPathId) == nullptr) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::OperationModeDoesNotContainLightPath);
    }

    if (!cameraResolution.isResolved() || !cameraResolution.selected()) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::CameraResolutionNotResolved);
    }

    const auto& selected = *cameraResolution.selected();
    if (selected.lightPathId != lightPathId) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::ResolvedCameraLightPathMismatch);
    }

    const auto cameraEndpoints = equipment.cameraEndpointIds(lightPathId);
    if (std::find(
            cameraEndpoints.begin(), cameraEndpoints.end(),
            selected.cameraComponentId) == cameraEndpoints.end()) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::ResolvedCameraNotOnLightPath);
    }

    const auto* camera = findCameraSpecification(
        equipment, selected.cameraComponentId);
    if (camera == nullptr) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::ResolvedCameraSpecificationMissing);
    }

    if (!cameraSupportsMode(*camera, selected.cameraModeId)) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::ResolvedCameraModeUnsupported);
    }

    auto request = ConfigurationRequest::create(
        std::move(profileId),
        operationMode.contrastMethod(),
        std::move(objectiveComponentId),
        std::move(condenserComponentId),
        lightPath->opticalPathIds(),
        selected.cameraComponentId,
        selected.cameraModeId,
        std::move(illuminationComponentId),
        std::move(illuminationChannelId),
        exposurePolicy,
        std::move(selectedAdditionalOpticalComponentIds));

    if (!request) {
        return Result<
            ConfigurationRequest,
            LightPathConfigurationRequestFactoryError>::failure(
                LightPathConfigurationRequestFactoryError::LegacyRequestRejected);
    }

    return Result<
        ConfigurationRequest,
        LightPathConfigurationRequestFactoryError>::success(
            std::move(request).value());
}

} // namespace microscope::core
