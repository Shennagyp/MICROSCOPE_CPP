#pragma once

#include "camera/CameraModelCatalog.h"

#include <string>
#include <vector>

namespace microscope::application::camera_support {

enum class CameraModelEnrichmentStatus {
    InsufficientIdentity,
    NoMatchingProfile,
    CandidateSelectionRequired,
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
    CameraModelEnrichmentStatus status =
        CameraModelEnrichmentStatus::InsufficientIdentity;
    std::vector<::camera::CameraModelKey> candidateProfiles;
    std::vector<CameraModelEnrichmentWarning> warnings;
};

class CameraModelDiscoveryEnricher final {
public:
    [[nodiscard]] static CameraModelEnrichmentResult enrich(
        const ::camera::CameraDeviceInfo& discoveredDevice,
        const ::camera::CameraModelCatalog& catalog);
};

} // namespace microscope::application::camera_support
