#pragma once

#include "microscope/core/configuration/Configuration.h"
#include "microscope/core/configuration/LightPathCameraResolver.h"
#include "microscope/core/configuration/OperationMode.h"

namespace microscope::core {

enum class LightPathConfigurationRequestFactoryError {
    MicroscopeMismatch,
    LightPathNotInstalled,
    OperationModeDoesNotContainLightPath,
    CameraResolutionNotResolved,
    ResolvedCameraOperationModeMismatch,
    ResolvedCameraLightPathMismatch,
    ResolvedCameraNotOnLightPath,
    ResolvedCameraSpecificationMissing,
    ResolvedCameraModeUnsupported,
    LegacyRequestRejected
};

// Migration bridge from the LightPath-first model to the established
// ConfigurationRequest contract. It derives the complete OpticalPathId set
// and camera/mode identity from the selected LightPath resolution; callers
// cannot supply an unrelated camera or path list.
class LightPathConfigurationRequestFactory final {
public:
    [[nodiscard]] static Result<
        ConfigurationRequest,
        LightPathConfigurationRequestFactoryError> create(
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
            std::vector<EquipmentComponentId> selectedAdditionalOpticalComponentIds = {});
};

} // namespace microscope::core
