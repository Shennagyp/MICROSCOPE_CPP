#pragma once

#include "camera/CameraModelCatalog.h"
#include "microscope/core/equipment/OpticalSpecifications.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace microscope::application::camera_support {

enum class CameraModelEnrichmentStatus {
    InsufficientIdentity,
    NoMatchingProfile,
    CandidateSelectionRequired,
    BoundProfileNotFound,
    BoundProfileRevisionMismatch,
    BoundProfileIdentityMismatch,
    ExactProfileApplied,
    ExactProfileAppliedWithConflicts
};

struct CameraModelEnrichmentWarning {
    std::string code;
    std::string message;
};

// A value result: the connected device identity and runtime feature values are
// retained. An exact persisted profile fills missing model facts; conflicts
// are reported and the live backend fact takes precedence for that field.
struct CameraModelEnrichmentResult {
    ::camera::CameraDeviceInfo deviceInfo;
    std::optional<core::EquipmentComponentId> equipmentComponentId;
    CameraModelEnrichmentStatus status =
        CameraModelEnrichmentStatus::InsufficientIdentity;
    std::vector<::camera::CameraModelKey> candidateProfiles;
    std::vector<CameraModelEnrichmentWarning> warnings;
};

struct CameraSpecificationProjectionResult {
    core::CameraSpecification specification;
    std::vector<CameraModelEnrichmentWarning> warnings;
};

enum class CameraSpecificationProjectionErrorCode {
    ModelProfileNotResolved,
    CoreSpecificationRejected
};

struct CameraSpecificationProjectionError {
    CameraSpecificationProjectionErrorCode code;
    std::optional<core::CameraSpecificationError> coreError;
};

class CameraModelDiscoveryEnricher final {
public:
    [[nodiscard]] static CameraModelEnrichmentResult enrich(
        const ::camera::CameraDeviceInfo& discoveredDevice,
        const ::camera::CameraModelCatalog& catalog);

    // Enriches the runtime device assigned to one installed camera component
    // using that component's persisted model-profile key and exact profile
    // revision. Missing runtime identity is supplied by the binding; any known
    // conflicting identity or stale profile revision blocks enrichment.
    [[nodiscard]] static CameraModelEnrichmentResult enrichBoundDevice(
        const ::camera::CameraDeviceInfo& discoveredDevice,
        const ::camera::CameraModelCatalog& catalog,
        const core::EquipmentComponentId& cameraComponentId,
        const ::camera::CameraModelKey& expectedModelKey,
        std::uint64_t expectedProfileRevision);

    // Projects enriched backend-neutral facts into the persistence-free Core
    // sensor model. Active sensor dimensions and pixel pitch remain on the
    // Core CameraSpecification itself.
    [[nodiscard]] static std::optional<core::CameraSensorCharacteristics>
    toCoreSensorCharacteristics(
        const ::camera::CameraDeviceInfo& enrichedDevice);

    // Enriches missing Core sensor-characteristic fields while preserving the
    // active sensor dimensions, pixel pitch and output modes declared by the
    // installed microscope equipment definition. Conflicting catalog values
    // are returned as warnings; they do not silently replace equipment data.
    [[nodiscard]] static core::Result<
        CameraSpecificationProjectionResult,
        core::CameraSpecificationError> projectToCoreCameraSpecification(
            const core::CameraSpecification& installedSpecification,
            const ::camera::CameraDeviceInfo& enrichedDevice);
};

} // namespace microscope::application::camera_support
