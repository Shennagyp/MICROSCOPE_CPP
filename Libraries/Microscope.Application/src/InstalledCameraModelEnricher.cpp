#include "microscope/application/camera/InstalledCameraModelEnricher.h"

#include <algorithm>
#include <utility>

namespace microscope::application::camera_support {
namespace {

using Error = InstalledCameraModelEnrichmentError;
using ErrorCode = InstalledCameraModelEnrichmentErrorCode;

[[nodiscard]] Error makeError(
    ErrorCode code,
    std::vector<CameraModelEnrichmentWarning> warnings = {},
    std::optional<CameraModelEnrichmentStatus> discoveryStatus = std::nullopt,
    std::optional<CameraSpecificationProjectionErrorCode> projectionError =
        std::nullopt) {
    return Error{
        code,
        discoveryStatus,
        projectionError,
        std::move(warnings)};
}

[[nodiscard]] bool containsLightPath(
    const core::MicroscopeEquipmentDefinition& equipment,
    const core::LightPathId& lightPathId) noexcept {
    const auto& lightPaths = equipment.lightPaths();
    return std::any_of(
        lightPaths.begin(), lightPaths.end(),
        [&lightPathId](const core::LightPath& lightPath) {
            return lightPath.id() == lightPathId;
        });
}

[[nodiscard]] const core::CameraSpecification* findCameraSpecification(
    const core::MicroscopeEquipmentDefinition& equipment,
    const core::EquipmentComponentId& cameraComponentId) noexcept {
    const auto& specifications = equipment.cameraSpecifications();
    const auto found = std::find_if(
        specifications.begin(), specifications.end(),
        [&cameraComponentId](const core::CameraSpecification& specification) {
            return specification.componentId() == cameraComponentId;
        });
    return found == specifications.end() ? nullptr : &*found;
}

} // namespace

core::Result<
    InstalledCameraModelEnrichment,
    InstalledCameraModelEnrichmentError>
InstalledCameraModelEnricher::enrich(
    const core::MicroscopeEquipmentDefinition& equipment,
    const ::camera::CameraModelCatalog& catalog,
    const persistence::PersistedCameraComponentModelBindings& bindingSet,
    const core::LightPathId& lightPathId,
    const core::EquipmentComponentId& cameraComponentId,
    const ::camera::CameraDeviceInfo& discoveredDevice) {
    if (!bindingSet.microscopeId ||
        *bindingSet.microscopeId != equipment.microscopeId()) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(ErrorCode::BindingSetMicroscopeMismatch));
    }

    if (bindingSet.equipmentDefinitionRevision != equipment.revision()) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(ErrorCode::BindingSetEquipmentRevisionMismatch));
    }

    if (bindingSet.bindingRevision == 0 && !bindingSet.bindings.empty()) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(ErrorCode::InvalidBindingSetRevision));
    }

    if (!containsLightPath(equipment, lightPathId)) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(ErrorCode::LightPathNotInstalled));
    }

    const auto endpointIds = equipment.cameraEndpointIds(lightPathId);
    if (std::find(
            endpointIds.begin(), endpointIds.end(), cameraComponentId) ==
        endpointIds.end()) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(ErrorCode::CameraNotConnectedToLightPath));
    }

    const auto* installedSpecification =
        findCameraSpecification(equipment, cameraComponentId);
    if (installedSpecification == nullptr) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(ErrorCode::CameraSpecificationMissing));
    }

    const persistence::CameraComponentModelBinding* selectedBinding = nullptr;
    for (const auto& binding : bindingSet.bindings) {
        if (binding.cameraComponentId != cameraComponentId) {
            continue;
        }
        if (selectedBinding != nullptr) {
            return core::Result<
                InstalledCameraModelEnrichment,
                InstalledCameraModelEnrichmentError>::failure(
                    makeError(ErrorCode::DuplicateBinding));
        }
        selectedBinding = &binding;
    }

    if (selectedBinding == nullptr) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(ErrorCode::BindingNotFound));
    }

    const auto enrichment = CameraModelDiscoveryEnricher::enrichBoundDevice(
        discoveredDevice,
        catalog,
        cameraComponentId,
        selectedBinding->modelKey,
        selectedBinding->modelProfileRevision);

    if (enrichment.status != CameraModelEnrichmentStatus::ExactProfileApplied &&
        enrichment.status !=
            CameraModelEnrichmentStatus::ExactProfileAppliedWithConflicts) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(
                    ErrorCode::BoundProfileNotApplied,
                    enrichment.warnings,
                    enrichment.status));
    }

    const auto projection =
        CameraModelDiscoveryEnricher::projectToCoreCameraSpecification(
            *installedSpecification,
            enrichment);
    if (!projection) {
        return core::Result<
            InstalledCameraModelEnrichment,
            InstalledCameraModelEnrichmentError>::failure(
                makeError(
                    ErrorCode::CoreProjectionRejected,
                    enrichment.warnings,
                    enrichment.status,
                    projection.error().code));
    }

    return core::Result<
        InstalledCameraModelEnrichment,
        InstalledCameraModelEnrichmentError>::success(
            InstalledCameraModelEnrichment{
                equipment.microscopeId(),
                equipment.revision(),
                bindingSet.bindingRevision,
                lightPathId,
                cameraComponentId,
                selectedBinding->modelKey,
                selectedBinding->modelProfileRevision,
                projection.value().specification,
                projection.value().warnings});
}

} // namespace microscope::application::camera_support
