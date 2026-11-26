#include "microscope/core/configuration/LightPathCameraResolver.h"

#include <algorithm>
#include <string>
#include <utility>

namespace microscope::core {
namespace {

void appendDiagnostic(
    Diagnostics& diagnostics,
    DiagnosticSeverity severity,
    std::string code,
    std::string message,
    const std::string& lightPathId,
    std::optional<std::string> cameraId = std::nullopt) {
    DiagnosticContext context;
    context.entityType = cameraId ? "EquipmentComponent" : "LightPath";
    context.entityId = cameraId ? std::move(cameraId)
                                : std::optional<std::string>{lightPathId};

    auto diagnostic = Diagnostic::create(
        severity,
        std::move(code),
        std::move(message),
        std::move(context));
    if (diagnostic) {
        diagnostics.push_back(std::move(diagnostic).value());
    }
}

[[nodiscard]] const LightPath* findLightPath(
    const MicroscopeEquipmentDefinition& equipment,
    const LightPathId& lightPathId) noexcept {
    const auto& paths = equipment.lightPaths();
    const auto found = std::find_if(
        paths.begin(), paths.end(),
        [&lightPathId](const LightPath& path) {
            return path.id() == lightPathId;
        });
    return found == paths.end() ? nullptr : &*found;
}

[[nodiscard]] const CameraSpecification* findCameraSpecification(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& cameraId) noexcept {
    const auto& specifications = equipment.cameraSpecifications();
    const auto found = std::find_if(
        specifications.begin(), specifications.end(),
        [&cameraId](const CameraSpecification& specification) {
            return specification.componentId() == cameraId;
        });
    return found == specifications.end() ? nullptr : &*found;
}

[[nodiscard]] const CameraOutputMode* findCameraOutputMode(
    const CameraSpecification& camera,
    const CameraModeId& cameraModeId) noexcept {
    const auto& modes = camera.outputModes();
    const auto found = std::find_if(
        modes.begin(), modes.end(),
        [&cameraModeId](const CameraOutputMode& mode) {
            return mode.id() == cameraModeId;
        });
    return found == modes.end() ? nullptr : &*found;
}

[[nodiscard]] bool hasSingleOutputMode(
    const CameraSpecification& camera) noexcept {
    return camera.outputModes().size() == 1;
}

[[nodiscard]] std::optional<CameraModeId> singleOutputModeId(
    const CameraSpecification& camera) noexcept {
    if (!hasSingleOutputMode(camera)) {
        return std::nullopt;
    }
    return camera.outputModes().front().id();
}

[[nodiscard]] std::size_t findCandidateIndex(
    const std::vector<LightPathCameraCandidate>& candidates,
    const EquipmentComponentId& cameraId) noexcept {
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (candidates[index].cameraComponentId == cameraId) {
            return index;
        }
    }
    return candidates.size();
}

[[nodiscard]] bool appendSuitability(
    LightPathCameraCandidate& candidate,
    const CameraSpecification& camera,
    const std::optional<FluorescenceCameraRequirements>& requirements,
    Diagnostics& diagnostics) {
    if (!requirements) {
        return true;
    }

    candidate.suitability = CameraSuitabilityAssessor::assessFluorescence(
        camera, *requirements);
    for (const auto& warning : candidate.suitability->warnings) {
        diagnostics.push_back(warning);
    }
    return true;
}

[[nodiscard]] bool isUniqueBestCandidate(
    const std::vector<LightPathCameraCandidate>& candidates,
    const std::vector<const CameraSpecification*>& specifications,
    const FluorescenceCameraRequirements& requirements,
    std::size_t& bestIndex) noexcept {
    if (candidates.size() < 2 ||
        candidates.size() != specifications.size()) {
        return false;
    }

    std::optional<std::size_t> uniqueBest;
    for (std::size_t candidateIndex = 0;
         candidateIndex < candidates.size();
         ++candidateIndex) {
        if (!candidates[candidateIndex].suitability) {
            continue;
        }

        bool strictlyBetterThanAll = true;
        for (std::size_t otherIndex = 0;
             otherIndex < candidates.size();
             ++otherIndex) {
            if (candidateIndex == otherIndex) {
                continue;
            }
            if (!candidates[otherIndex].suitability ||
                !CameraSuitabilityAssessor::shouldRecommendBefore(
                    *specifications[candidateIndex],
                    *candidates[candidateIndex].suitability,
                    *specifications[otherIndex],
                    *candidates[otherIndex].suitability,
                    requirements) ||
                CameraSuitabilityAssessor::shouldRecommendBefore(
                    *specifications[otherIndex],
                    *candidates[otherIndex].suitability,
                    *specifications[candidateIndex],
                    *candidates[candidateIndex].suitability,
                    requirements)) {
                strictlyBetterThanAll = false;
                break;
            }
        }

        if (strictlyBetterThanAll) {
            if (uniqueBest) {
                return false;
            }
            uniqueBest = candidateIndex;
        }
    }

    if (!uniqueBest) {
        return false;
    }

    bestIndex = *uniqueBest;
    return true;
}

LightPathCameraResolution makeResolution(
    LightPathCameraResolutionStatus status,
    std::optional<ResolvedLightPathCamera> selected,
    std::vector<LightPathCameraCandidate> candidates,
    Diagnostics diagnostics) {
    return LightPathCameraResolution(
        status,
        std::move(selected),
        std::move(candidates),
        std::move(diagnostics));
}

} // namespace

LightPathCameraResolution::LightPathCameraResolution(
    LightPathCameraResolutionStatus status,
    std::optional<ResolvedLightPathCamera> selected,
    std::vector<LightPathCameraCandidate> candidates,
    Diagnostics diagnostics)
    : status_(status),
      selected_(std::move(selected)),
      candidates_(std::move(candidates)),
      diagnostics_(std::move(diagnostics)) {}

LightPathCameraResolutionStatus
LightPathCameraResolution::status() const noexcept {
    return status_;
}

const std::optional<ResolvedLightPathCamera>&
LightPathCameraResolution::selected() const noexcept {
    return selected_;
}

const std::vector<LightPathCameraCandidate>&
LightPathCameraResolution::candidates() const noexcept {
    return candidates_;
}

const Diagnostics& LightPathCameraResolution::diagnostics() const noexcept {
    return diagnostics_;
}

bool LightPathCameraResolution::isResolved() const noexcept {
    return status_ == LightPathCameraResolutionStatus::Resolved &&
           selected_.has_value();
}

LightPathCameraResolution LightPathCameraResolver::resolve(
    const MicroscopeEquipmentDefinition& equipment,
    const OperationMode& operationMode,
    const LightPathId& lightPathId,
    std::optional<EquipmentComponentId> manuallySelectedCameraId,
    std::optional<CameraModeId> cameraModeId,
    std::optional<FluorescenceCameraRequirements> fluorescenceRequirements) {
    Diagnostics diagnostics;

    const auto path = findLightPath(equipment, lightPathId);
    if (path == nullptr) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "light_path_camera.path_unknown",
            "The requested LightPath is not part of this microscope equipment definition.",
            lightPathId.value());
        return makeResolution(
            LightPathCameraResolutionStatus::Unsupported,
            std::nullopt, {}, std::move(diagnostics));
    }

    if (operationMode.microscopeId() != equipment.microscopeId()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "light_path_camera.operation_mode_microscope_mismatch",
            "The selected operation mode belongs to a different microscope.",
            lightPathId.value());
        return makeResolution(
            LightPathCameraResolutionStatus::Unsupported,
            std::nullopt, {}, std::move(diagnostics));
    }

    if (operationMode.findLightPathSetting(lightPathId) == nullptr) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "light_path_camera.path_not_configured_by_mode",
            "The selected operation mode does not contain a setting for this LightPath.",
            lightPathId.value());
        return makeResolution(
            LightPathCameraResolutionStatus::Unsupported,
            std::nullopt, {}, std::move(diagnostics));
    }

    const auto endpointIds = equipment.cameraEndpointIds(lightPathId);
    if (endpointIds.empty()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "light_path_camera.no_imaging_camera_endpoint",
            "The selected LightPath has no connected camera endpoint on an Imaging route.",
            lightPathId.value());
        return makeResolution(
            LightPathCameraResolutionStatus::Unsupported,
            std::nullopt, {}, std::move(diagnostics));
    }

    if (manuallySelectedCameraId &&
        std::find(
            endpointIds.begin(), endpointIds.end(),
            *manuallySelectedCameraId) == endpointIds.end()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "light_path_camera.manual_camera_not_on_path",
            "The manually selected camera is not connected to an Imaging route in the selected LightPath. The selection cannot be used for this configuration.",
            lightPathId.value(),
            manuallySelectedCameraId->value());
        return makeResolution(
            LightPathCameraResolutionStatus::Unsupported,
            std::nullopt, {}, std::move(diagnostics));
    }

    std::vector<LightPathCameraCandidate> candidates;
    std::vector<const CameraSpecification*> specifications;
    for (const auto& endpointId : endpointIds) {
        if (manuallySelectedCameraId &&
            endpointId != *manuallySelectedCameraId) {
            continue;
        }

        const auto* camera = findCameraSpecification(equipment, endpointId);
        if (camera == nullptr) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "light_path_camera.camera_specification_missing",
                "The selected LightPath camera endpoint has no CameraSpecification.",
                lightPathId.value(),
                endpointId.value());
            continue;
        }

        LightPathCameraCandidate candidate;
        candidate.cameraComponentId = endpointId;
        for (const auto& outputMode : camera->outputModes()) {
            candidate.availableCameraModeIds.push_back(outputMode.id());
        }

        if (cameraModeId &&
            findCameraOutputMode(*camera, *cameraModeId) == nullptr) {
            if (manuallySelectedCameraId) {
                appendDiagnostic(
                    diagnostics,
                    DiagnosticSeverity::Error,
                    "light_path_camera.manual_camera_mode_unsupported",
                    "The manually selected camera does not support the requested output mode.",
                    lightPathId.value(),
                    endpointId.value());
                return makeResolution(
                    LightPathCameraResolutionStatus::Unsupported,
                    std::nullopt, {}, std::move(diagnostics));
            }
            continue;
        }

        appendSuitability(
            candidate, *camera, fluorescenceRequirements, diagnostics);
        candidates.push_back(std::move(candidate));
        specifications.push_back(camera);
    }

    if (candidates.empty()) {
        if (!manuallySelectedCameraId) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "light_path_camera.no_candidate_supports_mode",
                "No camera endpoint on this LightPath supports the requested camera output mode.",
                lightPathId.value());
        }
        return makeResolution(
            LightPathCameraResolutionStatus::Unsupported,
            std::nullopt, {}, std::move(diagnostics));
    }

    if (manuallySelectedCameraId) {
        const auto& candidate = candidates.front();
        const auto* camera = specifications.front();
        const auto selectedModeId = cameraModeId
            ? cameraModeId
            : singleOutputModeId(*camera);
        if (!selectedModeId) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Information,
                "light_path_camera.output_mode_selection_required",
                "The selected camera supports multiple output modes; select one before acquisition.",
                lightPathId.value(),
                candidate.cameraComponentId.value());
            return makeResolution(
                LightPathCameraResolutionStatus::InputRequired,
                std::nullopt,
                std::move(candidates),
                std::move(diagnostics));
        }

        return makeResolution(
            LightPathCameraResolutionStatus::Resolved,
            ResolvedLightPathCamera{
                lightPathId, candidate.cameraComponentId, *selectedModeId},
            std::move(candidates),
            std::move(diagnostics));
    }

    if (cameraModeId) {
        for (auto& candidate : candidates) {
            candidate.recommended = candidates.size() == 1;
        }
        if (candidates.size() == 1) {
            return makeResolution(
                LightPathCameraResolutionStatus::Resolved,
                ResolvedLightPathCamera{
                    lightPathId,
                    candidates.front().cameraComponentId,
                    *cameraModeId},
                std::move(candidates),
                std::move(diagnostics));
        }
    }

    if (candidates.size() == 1) {
        const auto selectedModeId =
            singleOutputModeId(*specifications.front());
        if (!selectedModeId) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Information,
                "light_path_camera.output_mode_selection_required",
                "The connected camera supports multiple output modes; select one before acquisition.",
                lightPathId.value(),
                candidates.front().cameraComponentId.value());
            return makeResolution(
                LightPathCameraResolutionStatus::InputRequired,
                std::nullopt,
                std::move(candidates),
                std::move(diagnostics));
        }

        candidates.front().recommended = true;
        return makeResolution(
            LightPathCameraResolutionStatus::Resolved,
            ResolvedLightPathCamera{
                lightPathId,
                candidates.front().cameraComponentId,
                *selectedModeId},
            std::move(candidates),
            std::move(diagnostics));
    }

    if (fluorescenceRequirements) {
        std::size_t bestIndex = candidates.size();
        if (isUniqueBestCandidate(
                candidates, specifications,
                *fluorescenceRequirements, bestIndex)) {
            candidates[bestIndex].recommended = true;
            const auto selectedModeId = cameraModeId
                ? cameraModeId
                : singleOutputModeId(*specifications[bestIndex]);
            if (selectedModeId) {
                return makeResolution(
                    LightPathCameraResolutionStatus::Resolved,
                    ResolvedLightPathCamera{
                        lightPathId,
                        candidates[bestIndex].cameraComponentId,
                        *selectedModeId},
                    std::move(candidates),
                    std::move(diagnostics));
            }

            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Information,
                "light_path_camera.output_mode_selection_required",
                "The best-ranked connected camera supports multiple output modes; select one before acquisition.",
                lightPathId.value(),
                candidates[bestIndex].cameraComponentId.value());
            return makeResolution(
                LightPathCameraResolutionStatus::InputRequired,
                std::nullopt,
                std::move(candidates),
                std::move(diagnostics));
        }
    }

    appendDiagnostic(
        diagnostics,
        DiagnosticSeverity::Information,
        "light_path_camera.camera_selection_required",
        "Multiple cameras are physically connected to the selected LightPath and no unique suitability winner was established. Select one of the listed camera endpoints.",
        lightPathId.value());

    return makeResolution(
        LightPathCameraResolutionStatus::SelectionRequired,
        std::nullopt,
        std::move(candidates),
        std::move(diagnostics));
}

} // namespace microscope::core
