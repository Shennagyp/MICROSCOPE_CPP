#include "microscope/core/runtime/SerializedOperationRuntime.h"
#include "microscope/core/motion/StageMotionPort.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {
using namespace microscope::core;

template<class T,class E>
T take(Result<T,E> result) {
    if(!result)throw std::runtime_error("serialized runtime fixture rejected");
    return std::move(result).value();
}
template<class T>T id(const char* value){return take(T::create(value));}

class TestRuntimeSignal final : public IFeedbackRuntimeSignal {
public:
    void feedbackAvailable() noexcept override { available.fetch_add(1); }
    void feedbackLossDetected() noexcept override { loss.fetch_add(1); }
    void feedbackIngressClosed() noexcept override { closed.fetch_add(1); }
    std::atomic<int> available{0};
    std::atomic<int> loss{0};
    std::atomic<int> closed{0};
};

RuntimeDeviceCorrelation deviceSession(std::uint64_t sessionGeneration=4,std::uint64_t runtimeGeneration=2) {
    return RuntimeDeviceCorrelation{
        id<MicroscopeId>("scope"),id<RuntimeId>("runtime"),
        take(RuntimeGeneration::create(runtimeGeneration)),
        take(DeviceSessionGeneration::create(sessionGeneration)),
        id<DeviceId>("stage")};
}

FeedbackEnvelope<StageMotionFeedbackPayload> acknowledgement(
    const RuntimeDeviceCorrelation& device,
    const OperationId& operation,const StepId& step,const CommandId& command) {
    const auto axes=take(StageAxisSet::create({
        id<StageAxisId>("x"),id<StageAxisId>("y")}));
    const auto payload=take(StageMotionAcknowledgement::create(
        StageMotionAction::MoveAxesTo,axes));
    const auto metadata=take(FeedbackMetadata::create(
        FeedbackCorrelation{device,OperationCorrelation{operation,step,command}},
        FeedbackKind::DeviceAcknowledged,ObservationSource::DeviceEvent));
    return FeedbackEnvelope<StageMotionFeedbackPayload>::create(
        metadata,StageMotionFeedbackPayload{payload});
}

FeedbackEnvelope<StageMotionFeedbackPayload> actionCompleted(
    const RuntimeDeviceCorrelation& device,
    const OperationId& operation,const StepId& step,const CommandId& command) {
    const auto axes=take(StageAxisSet::create({
        id<StageAxisId>("x"),id<StageAxisId>("y")}));
    const auto payload=take(StageMotionActionCompletion::create(
        StageMotionAction::MoveAxesTo,axes));
    const auto metadata=take(FeedbackMetadata::create(
        FeedbackCorrelation{device,OperationCorrelation{operation,step,command}},
        FeedbackKind::DeviceActionCompleted,ObservationSource::HardwareReadback));
    return FeedbackEnvelope<StageMotionFeedbackPayload>::create(
        metadata,StageMotionFeedbackPayload{payload});
}

FeedbackEnvelope<StageMotionFeedbackPayload> observedState(
    const RuntimeDeviceCorrelation& device,
    const OperationId& operation,const StepId& step) {
    const auto payload=take(AxisMotionStateObservation::create(
        id<StageAxisId>("x"),StageAxisMotionState::Idle));
    const auto metadata=take(FeedbackMetadata::create(
        FeedbackCorrelation{device,OperationCorrelation{operation,step,std::nullopt}},
        FeedbackKind::DeviceStateObserved,ObservationSource::HardwareReadback));
    return FeedbackEnvelope<StageMotionFeedbackPayload>::create(
        metadata,StageMotionFeedbackPayload{payload});
}

FeedbackEnvelope<StageMotionFeedbackPayload> homeReference(
    const RuntimeDeviceCorrelation& device,
    const OperationId& operation,const StepId& step,const CommandId& command) {
    auto x=take(AxisPosition::create(
        id<StageAxisId>("x"),0.25,AxisCoordinateUnit::Millimeters));
    auto y=take(AxisPosition::create(
        id<StageAxisId>("y"),-0.5,AxisCoordinateUnit::Millimeters));
    auto xObservation=take(AxisPositionObservation::create(
        x,0.002,StagePositionEvidence::ControllerReported));
    auto yObservation=take(AxisPositionObservation::create(
        y,0.001,StagePositionEvidence::EncoderMeasured));
    auto payload=take(StageHomeReferenceObservation::create(
        {xObservation,yObservation}));
    auto metadata=take(FeedbackMetadata::create(
        FeedbackCorrelation{device,OperationCorrelation{operation,step,command}},
        FeedbackKind::HomeReferenceEstablished,ObservationSource::HardwareReadback));
    return FeedbackEnvelope<StageMotionFeedbackPayload>::create(
        metadata,StageMotionFeedbackPayload{payload});
}

FeedbackEnvelope<StageMotionFeedbackPayload> staleSessionFeedback(
    RuntimeDeviceCorrelation device) {
    const auto payload=take(AxisMotionStateObservation::create(
        id<StageAxisId>("x"),StageAxisMotionState::Moving));
    const auto metadata=take(FeedbackMetadata::create(
        FeedbackCorrelation{std::move(device),std::nullopt},
        FeedbackKind::DeviceStateObserved,ObservationSource::DeviceEvent));
    return FeedbackEnvelope<StageMotionFeedbackPayload>::create(
        metadata,StageMotionFeedbackPayload{payload});
}

FeedbackEnvelope<StageMotionFeedbackPayload> adapterFailure(
    const RuntimeDeviceCorrelation& device,
    const OperationId& operation,const StepId& step,const CommandId& command) {
    const auto payload=take(StageMotionFaultObservation::create(
        StageMotionFaultKind::ControllerFault,id<StageAxisId>("x")));
    const auto metadata=take(FeedbackMetadata::create(
        FeedbackCorrelation{device,OperationCorrelation{operation,step,command}},
        FeedbackKind::AdapterFailure,ObservationSource::AdapterReport));
    return FeedbackEnvelope<StageMotionFeedbackPayload>::create(
        metadata,StageMotionFeedbackPayload{payload});
}
}

int main() {
    int failures=0;
    const auto check=[&](bool value,const char* message) {
        if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
    };

    TestRuntimeSignal signal;
    auto runtime=take(SerializedOperationRuntime<StageMotionFeedbackPayload>::create(
        id<MicroscopeId>("scope"),id<RuntimeId>("runtime"),
        take(RuntimeGeneration::create(2)),16,signal));
    auto notBound=runtime->createOperation(id<OperationId>("op"));
    check(!notBound&&notBound.error()==SerializedOperationRuntimeError::OwnerThreadNotBound,
          "runtime rejects state mutation before an owner thread is bound");
    auto bound=runtime->bindOwnerThread();
    check(bound&&bound.value(),"first caller binds as the serialized runtime owner");
    check(!runtime->bindOwnerThread().value(),
          "rebinding from the same owner thread is idempotent");

    const auto session=deviceSession();
    check(bool(runtime->openDeviceSession(session)),
          "runtime opens the current stage device session");

    const auto operation=id<OperationId>("operation-navigation");
    const auto step=id<StepId>("step-move");
    const auto command=id<CommandId>("command-move");
    auto created=take(runtime->createOperation(operation));
    check(created.status()==OperationStatus::Accepted&&created.revision()==1,
          "new operation begins Accepted at revision one");
    auto started=take(runtime->startOperation(operation));
    check(started.status()==OperationStatus::Running&&started.revision()==2,
          "operation starts through the lifecycle reducer");
    check(runtime->createStep(operation,step).value()==StepStatus::Pending,
          "operation owns a pending step");
    check(runtime->dispatchCommand(operation,step,command,session).value()==StepStatus::Dispatched,
          "command dispatch is recorded before adapter invocation");
    check(runtime->recordAdapterAccepted(operation,step).value()==StepStatus::AdapterAccepted,
          "adapter acceptance is distinct from device acknowledgement");

    auto ack=acknowledgement(session,operation,step,command);
    check(runtime->feedbackIngress().submit(std::move(ack))==FeedbackSubmitStatus::Enqueued,
          "device acknowledgement enters the bounded feedback inbox");
    auto ackResult=take(runtime->processNextFeedback());
    check(ackResult&&ackResult->disposition==FeedbackProcessingDisposition::AppliedToOperation,
          "current device/operation/step/command feedback is applied");
    check(ackResult&&ackResult->stepStatus==StepStatus::DeviceAcknowledged,
          "acknowledgement advances only the matching step");
    check(ackResult&&ackResult->receipt&&
          std::holds_alternative<StageMotionAcknowledgement>(
              ackResult->receipt->feedback().payload()),
          "runtime returns the typed payload to the owner after lifecycle reduction");

    auto completion=actionCompleted(session,operation,step,command);
    check(runtime->feedbackIngress().submit(std::move(completion))==FeedbackSubmitStatus::Enqueued,
          "device action completion is accepted with operation/step correlation");
    auto completionResult=take(runtime->processNextFeedback());
    check(completionResult&&completionResult->stepStatus==StepStatus::AwaitingObservation,
          "action completion moves the step to AwaitingObservation, not Verified");

    auto observation=observedState(session,operation,step);
    check(runtime->feedbackIngress().submit(std::move(observation))==FeedbackSubmitStatus::Enqueued,
          "device state observation is accepted for the active operation step");
    auto observationResult=take(runtime->processNextFeedback());
    check(observationResult&&observationResult->stepStatus==StepStatus::Verifying,
          "observed state moves the step to Verifying");
    check(observationResult&&observationResult->operationSnapshot&&
          observationResult->operationSnapshot->status()==OperationStatus::Running,
          "one step entering Verifying does not prematurely move the whole operation to Verifying");

    check(runtime->recordStepVerification(operation,step,true).value()==StepStatus::Verified,
          "Core verification is a separate explicit step transition");
    auto operationVerification=take(runtime->beginOperationVerification(operation));
    check(operationVerification.status()==OperationStatus::Verifying,
          "operation-level verification begins only after every step is Verified");
    auto operationCompleted=take(runtime->recordOperationVerification(operation,true));
    check(operationCompleted.status()==OperationStatus::Succeeded &&
          runtime->operationSnapshot(operation).value().status()==OperationStatus::Succeeded,
          "only the owner can commit operation success after operation-level verification");

    const auto estimatedOperation=id<OperationId>("operation-estimated-position");
    const auto estimatedStep=id<StepId>("step-estimated-position");
    const auto estimatedCommand=id<CommandId>("command-estimated-position");
    take(runtime->createOperation(estimatedOperation));
    take(runtime->startOperation(estimatedOperation));
    take(runtime->createStep(estimatedOperation,estimatedStep));
    take(runtime->dispatchCommand(
        estimatedOperation,estimatedStep,estimatedCommand,session));
    take(runtime->recordAdapterAccepted(estimatedOperation,estimatedStep));
    auto estimatedCompletion=actionCompleted(
        session,estimatedOperation,estimatedStep,estimatedCommand);
    check(runtime->feedbackIngress().submit(std::move(estimatedCompletion))==
              FeedbackSubmitStatus::Enqueued,
          "open-loop driver completion enters the same correlated feedback path");
    auto estimatedCompletionResult=take(runtime->processNextFeedback());
    check(estimatedCompletionResult &&
          estimatedCompletionResult->stepStatus==StepStatus::AwaitingObservation,
          "action completion does not fabricate a position observation");
    check(runtime->beginStepVerification(estimatedOperation,estimatedStep).value()==
              StepStatus::Verifying,
          "owner can begin verification after its calibrated command-derived estimate satisfies policy");
    check(runtime->recordStepVerification(estimatedOperation,estimatedStep,true).value()==
              StepStatus::Verified,
          "bounded estimate verification uses the same explicit Verified transition");
    take(runtime->beginOperationVerification(estimatedOperation));
    check(runtime->recordOperationVerification(estimatedOperation,true).value().status()==
              OperationStatus::Succeeded,
          "an accepted command-derived verification can complete the operation without inventing sensor feedback");

    const auto preDispatchOperation=id<OperationId>("operation-pre-dispatch-failure");
    const auto preDispatchStep=id<StepId>("step-pre-dispatch-failure");
    take(runtime->createOperation(preDispatchOperation));
    take(runtime->startOperation(preDispatchOperation));
    take(runtime->createStep(preDispatchOperation,preDispatchStep));
    check(runtime->recordPreDispatchFailure(preDispatchOperation,preDispatchStep).value()==
              StepStatus::Failed &&
          runtime->operationSnapshot(preDispatchOperation).value().status()==
              OperationStatus::Failed,
          "a failure before any adapter call terminates both the pending step and its operation");

    auto oldSession=staleSessionFeedback(deviceSession(3));
    check(runtime->feedbackIngress().submit(std::move(oldSession))==FeedbackSubmitStatus::Enqueued,
          "old-session callback is retained for freshness evaluation");
    auto oldSessionResult=take(runtime->processNextFeedback());
    check(oldSessionResult&&oldSessionResult->disposition==
          FeedbackProcessingDisposition::StaleDeviceSession,
          "callback from an earlier device reconnect is discarded");

    auto uncorrelated=staleSessionFeedback(session);
    check(runtime->feedbackIngress().submit(std::move(uncorrelated))==FeedbackSubmitStatus::Enqueued,
          "unsolicited device telemetry is accepted");
    auto telemetryResult=take(runtime->processNextFeedback());
    check(telemetryResult&&telemetryResult->disposition==
          FeedbackProcessingDisposition::UncorrelatedDeviceFeedback,
          "uncorrelated telemetry is surfaced without mutating an operation");

    auto cameraSession=session;
    cameraSession.deviceId=id<DeviceId>("camera");
    take(runtime->openDeviceSession(cameraSession));
    const auto deviceBoundOperation=id<OperationId>("operation-device-bound");
    const auto deviceBoundStep=id<StepId>("step-device-bound");
    const auto deviceBoundCommand=id<CommandId>("command-device-bound");
    take(runtime->createOperation(deviceBoundOperation));
    take(runtime->startOperation(deviceBoundOperation));
    take(runtime->createStep(deviceBoundOperation,deviceBoundStep));
    take(runtime->dispatchCommand(
        deviceBoundOperation,deviceBoundStep,deviceBoundCommand,session));
    take(runtime->recordAdapterAccepted(deviceBoundOperation,deviceBoundStep));
    auto wrongDeviceFeedback=acknowledgement(
        cameraSession,deviceBoundOperation,deviceBoundStep,deviceBoundCommand);
    check(runtime->feedbackIngress().submit(std::move(wrongDeviceFeedback))==
          FeedbackSubmitStatus::Enqueued,
          "feedback from another current device session reaches the step identity gate");
    auto wrongDeviceResult=take(runtime->processNextFeedback());
    check(wrongDeviceResult&&wrongDeviceResult->disposition==
          FeedbackProcessingDisposition::WrongDeviceSessionForStep&&
          runtime->stepStatus(deviceBoundOperation,deviceBoundStep).value()==StepStatus::AdapterAccepted,
          "a valid camera session cannot acknowledge a command dispatched to the stage");
    check(bool(runtime->closeDeviceSession(
        id<DeviceId>("camera"),take(DeviceSessionGeneration::create(4)))),
        "unrelated device session can close without altering stage-bound work");

    const auto homeOperation=id<OperationId>("operation-home-reference");
    const auto homeStep=id<StepId>("step-home-reference");
    const auto homeCommand=id<CommandId>("command-home-reference");
    take(runtime->createOperation(homeOperation));
    take(runtime->startOperation(homeOperation));
    take(runtime->createStep(homeOperation,homeStep));
    take(runtime->dispatchCommand(homeOperation,homeStep,homeCommand,session));
    take(runtime->recordAdapterAccepted(homeOperation,homeStep));
    auto homeReceipt=homeReference(session,homeOperation,homeStep,homeCommand);
    check(runtime->feedbackIngress().submit(std::move(homeReceipt))==
          FeedbackSubmitStatus::Enqueued,
          "driver submits a complete set of command-correlated home-reference observations");
    auto homeResult=take(runtime->processNextFeedback());
    check(homeResult&&
          homeResult->disposition==FeedbackProcessingDisposition::AppliedToOperation&&
          homeResult->stepStatus==StepStatus::Verifying&&
          homeResult->receipt&&
          std::holds_alternative<StageHomeReferenceObservation>(
              homeResult->receipt->feedback().payload()),
          "explicit home-reference fact enters Core verification; generic Idle or action-completed facts do not substitute for it");
    check(runtime->recordStepVerification(homeOperation,homeStep,true).value()==
          StepStatus::Verified,
          "host evidence policy can verify the correlated home-reference step");
    auto prematureHomeFinalization=runtime->recordOperationVerification(homeOperation,false);
    check(!prematureHomeFinalization &&
          prematureHomeFinalization.error()==SerializedOperationRuntimeError::OperationIncomplete,
          "operation finalization is rejected until operation-level verification begins");
    auto homeOperationVerification=take(runtime->beginOperationVerification(homeOperation));
    check(homeOperationVerification.status()==OperationStatus::Verifying,
          "a home-only operation enters operation-level verification after its step is verified");
    auto rejectedOperation=take(runtime->recordOperationVerification(homeOperation,false));
    check(rejectedOperation.status()==OperationStatus::Failed,
          "operation-level verification failure is distinct from step verification and marks the operation Failed");

    const auto failureOperation=id<OperationId>("operation-failure");
    const auto failureStep=id<StepId>("step-failure");
    const auto failureCommand=id<CommandId>("command-failure");
    take(runtime->createOperation(failureOperation));
    take(runtime->startOperation(failureOperation));
    take(runtime->createStep(failureOperation,failureStep));
    take(runtime->dispatchCommand(failureOperation,failureStep,failureCommand,session));
    take(runtime->recordAdapterAccepted(failureOperation,failureStep));
    auto failure=adapterFailure(session,failureOperation,failureStep,failureCommand);
    check(runtime->feedbackIngress().submit(std::move(failure))==FeedbackSubmitStatus::Enqueued,
          "adapter failure is accepted as correlated runtime feedback");
    auto failureResult=take(runtime->processNextFeedback());
    check(failureResult&&failureResult->stepStatus==StepStatus::Failed&&
          failureResult->operationSnapshot&&
          failureResult->operationSnapshot->status()==OperationStatus::Failed,
          "adapter failure atomically fails its step and operation");

    std::atomic<bool> wrongThreadRejected{false};
    std::thread otherThread([&] {
        auto query=runtime->operationSnapshot(operation);
        wrongThreadRejected.store(
            !query&&query.error()==SerializedOperationRuntimeError::WrongOwnerThread);
    });
    otherThread.join();
    check(wrongThreadRejected.load(),
          "state queries from another thread are rejected by owner-thread affinity");

    const auto reconnectOperation=id<OperationId>("operation-session-reconnect");
    const auto reconnectStep=id<StepId>("step-session-reconnect");
    const auto reconnectCommand=id<CommandId>("command-session-reconnect");
    take(runtime->createOperation(reconnectOperation));
    take(runtime->startOperation(reconnectOperation));
    take(runtime->createStep(reconnectOperation,reconnectStep));
    take(runtime->dispatchCommand(reconnectOperation,reconnectStep,reconnectCommand,session));
    take(runtime->recordAdapterAccepted(reconnectOperation,reconnectStep));

    const auto replacementSession=deviceSession(5);
    check(bool(runtime->openDeviceSession(replacementSession)),
          "opening a replacement device session retires the previous session");
    check(runtime->stepStatus(reconnectOperation,reconnectStep).value()==StepStatus::Failed&&
          runtime->operationSnapshot(reconnectOperation).value().status()==OperationStatus::Failed,
          "reconnect fails active work bound to the retired device session");
    auto oldGenerationFeedback=acknowledgement(
        session,reconnectOperation,reconnectStep,reconnectCommand);
    check(runtime->feedbackIngress().submit(std::move(oldGenerationFeedback))==
          FeedbackSubmitStatus::Enqueued,
          "late feedback from the retired session reaches the runtime freshness gate");
    auto oldGenerationResult=take(runtime->processNextFeedback());
    check(oldGenerationResult&&oldGenerationResult->disposition==
          FeedbackProcessingDisposition::StaleDeviceSession,
          "retired-session callback cannot update a reconnect operation");
    auto replacementGenerationFeedback=acknowledgement(
        replacementSession,reconnectOperation,reconnectStep,reconnectCommand);
    check(runtime->feedbackIngress().submit(std::move(replacementGenerationFeedback))==
          FeedbackSubmitStatus::Enqueued,
          "replacement-session feedback reaches the operation state gate");
    auto replacementGenerationResult=take(runtime->processNextFeedback());
    check(replacementGenerationResult&&replacementGenerationResult->disposition==
          FeedbackProcessingDisposition::TerminalOperationFeedback,
          "new session cannot continue a terminal operation created on the old session");

    check(bool(runtime->closeDeviceSession(
        id<DeviceId>("stage"),take(DeviceSessionGeneration::create(5)))),
        "current replacement device session can be closed by its owner");
    check(signal.available.load()>=5&&signal.closed.load()==0,
          "runtime receives all accepted feedback wakeups without premature closure");

    TestRuntimeSignal overflowSignal;
    auto overflowRuntime=take(SerializedOperationRuntime<StageMotionFeedbackPayload>::create(
        id<MicroscopeId>("scope"),id<RuntimeId>("runtime"),
        take(RuntimeGeneration::create(8)),1,overflowSignal));
    take(overflowRuntime->bindOwnerThread());
    take(overflowRuntime->openDeviceSession(deviceSession(4,8)));
    const auto overflowOperation=id<OperationId>("operation-overflow");
    const auto overflowStep=id<StepId>("step-overflow");
    const auto overflowCommand=id<CommandId>("command-overflow");
    take(overflowRuntime->createOperation(overflowOperation));
    take(overflowRuntime->startOperation(overflowOperation));
    take(overflowRuntime->createStep(overflowOperation,overflowStep));
    auto firstTelemetry=staleSessionFeedback(deviceSession(4,8));
    auto droppedTelemetry=staleSessionFeedback(deviceSession(4,8));
    check(overflowRuntime->feedbackIngress().submit(std::move(firstTelemetry))==
          FeedbackSubmitStatus::Enqueued,
          "overflow fixture fills the bounded feedback queue");
    check(overflowRuntime->feedbackIngress().submit(std::move(droppedTelemetry))==
          FeedbackSubmitStatus::QueueFull&&overflowRuntime->feedbackLossLatched(),
          "dropped feedback latches runtime reconciliation state");
    auto blockedVerification=overflowRuntime->beginStepVerification(
        overflowOperation,overflowStep);
    check(!blockedVerification && blockedVerification.error()==
          SerializedOperationRuntimeError::FeedbackLossUnresolved,
          "Core cannot begin estimate-based verification while feedback loss is unresolved");
    auto blockedDispatch=overflowRuntime->dispatchCommand(
        overflowOperation,overflowStep,overflowCommand,deviceSession(4,8));
    check(!blockedDispatch&&blockedDispatch.error()==
          SerializedOperationRuntimeError::FeedbackLossUnresolved,
          "runtime blocks new hardware dispatch while feedback loss is unresolved");
    auto prematureAcknowledge=overflowRuntime->acknowledgeFeedbackLoss();
    check(!prematureAcknowledge&&prematureAcknowledge.error()==
          SerializedOperationRuntimeError::FeedbackLossUnresolved,
          "runtime cannot clear feedback loss while queued pre-loss feedback remains");
    auto drained=take(overflowRuntime->processNextFeedback());
    check(drained&&drained->disposition==
          FeedbackProcessingDisposition::UncorrelatedDeviceFeedback,
          "runtime drains preserved feedback before reconciliation can complete");
    check(bool(overflowRuntime->acknowledgeFeedbackLoss())&&
          !overflowRuntime->feedbackLossLatched(),
          "owner can acknowledge feedback loss after draining and reconciling");
    check(overflowRuntime->dispatchCommand(
          overflowOperation,overflowStep,overflowCommand,deviceSession(4,8)).value()==StepStatus::Dispatched,
          "command dispatch resumes after feedback loss reconciliation");

    overflowRuntime->closeFeedbackIngress();
    check(overflowRuntime->feedbackIngressClosed()&&overflowSignal.closed.load()==1,
          "host shutdown closes ingress and signals the serialized owner");
    auto lateFeedback=staleSessionFeedback(deviceSession(4,8));
    check(overflowRuntime->feedbackIngress().submit(std::move(lateFeedback))==
          FeedbackSubmitStatus::Closed,
          "late driver callback cannot reopen a closed microscope runtime");

    if(failures) {
        std::cerr<<failures<<" serialized operation runtime test(s) failed\n";
        return 1;
    }
    std::cout<<"All serialized operation runtime tests passed\n";
    return 0;
}
