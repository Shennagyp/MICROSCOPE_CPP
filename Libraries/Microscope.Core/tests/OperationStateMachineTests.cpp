#include "microscope/core/runtime/OperationStateMachine.h"

#include <iostream>

namespace {

using namespace microscope::core;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testOperationLifecycleRequiresVerificationBeforeSuccess() {
    auto accepted = applyOperationLifecycleEvent(
        OperationStatus::Accepted, OperationLifecycleEvent::Succeeded);
    check(!accepted &&
          accepted.error() == OperationLifecycleError::TransitionNotAllowed,
          "does not allow an accepted operation to become successful directly");

    auto running = applyOperationLifecycleEvent(
        OperationStatus::Accepted, OperationLifecycleEvent::Started);
    check(running && running.value() == OperationStatus::Running,
          "starts an accepted operation");

    auto awaitingOperator = applyOperationLifecycleEvent(
        OperationStatus::Running, OperationLifecycleEvent::AwaitingOperator);
    check(awaitingOperator &&
          awaitingOperator.value() == OperationStatus::AwaitingOperator,
          "moves a running operation to awaiting-operator");

    auto resumed = applyOperationLifecycleEvent(
        OperationStatus::AwaitingOperator, OperationLifecycleEvent::Resumed);
    check(resumed && resumed.value() == OperationStatus::Running,
          "resumes an operation after operator input");

    auto verifying = applyOperationLifecycleEvent(
        OperationStatus::Running, OperationLifecycleEvent::VerificationStarted);
    check(verifying && verifying.value() == OperationStatus::Verifying,
          "requires a distinct operation verification stage");

    auto succeeded = applyOperationLifecycleEvent(
        OperationStatus::Verifying, OperationLifecycleEvent::Succeeded);
    check(succeeded && succeeded.value() == OperationStatus::Succeeded,
          "allows success only after operation verification begins");

    auto terminalTransition = applyOperationLifecycleEvent(
        OperationStatus::Succeeded, OperationLifecycleEvent::Failed);
    check(!terminalTransition &&
          terminalTransition.error() ==
              OperationLifecycleError::TransitionNotAllowed,
          "rejects transitions out of terminal operation state");
}

void testOperationCancellationAndPartialCompletionRemainDistinct() {
    auto cancelling = applyOperationLifecycleEvent(
        OperationStatus::Running,
        OperationLifecycleEvent::CancellationRequested);
    check(cancelling &&
          cancelling.value() == OperationStatus::CancellationRequested,
          "treats cancellation request as non-terminal");

    auto cancelled = applyOperationLifecycleEvent(
        OperationStatus::CancellationRequested,
        OperationLifecycleEvent::CancellationConfirmed);
    check(cancelled && cancelled.value() == OperationStatus::Cancelled,
          "requires cancellation confirmation before terminal Cancelled");

    auto partial = applyOperationLifecycleEvent(
        OperationStatus::Running,
        OperationLifecycleEvent::PartiallyCompleted);
    check(partial && partial.value() == OperationStatus::PartiallyCompleted,
          "represents an operation with completed work and an incomplete remainder separately from Failed");

    auto invalidCancel = applyOperationLifecycleEvent(
        OperationStatus::Cancelled,
        OperationLifecycleEvent::Started);
    check(!invalidCancel,
          "does not restart a cancelled operation");
}

void testDeviceActionCompletionDoesNotVerifyStep() {
    auto dispatched = applyStepLifecycleEvent(
        StepStatus::Pending, StepLifecycleEvent::CommandDispatched);
    check(dispatched && dispatched.value() == StepStatus::Dispatched,
          "records Core command dispatch as a distinct step fact");

    auto accepted = applyStepLifecycleEvent(
        StepStatus::Dispatched, StepLifecycleEvent::AdapterAccepted);
    check(accepted && accepted.value() == StepStatus::AdapterAccepted,
          "records adapter acceptance after dispatch");

    auto acknowledged = applyStepLifecycleEvent(
        StepStatus::AdapterAccepted, StepLifecycleEvent::DeviceAcknowledged);
    check(acknowledged &&
          acknowledged.value() == StepStatus::DeviceAcknowledged,
          "records device acknowledgement separately from adapter acceptance");

    auto actionCompleted = applyStepLifecycleEvent(
        StepStatus::DeviceAcknowledged,
        StepLifecycleEvent::DeviceActionCompleted);
    check(actionCompleted &&
          actionCompleted.value() == StepStatus::AwaitingObservation,
          "device action completion still awaits state evidence");

    auto observed = applyStepLifecycleEvent(
        StepStatus::AwaitingObservation,
        StepLifecycleEvent::DeviceStateObserved);
    check(observed && observed.value() == StepStatus::Verifying,
          "moves to verification after receiving a state observation");

    auto verified = applyStepLifecycleEvent(
        StepStatus::Verifying,
        StepLifecycleEvent::VerificationSucceeded);
    check(verified && verified.value() == StepStatus::Verified,
          "marks a step verified only after the Core verification result");

    auto directVerification = applyStepLifecycleEvent(
        StepStatus::AdapterAccepted,
        StepLifecycleEvent::VerificationSucceeded);
    check(!directVerification,
          "does not accept a verification result before entering Verifying");

    auto directSuccessAfterDispatch = applyStepLifecycleEvent(
        StepStatus::Dispatched,
        StepLifecycleEvent::DeviceActionCompleted);
    check(!directSuccessAfterDispatch,
          "does not accept device completion before adapter acceptance");

    auto homeReferenceFromAccepted = applyStepLifecycleEvent(
        StepStatus::AdapterAccepted,
        StepLifecycleEvent::HomeReferenceEstablished);
    check(homeReferenceFromAccepted &&
          homeReferenceFromAccepted.value() == StepStatus::Verifying,
          "explicit home-reference evidence moves the home step into verification");

    auto homeReferenceFromAwaiting = applyStepLifecycleEvent(
        StepStatus::AwaitingObservation,
        StepLifecycleEvent::HomeReferenceEstablished);
    check(homeReferenceFromAwaiting &&
          homeReferenceFromAwaiting.value() == StepStatus::Verifying,
          "explicit home-reference evidence can advance a step awaiting observation");

    auto homeReferenceWithoutDispatch = applyStepLifecycleEvent(
        StepStatus::Pending,
        StepLifecycleEvent::HomeReferenceEstablished);
    check(!homeReferenceWithoutDispatch,
          "home-reference evidence cannot satisfy an undispatched step");
}

void testManualAndFailurePathsRemainExplicit() {
    auto awaitingOperator = applyStepLifecycleEvent(
        StepStatus::Pending, StepLifecycleEvent::OperatorActionRequested);
    check(awaitingOperator &&
          awaitingOperator.value() == StepStatus::AwaitingOperator,
          "supports a manual step without inventing a device command");

    auto operatorConfirmed = applyStepLifecycleEvent(
        StepStatus::AwaitingOperator, StepLifecycleEvent::OperatorConfirmed);
    check(operatorConfirmed &&
          operatorConfirmed.value() == StepStatus::Verifying,
          "operator confirmation begins Core verification but does not mark the step complete");

    auto operatorVerified = applyStepLifecycleEvent(
        StepStatus::Verifying, StepLifecycleEvent::VerificationSucceeded);
    check(operatorVerified && operatorVerified.value() == StepStatus::Verified,
          "allows the Core evidence policy to accept operator-confirmed completion where permitted");

    auto rejected = applyStepLifecycleEvent(
        StepStatus::Dispatched, StepLifecycleEvent::AdapterRejected);
    check(rejected && rejected.value() == StepStatus::Failed,
          "maps adapter dispatch rejection to a failed step");

    auto timedOut = applyStepLifecycleEvent(
        StepStatus::AdapterAccepted, StepLifecycleEvent::TimeoutElapsed);
    check(timedOut && timedOut.value() == StepStatus::TimedOut,
          "maps a Core-owned timeout to a timed-out step");

    auto terminalStepTransition = applyStepLifecycleEvent(
        StepStatus::TimedOut, StepLifecycleEvent::DeviceStateObserved);
    check(!terminalStepTransition,
          "does not silently revive a timed-out step from late state feedback");
}

void testInvalidLifecycleValuesAreRejected() {
    auto invalidOperationStatus = applyOperationLifecycleEvent(
        static_cast<OperationStatus>(255),
        OperationLifecycleEvent::Started);
    check(!invalidOperationStatus &&
          invalidOperationStatus.error() ==
              OperationLifecycleError::InvalidCurrentStatus,
          "rejects unknown operation status values");

    auto invalidOperationEvent = applyOperationLifecycleEvent(
        OperationStatus::Accepted,
        static_cast<OperationLifecycleEvent>(255));
    check(!invalidOperationEvent &&
          invalidOperationEvent.error() ==
              OperationLifecycleError::InvalidEvent,
          "rejects unknown operation lifecycle events");

    auto invalidStepStatus = applyStepLifecycleEvent(
        static_cast<StepStatus>(255),
        StepLifecycleEvent::CommandDispatched);
    check(!invalidStepStatus &&
          invalidStepStatus.error() == StepLifecycleError::InvalidCurrentStatus,
          "rejects unknown step status values");

    auto invalidStepEvent = applyStepLifecycleEvent(
        StepStatus::Pending,
        static_cast<StepLifecycleEvent>(255));
    check(!invalidStepEvent &&
          invalidStepEvent.error() == StepLifecycleError::InvalidEvent,
          "rejects unknown step lifecycle event values");
}

} // namespace

int main() {
    testOperationLifecycleRequiresVerificationBeforeSuccess();
    testOperationCancellationAndPartialCompletionRemainDistinct();
    testDeviceActionCompletionDoesNotVerifyStep();
    testManualAndFailurePathsRemainExplicit();
    testInvalidLifecycleValuesAreRejected();

    if (failures != 0) {
        std::cerr << failures << " operation lifecycle contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All operation lifecycle contract tests passed.\n";
    return 0;
}
