#pragma once

#include "microscope/core/runtime/OperationState.h"

namespace microscope::core {

// Lifecycle inputs are facts/events. Correlation, freshness and evidence
// validation happen before these pure transition functions are called.
enum class OperationLifecycleEvent {
    Started,
    AwaitingOperator,
    Resumed,
    VerificationStarted,
    Succeeded,
    Failed,
    PartiallyCompleted,
    CancellationRequested,
    CancellationConfirmed
};

enum class StepLifecycleEvent {
    CommandDispatched,
    AdapterAccepted,
    AdapterRejected,
    DeviceAcknowledged,
    DeviceProgress,
    DeviceActionCompleted,
    DeviceStateObserved,
    AdapterFailure,
    OperatorActionRequested,
    OperatorConfirmed,
    VerificationSucceeded,
    VerificationFailed,
    TimeoutElapsed,
    Skipped
};

enum class OperationLifecycleError {
    InvalidCurrentStatus,
    InvalidEvent,
    TransitionNotAllowed
};

enum class StepLifecycleError {
    InvalidCurrentStatus,
    InvalidEvent,
    TransitionNotAllowed
};

// Produces a new status only when the event is legal for the current status.
// Terminal operation statuses have no outgoing transitions.
[[nodiscard]] Result<
    OperationStatus,
    OperationLifecycleError> applyOperationLifecycleEvent(
        OperationStatus current,
        OperationLifecycleEvent event);

// DeviceActionCompleted and OperatorConfirmed are not verification. They move
// a step to Verifying (possibly via AwaitingObservation); only a Core
// VerificationSucceeded event can produce StepStatus::Verified.
[[nodiscard]] Result<
    StepStatus,
    StepLifecycleError> applyStepLifecycleEvent(
        StepStatus current,
        StepLifecycleEvent event);

} // namespace microscope::core
