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
    const OperationId& operation,const StepId& step) {
    const auto axes=take(StageAxisSet::create({
        id<StageAxisId>("x"),id<StageAxisId>("y")}));
    const auto payload=take(StageMotionActionCompletion::create(
        StageMotionAction::MoveAxesTo,axes));
    const auto metadata=take(FeedbackMetadata::create(
        FeedbackCorrelation{device,OperationCorrelation{operation,step,std::nullopt}},
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

    auto completion=actionCompleted(session,operation,step);
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
