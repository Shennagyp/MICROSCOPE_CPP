#include "microscope/core/runtime/OperationStateMachine.h"

namespace microscope::core {
namespace {

[[nodiscard]] bool isKnownOperationStatus(OperationStatus value) noexcept {
    switch (value) {
    case OperationStatus::Accepted:
    case OperationStatus::Running:
    case OperationStatus::AwaitingOperator:
    case OperationStatus::Verifying:
    case OperationStatus::Succeeded:
    case OperationStatus::Failed:
    case OperationStatus::PartiallyCompleted:
    case OperationStatus::CancellationRequested:
    case OperationStatus::Cancelled:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownOperationEvent(
    OperationLifecycleEvent value) noexcept {
    switch (value) {
    case OperationLifecycleEvent::Started:
    case OperationLifecycleEvent::AwaitingOperator:
    case OperationLifecycleEvent::Resumed:
    case OperationLifecycleEvent::VerificationStarted:
    case OperationLifecycleEvent::Succeeded:
    case OperationLifecycleEvent::Failed:
    case OperationLifecycleEvent::PartiallyCompleted:
    case OperationLifecycleEvent::CancellationRequested:
    case OperationLifecycleEvent::CancellationConfirmed:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownStepStatus(StepStatus value) noexcept {
    switch (value) {
    case StepStatus::Pending:
    case StepStatus::Dispatched:
    case StepStatus::AdapterAccepted:
    case StepStatus::DeviceAcknowledged:
    case StepStatus::Executing:
    case StepStatus::AwaitingOperator:
    case StepStatus::AwaitingObservation:
    case StepStatus::Verifying:
    case StepStatus::Verified:
    case StepStatus::Failed:
    case StepStatus::TimedOut:
    case StepStatus::Skipped:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownStepEvent(StepLifecycleEvent value) noexcept {
    switch (value) {
    case StepLifecycleEvent::CommandDispatched:
    case StepLifecycleEvent::AdapterAccepted:
    case StepLifecycleEvent::AdapterRejected:
    case StepLifecycleEvent::DeviceAcknowledged:
    case StepLifecycleEvent::DeviceProgress:
    case StepLifecycleEvent::DeviceActionCompleted:
    case StepLifecycleEvent::DeviceStateObserved:
    case StepLifecycleEvent::HomeReferenceEstablished:
    case StepLifecycleEvent::AdapterFailure:
    case StepLifecycleEvent::OperatorActionRequested:
    case StepLifecycleEvent::OperatorConfirmed:
    case StepLifecycleEvent::VerificationSucceeded:
    case StepLifecycleEvent::VerificationFailed:
    case StepLifecycleEvent::TimeoutElapsed:
    case StepLifecycleEvent::Skipped:
        return true;
    }
    return false;
}

[[nodiscard]] bool isTerminal(OperationStatus status) noexcept {
    return status == OperationStatus::Succeeded ||
           status == OperationStatus::Failed ||
           status == OperationStatus::PartiallyCompleted ||
           status == OperationStatus::Cancelled;
}

[[nodiscard]] bool isTerminal(StepStatus status) noexcept {
    return status == StepStatus::Verified ||
           status == StepStatus::Failed ||
           status == StepStatus::TimedOut ||
           status == StepStatus::Skipped;
}

} // namespace

Result<
    OperationStatus,
    OperationLifecycleError> applyOperationLifecycleEvent(
        OperationStatus current,
        OperationLifecycleEvent event) {
    if (!isKnownOperationStatus(current)) {
        return Result<
            OperationStatus,
            OperationLifecycleError>::failure(
                OperationLifecycleError::InvalidCurrentStatus);
    }
    if (!isKnownOperationEvent(event)) {
        return Result<
            OperationStatus,
            OperationLifecycleError>::failure(
                OperationLifecycleError::InvalidEvent);
    }
    if (isTerminal(current)) {
        return Result<
            OperationStatus,
            OperationLifecycleError>::failure(
                OperationLifecycleError::TransitionNotAllowed);
    }

    switch (current) {
    case OperationStatus::Accepted:
        if (event == OperationLifecycleEvent::Started) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Running);
        }
        if (event == OperationLifecycleEvent::CancellationRequested) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::CancellationRequested);
        }
        if (event == OperationLifecycleEvent::Failed) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Failed);
        }
        break;

    case OperationStatus::Running:
        if (event == OperationLifecycleEvent::AwaitingOperator) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::AwaitingOperator);
        }
        if (event == OperationLifecycleEvent::VerificationStarted) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Verifying);
        }
        if (event == OperationLifecycleEvent::Failed) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Failed);
        }
        if (event == OperationLifecycleEvent::PartiallyCompleted) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::PartiallyCompleted);
        }
        if (event == OperationLifecycleEvent::CancellationRequested) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::CancellationRequested);
        }
        break;

    case OperationStatus::AwaitingOperator:
        if (event == OperationLifecycleEvent::Resumed) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Running);
        }
        if (event == OperationLifecycleEvent::VerificationStarted) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Verifying);
        }
        if (event == OperationLifecycleEvent::Failed) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Failed);
        }
        if (event == OperationLifecycleEvent::PartiallyCompleted) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::PartiallyCompleted);
        }
        if (event == OperationLifecycleEvent::CancellationRequested) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::CancellationRequested);
        }
        break;

    case OperationStatus::Verifying:
        if (event == OperationLifecycleEvent::Succeeded) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Succeeded);
        }
        if (event == OperationLifecycleEvent::Failed) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Failed);
        }
        if (event == OperationLifecycleEvent::PartiallyCompleted) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::PartiallyCompleted);
        }
        if (event == OperationLifecycleEvent::AwaitingOperator) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::AwaitingOperator);
        }
        if (event == OperationLifecycleEvent::CancellationRequested) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::CancellationRequested);
        }
        break;

    case OperationStatus::CancellationRequested:
        if (event == OperationLifecycleEvent::CancellationConfirmed) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Cancelled);
        }
        if (event == OperationLifecycleEvent::Failed) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(OperationStatus::Failed);
        }
        if (event == OperationLifecycleEvent::PartiallyCompleted) {
            return Result<
                OperationStatus,
                OperationLifecycleError>::success(
                    OperationStatus::PartiallyCompleted);
        }
        break;

    case OperationStatus::Succeeded:
    case OperationStatus::Failed:
    case OperationStatus::PartiallyCompleted:
    case OperationStatus::Cancelled:
        break;
    }

    return Result<
        OperationStatus,
        OperationLifecycleError>::failure(
            OperationLifecycleError::TransitionNotAllowed);
}

Result<
    StepStatus,
    StepLifecycleError> applyStepLifecycleEvent(
        StepStatus current,
        StepLifecycleEvent event) {
    if (!isKnownStepStatus(current)) {
        return Result<
            StepStatus,
            StepLifecycleError>::failure(
                StepLifecycleError::InvalidCurrentStatus);
    }
    if (!isKnownStepEvent(event)) {
        return Result<
            StepStatus,
            StepLifecycleError>::failure(
                StepLifecycleError::InvalidEvent);
    }
    if (isTerminal(current)) {
        return Result<
            StepStatus,
            StepLifecycleError>::failure(
                StepLifecycleError::TransitionNotAllowed);
    }

    switch (current) {
    case StepStatus::Pending:
        if (event == StepLifecycleEvent::CommandDispatched) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Dispatched);
        }
        if (event == StepLifecycleEvent::OperatorActionRequested) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::AwaitingOperator);
        }
        if (event == StepLifecycleEvent::DeviceStateObserved) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Verifying);
        }
        if (event == StepLifecycleEvent::AdapterFailure) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Failed);
        }
        if (event == StepLifecycleEvent::Skipped) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Skipped);
        }
        break;

    case StepStatus::Dispatched:
        if (event == StepLifecycleEvent::AdapterAccepted) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::AdapterAccepted);
        }
        if (event == StepLifecycleEvent::AdapterRejected ||
            event == StepLifecycleEvent::AdapterFailure) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Failed);
        }
        if (event == StepLifecycleEvent::TimeoutElapsed) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::TimedOut);
        }
        break;

    case StepStatus::AdapterAccepted:
        if (event == StepLifecycleEvent::HomeReferenceEstablished) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Verifying);
        }
        if (event == StepLifecycleEvent::DeviceAcknowledged) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::DeviceAcknowledged);
        }
        if (event == StepLifecycleEvent::DeviceProgress) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Executing);
        }
        if (event == StepLifecycleEvent::DeviceActionCompleted) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::AwaitingObservation);
        }
        if (event == StepLifecycleEvent::DeviceStateObserved) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Verifying);
        }
        if (event == StepLifecycleEvent::OperatorActionRequested) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::AwaitingOperator);
        }
        if (event == StepLifecycleEvent::AdapterFailure) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Failed);
        }
        if (event == StepLifecycleEvent::TimeoutElapsed) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::TimedOut);
        }
        break;

    case StepStatus::DeviceAcknowledged:
    case StepStatus::Executing:
        if (event == StepLifecycleEvent::HomeReferenceEstablished) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Verifying);
        }
        if (event == StepLifecycleEvent::DeviceProgress) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Executing);
        }
        if (event == StepLifecycleEvent::DeviceActionCompleted) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::AwaitingObservation);
        }
        if (event == StepLifecycleEvent::DeviceStateObserved) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Verifying);
        }
        if (event == StepLifecycleEvent::OperatorActionRequested) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::AwaitingOperator);
        }
        if (event == StepLifecycleEvent::AdapterFailure) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Failed);
        }
        if (event == StepLifecycleEvent::TimeoutElapsed) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::TimedOut);
        }
        break;

    case StepStatus::AwaitingOperator:
        if (event == StepLifecycleEvent::OperatorConfirmed ||
            event == StepLifecycleEvent::DeviceStateObserved) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Verifying);
        }
        if (event == StepLifecycleEvent::AdapterFailure) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Failed);
        }
        if (event == StepLifecycleEvent::TimeoutElapsed) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::TimedOut);
        }
        break;

    case StepStatus::AwaitingObservation:
        if (event == StepLifecycleEvent::DeviceStateObserved ||
            event == StepLifecycleEvent::HomeReferenceEstablished ||
            event == StepLifecycleEvent::OperatorConfirmed) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Verifying);
        }
        if (event == StepLifecycleEvent::AdapterFailure) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Failed);
        }
        if (event == StepLifecycleEvent::TimeoutElapsed) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::TimedOut);
        }
        break;

    case StepStatus::Verifying:
        if (event == StepLifecycleEvent::VerificationSucceeded) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Verified);
        }
        if (event == StepLifecycleEvent::VerificationFailed ||
            event == StepLifecycleEvent::AdapterFailure) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::Failed);
        }
        if (event == StepLifecycleEvent::TimeoutElapsed) {
            return Result<StepStatus, StepLifecycleError>::success(
                StepStatus::TimedOut);
        }
        break;

    case StepStatus::Verified:
    case StepStatus::Failed:
    case StepStatus::TimedOut:
    case StepStatus::Skipped:
        break;
    }

    return Result<StepStatus, StepLifecycleError>::failure(
        StepLifecycleError::TransitionNotAllowed);
}

} // namespace microscope::core
