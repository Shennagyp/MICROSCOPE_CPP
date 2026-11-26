#pragma once

#include "microscope/application/camera/CameraModelDiscoveryEnricher.h"
#include "microscope/application/persistence/CameraComponentModelBindingSqliteStore.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace microscope::application::camera_support {

enum class InstalledCameraModelEnrichmentErrorCode {
    BindingSetMicroscopeMismatch,
    BindingSetEquipmentRevisionMismatch,
    LightPathNotInstalled,
    CameraNotConnectedToLightPath,
    BindingNotFound,
    DuplicateBinding,
    CameraSpecificationMissing,
    BoundProfileNotApplied,
    CoreProjectionRejected
};

struct InstalledCameraModelEnrichmentError {
    InstalledCameraModelEnrichmentErrorCode code;
    std::optional<CameraModelEnrichmentStatus> discoveryStatus;
    std::optional<CameraSpecificationProjectionErrorCode> projectionError;
    std::vector<CameraModelEnrichmentWarning> warnings;
};

// Camera model facts composed for one installed camera endpoint of one selected
// LightPath. It retains all revision scopes needed to create a reproducible
// acquisition/configuration snapshot.
struct InstalledCameraModelEnrichment {
    core::MicroscopeId microscopeId;
    std::uint64_t equipmentDefinitionRevision = 0;
    std::uint64_t bindingSetRevision = 0;
    core::LightPathId lightPathId;
    core::EquipmentComponentId cameraComponentId;
    ::camera::CameraModelKey modelKey;
    std::uint64_t modelProfileRevision = 0;
    core::CameraSpecification cameraSpecification;
    std::vector<CameraModelEnrichmentWarning> warnings;
};

class InstalledCameraModelEnricher final {
public:
    // Composes the selected LightPath endpoint, the loaded revision-scoped
    // binding set, the shared model catalog and the live backend discovery
    // value. The returned CameraSpecification preserves equipment-owned
    // geometry/output modes. This does not open, claim or validate a vendor
    // SDK camera session.
    [[nodiscard]] static core::Result<
        InstalledCameraModelEnrichment,
        InstalledCameraModelEnrichmentError> enrich(
            const core::MicroscopeEquipmentDefinition& equipment,
            const ::camera::CameraModelCatalog& catalog,
            const persistence::PersistedCameraComponentModelBindings& bindingSet,
            const core::LightPathId& lightPathId,
            const core::EquipmentComponentId& cameraComponentId,
            const ::camera::CameraDeviceInfo& discoveredDevice);
};

} // namespace microscope::application::camera_support
