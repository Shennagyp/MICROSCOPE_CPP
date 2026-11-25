#pragma once

#include "microscope/core/ids/Ids.h"

#include <cstdint>

namespace microscope::core {

enum class OperationStatus {
    Accepted,
    Running,
    AwaitingOperator,
    Verifying,
    Succeeded,
    Failed,
    PartiallyCompleted,
    CancellationRequested,
    Cancelled
};

enum class StepStatus {
    Pending,
    Dispatched,
    AdapterAccepted,
    DeviceAcknowledged,
    Executing,
    AwaitingOperator,
    AwaitingObservation,
    Verifying,
    Verified,
    Failed,
    TimedOut,
    Skipped
};

// Immutable-by-convention value snapshot. The runtime creates a new revision
// rather than mutating a snapshot already delivered to a subscriber.
struct OperationSnapshot final {
    MicroscopeId microscopeId;
    RuntimeId runtimeId;
    RuntimeGeneration runtimeGeneration;
    OperationId operationId;
    OperationStatus status = OperationStatus::Accepted;
    std::uint64_t revision = 0;
};

} // namespace microscope::core
