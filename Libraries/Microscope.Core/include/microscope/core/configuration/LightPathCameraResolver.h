#pragma once

#include "microscope/core/configuration/CameraSuitability.h"
#include "microscope/core/configuration/OperationMode.h"
#include "microscope/core/equipment/OpticalTopology.h"

#include <optional>
#include <vector>

namespace microscope::core {

enum class LightPathCameraResolutionStatus {
    Resolved,
    SelectionRequired,
    InputRequired,
    Unsupported
};

struct LightPathCameraCandidate {
    EquipmentComponentId cameraComponentId;
    std::vector<CameraModeId> availableCameraModeIds;
    std::optional<CameraSuitabilityReport> suitability;
    bool recommended = false;
};

struct ResolvedLightPathCamera {
    LightPathId lightPathId;
    EquipmentComponentId cameraComponentId;
    CameraModeId cameraModeId;
};

// Result for one selected LightPath and OperationMode. Candidate identities
// are derived exclusively from Imaging route terminal devices. A manual camera
// choice is accepted only if it is one of those endpoints.
class LightPathCameraResolution final {
public:
    [[nodiscard]] LightPathCameraResolutionStatus status() const noexcept;
    [[nodiscard]] const std::optional<ResolvedLightPathCamera>&
    selected() const noexcept;
    [[nodiscard]] const std::vector<LightPathCameraCandidate>&
    candidates() const noexcept;
    [[nodiscard]] const Diagnostics& diagnostics() const noexcept;
    [[nodiscard]] bool isResolved() const noexcept;

private:
    friend class LightPathCameraResolver;

    LightPathCameraResolution(
        LightPathCameraResolutionStatus status,
        std::optional<ResolvedLightPathCamera> selected,
        std::vector<LightPathCameraCandidate> candidates,
        Diagnostics diagnostics);

    LightPathCameraResolutionStatus status_;
    std::optional<ResolvedLightPathCamera> selected_;
    std::vector<LightPathCameraCandidate> candidates_;
    Diagnostics diagnostics_;
};

class LightPathCameraResolver final {
public:
    // cameraModeId may be omitted only when the selected camera has one
    // available output mode. Manual camera selection never expands eligibility
    // beyond the camera endpoints derived from the selected LightPath.
    //
    // Fluorescence preferences affect recommendation and produce warnings only.
    // They never make a physically connected, specified camera ineligible.
    [[nodiscard]] static LightPathCameraResolution resolve(
        const MicroscopeEquipmentDefinition& equipment,
        const OperationMode& operationMode,
        const LightPathId& lightPathId,
        std::optional<EquipmentComponentId> manuallySelectedCameraId =
            std::nullopt,
        std::optional<CameraModeId> cameraModeId = std::nullopt,
        std::optional<FluorescenceCameraRequirements> fluorescenceRequirements =
            std::nullopt);
};

} // namespace microscope::core
