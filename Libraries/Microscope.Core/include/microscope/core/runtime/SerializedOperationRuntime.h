#pragma once

#include "microscope/core/runtime/Feedback.h"
#include "microscope/core/runtime/OperationStateMachine.h"
#include "microscope/core/runtime/BoundedFeedbackInbox.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace microscope::core {

enum class SerializedOperationRuntimeError {
    ZeroFeedbackCapacity,
    OwnerThreadNotBound,
    WrongOwnerThread,
    WrongRuntimeScope,
    DeviceSessionNotOpen,
    StaleDeviceSession,
    DuplicateOperation,
    UnknownOperation,
    OperationTerminal,
    OperationIncomplete,
    StepNotVerifying,
    DuplicateStep,
    UnknownStep,
    InvalidTransition,
    SnapshotRevisionExhausted,
    FeedbackLossUnresolved
};

enum class FeedbackProcessingDisposition {
    AppliedToOperation,
    UncorrelatedDeviceFeedback,
    UnknownDeviceSession,
    StaleDeviceSession,
    StaleOperation,
    StaleStep,
    StaleCommand,
    TerminalOperationFeedback,
    StepTransitionNotAllowed,
    SnapshotRevisionExhausted
};

template <typename TPayload>
struct FeedbackProcessingResult final {
    FeedbackProcessingDisposition disposition;
    std::optional<OperationId> operationId;
    std::optional<StepId> stepId;
    std::optional<StepStatus> stepStatus;
    std::optional<OperationSnapshot> operationSnapshot;
    std::optional<FeedbackReceipt<TPayload>> receipt;
};

// Single-writer Core operation state for one microscope runtime incarnation.
// Application owns the worker/thread and binds this object to that thread.
// Device callbacks use feedbackIngress() only; they never call state reducers.
// TPayload must be a concrete capability payload or a closed std::variant of
// capability payloads selected by the host composition.
template <typename TPayload>
class SerializedOperationRuntime final {
public:
    using Pointer = std::unique_ptr<SerializedOperationRuntime>;

    ~SerializedOperationRuntime() { feedbackInbox_->close(); }

    static Result<Pointer,SerializedOperationRuntimeError> create(
        MicroscopeId microscopeId,
        RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,
        std::size_t feedbackCapacity,
        IFeedbackRuntimeSignal& runtimeSignal) {
        if(feedbackCapacity==0)
            return Result<Pointer,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::ZeroFeedbackCapacity);

        auto inbox=BoundedFeedbackInbox<TPayload>::create(
            microscopeId,runtimeId,runtimeGeneration,feedbackCapacity,runtimeSignal);
        if(!inbox)
            return Result<Pointer,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::ZeroFeedbackCapacity);

        return Result<Pointer,SerializedOperationRuntimeError>::success(
            Pointer(new SerializedOperationRuntime(
                std::move(microscopeId),std::move(runtimeId),runtimeGeneration,
                std::move(inbox).value())));
    }

    Result<bool,SerializedOperationRuntimeError> bindOwnerThread() {
        const auto current=std::this_thread::get_id();
        if(ownerThreadBound_&&ownerThread_!=current)
            return Result<bool,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::WrongOwnerThread);
        const bool newlyBound=!ownerThreadBound_;
        ownerThread_=current;
        ownerThreadBound_=true;
        return Result<bool,SerializedOperationRuntimeError>::success(newlyBound);
    }

    IFeedbackIngress<TPayload>& feedbackIngress() noexcept {
        return *feedbackInbox_;
    }

    Result<bool,SerializedOperationRuntimeError> openDeviceSession(
        RuntimeDeviceCorrelation session) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<bool,SerializedOperationRuntimeError>::failure(owner.error());
        if(session.microscopeId!=microscopeId_||
           session.runtimeId!=runtimeId_||
           session.runtimeGeneration!=runtimeGeneration_)
            return Result<bool,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::WrongRuntimeScope);

        for(auto& binding:deviceSessions_) {
            if(binding.identity().deviceId==session.deviceId) {
                binding=RuntimeDeviceSessionBinding::create(std::move(session));
                return Result<bool,SerializedOperationRuntimeError>::success(true);
            }
        }
        deviceSessions_.push_back(
            RuntimeDeviceSessionBinding::create(std::move(session)));
        return Result<bool,SerializedOperationRuntimeError>::success(true);
    }

    Result<bool,SerializedOperationRuntimeError> closeDeviceSession(
        const DeviceId& deviceId,DeviceSessionGeneration generation) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<bool,SerializedOperationRuntimeError>::failure(owner.error());

        for(auto it=deviceSessions_.begin();it!=deviceSessions_.end();++it) {
            if(it->identity().deviceId==deviceId) {
                if(it->identity().deviceSessionGeneration!=generation)
                    return Result<bool,SerializedOperationRuntimeError>::failure(
                        SerializedOperationRuntimeError::StaleDeviceSession);
                deviceSessions_.erase(it);
                return Result<bool,SerializedOperationRuntimeError>::success(true);
            }
        }
        return Result<bool,SerializedOperationRuntimeError>::failure(
            SerializedOperationRuntimeError::DeviceSessionNotOpen);
    }

    Result<OperationSnapshot,SerializedOperationRuntimeError> createOperation(
        OperationId operationId) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(owner.error());
        if(findOperation(operationId))
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::DuplicateOperation);

        OperationRecord record{std::move(operationId),OperationStatus::Accepted,1,{}};
        auto snapshot=snapshotFor(record);
        if(!snapshot)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);
        operations_.push_back(std::move(record));
        return Result<OperationSnapshot,SerializedOperationRuntimeError>::success(
            std::move(snapshot).value());
    }

    Result<OperationSnapshot,SerializedOperationRuntimeError> startOperation(
        const OperationId& operationId) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(owner.error());
        auto* operation=findOperation(operationId);
        if(!operation)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        if(!canIncrementRevision(*operation))
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);

        auto next=applyOperationLifecycleEvent(
            operation->status,OperationLifecycleEvent::Started);
        if(!next)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::InvalidTransition);
        operation->status=next.value();
        ++operation->revision;
        return snapshotFor(*operation);
    }

    Result<StepStatus,SerializedOperationRuntimeError> createStep(
        const OperationId& operationId,StepId stepId) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(owner.error());
        auto* operation=findOperation(operationId);
        if(!operation)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        if(isTerminal(operation->status))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::OperationTerminal);
        if(findStep(*operation,stepId))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::DuplicateStep);
        if(!canIncrementRevision(*operation))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);

        operation->steps.push_back(StepRecord{std::move(stepId),StepStatus::Pending,std::nullopt});
        ++operation->revision;
        return Result<StepStatus,SerializedOperationRuntimeError>::success(StepStatus::Pending);
    }

    Result<StepStatus,SerializedOperationRuntimeError> dispatchCommand(
        const OperationId& operationId,const StepId& stepId,CommandId commandId) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(owner.error());
        auto* operation=findOperation(operationId);
        if(!operation)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        if(isTerminal(operation->status))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::OperationTerminal);
        if(operation->status!=OperationStatus::Running)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::InvalidTransition);
        if(feedbackInbox_->feedbackLossLatched())
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::FeedbackLossUnresolved);
        auto* step=findStep(*operation,stepId);
        if(!step)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownStep);
        if(!canIncrementRevision(*operation))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);

        auto next=applyStepLifecycleEvent(step->status,StepLifecycleEvent::CommandDispatched);
        if(!next)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::InvalidTransition);
        step->status=next.value();
        step->commandId=std::move(commandId);
        ++operation->revision;
        return Result<StepStatus,SerializedOperationRuntimeError>::success(step->status);
    }

    Result<StepStatus,SerializedOperationRuntimeError> recordAdapterAccepted(
        const OperationId& operationId,const StepId& stepId) {
        return applyControlStepEvent(operationId,stepId,StepLifecycleEvent::AdapterAccepted);
    }

    Result<StepStatus,SerializedOperationRuntimeError> recordAdapterRejected(
        const OperationId& operationId,const StepId& stepId) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(owner.error());
        auto* operation=findOperation(operationId);
        if(!operation)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        if(isTerminal(operation->status))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::OperationTerminal);
        auto* step=findStep(*operation,stepId);
        if(!step)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownStep);
        if(!canIncrementRevision(*operation))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);

        auto failedStep=applyStepLifecycleEvent(
            step->status,StepLifecycleEvent::AdapterRejected);
        auto failedOperation=applyOperationLifecycleEvent(
            operation->status,OperationLifecycleEvent::Failed);
        if(!failedStep||!failedOperation)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::InvalidTransition);

        step->status=failedStep.value();
        operation->status=failedOperation.value();
        ++operation->revision;
        return Result<StepStatus,SerializedOperationRuntimeError>::success(step->status);
    }

    Result<StepStatus,SerializedOperationRuntimeError> recordStepVerification(
        const OperationId& operationId,const StepId& stepId,bool succeeded) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(owner.error());
        auto* operation=findOperation(operationId);
        if(!operation)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        if(isTerminal(operation->status))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::OperationTerminal);
        auto* step=findStep(*operation,stepId);
        if(!step)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownStep);
        if(step->status!=StepStatus::Verifying)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::StepNotVerifying);
        if(feedbackInbox_->feedbackLossLatched())
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::FeedbackLossUnresolved);
        if(!canIncrementRevision(*operation))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);

        const auto event=succeeded
            ?StepLifecycleEvent::VerificationSucceeded
            :StepLifecycleEvent::VerificationFailed;
        auto next=applyStepLifecycleEvent(step->status,event);
        if(!next)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::InvalidTransition);

        std::optional<OperationStatus> nextOperationStatus;
        if(!succeeded) {
            bool hasVerifiedStep=false;
            for(const auto& candidate:operation->steps)
                if(candidate.status==StepStatus::Verified)hasVerifiedStep=true;
            const bool partial=hasVerifiedStep&&operation->status!=OperationStatus::Accepted;
            auto terminal=applyOperationLifecycleEvent(
                operation->status,
                partial?OperationLifecycleEvent::PartiallyCompleted:
                        OperationLifecycleEvent::Failed);
            if(!terminal)
                return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                    SerializedOperationRuntimeError::InvalidTransition);
            nextOperationStatus=terminal.value();
        }

        step->status=next.value();
        if(nextOperationStatus)
            operation->status=*nextOperationStatus;
        ++operation->revision;
        return Result<StepStatus,SerializedOperationRuntimeError>::success(step->status);
    }

    Result<OperationSnapshot,SerializedOperationRuntimeError> beginOperationVerification(
        const OperationId& operationId) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(owner.error());
        auto* operation=findOperation(operationId);
        if(!operation)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        if(isTerminal(operation->status))
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::OperationTerminal);
        if(feedbackInbox_->feedbackLossLatched())
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::FeedbackLossUnresolved);
        if(operation->steps.empty())
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::OperationIncomplete);
        for(const auto& step:operation->steps)
            if(step.status!=StepStatus::Verified)
                return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                    SerializedOperationRuntimeError::OperationIncomplete);
        if(!canIncrementRevision(*operation))
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);
        auto verifying=applyOperationLifecycleEvent(
            operation->status,OperationLifecycleEvent::VerificationStarted);
        if(!verifying)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::InvalidTransition);
        operation->status=verifying.value();
        ++operation->revision;
        return snapshotFor(*operation);
    }

    Result<OperationSnapshot,SerializedOperationRuntimeError> operationSnapshot(
        const OperationId& operationId) const {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(owner.error());
        const auto* operation=findOperation(operationId);
        if(!operation)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        return snapshotFor(*operation);
    }

    Result<StepStatus,SerializedOperationRuntimeError> stepStatus(
        const OperationId& operationId,const StepId& stepId) const {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(owner.error());
        const auto* operation=findOperation(operationId);
        if(!operation)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        const auto* step=findStep(*operation,stepId);
        if(!step)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownStep);
        return Result<StepStatus,SerializedOperationRuntimeError>::success(step->status);
    }

    Result<std::optional<FeedbackProcessingResult<TPayload>>,SerializedOperationRuntimeError>
    processNextFeedback() {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::failure(owner.error());

        auto receipt=feedbackInbox_->tryPop();
        if(!receipt)
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::nullopt);

        FeedbackProcessingResult<TPayload> result{
            FeedbackProcessingDisposition::UncorrelatedDeviceFeedback,
            std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt};

        const auto& metadata=receipt->feedback().metadata();
        const auto& device=metadata.correlation().runtimeDevice;
        const auto* session=findDeviceSession(device.deviceId);
        if(!session) {
            result.disposition=FeedbackProcessingDisposition::UnknownDeviceSession;
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }
        if(!session->matches(device)) {
            result.disposition=FeedbackProcessingDisposition::StaleDeviceSession;
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }

        const auto& correlation=metadata.correlation().operation;
        if(!correlation) {
            result.disposition=FeedbackProcessingDisposition::UncorrelatedDeviceFeedback;
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }

        result.operationId=correlation->operationId;
        result.stepId=correlation->stepId;
        auto* operation=findOperation(correlation->operationId);
        if(!operation) {
            result.disposition=FeedbackProcessingDisposition::StaleOperation;
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }
        auto* step=findStep(*operation,correlation->stepId);
        if(isTerminal(operation->status)) {
            result.disposition=FeedbackProcessingDisposition::TerminalOperationFeedback;
            result.operationSnapshot=snapshotFor(*operation).value();
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }
        if(!step) {
            result.disposition=FeedbackProcessingDisposition::StaleStep;
            result.operationSnapshot=snapshotFor(*operation).value();
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }
        if(!ActiveOperationStepBinding::create(
                OperationCorrelation{operation->id,step->id,step->commandId}).matches(*correlation)) {
            result.disposition=FeedbackProcessingDisposition::StaleCommand;
            result.stepStatus=step->status;
            result.operationSnapshot=snapshotFor(*operation).value();
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }

        const auto lifecycleEvent=stepEvent(metadata.kind());
        auto nextStep=applyStepLifecycleEvent(step->status,lifecycleEvent);
        if(!nextStep) {
            result.disposition=FeedbackProcessingDisposition::StepTransitionNotAllowed;
            result.stepStatus=step->status;
            result.operationSnapshot=snapshotFor(*operation).value();
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }

        std::optional<OperationStatus> nextOperationStatus;
        if(metadata.kind()==FeedbackKind::AdapterFailure) {
            auto failed=applyOperationLifecycleEvent(
                operation->status,OperationLifecycleEvent::Failed);
            if(failed)
                nextOperationStatus=failed.value();
        }
        if(!canIncrementRevision(*operation)) {
            result.disposition=FeedbackProcessingDisposition::SnapshotRevisionExhausted;
            result.stepStatus=step->status;
            result.operationSnapshot=snapshotFor(*operation).value();
            result.receipt=std::move(receipt);
            return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                          SerializedOperationRuntimeError>::success(std::move(result));
        }

        step->status=nextStep.value();
        if(nextOperationStatus)
            operation->status=*nextOperationStatus;
        ++operation->revision;

        result.disposition=FeedbackProcessingDisposition::AppliedToOperation;
        result.stepStatus=step->status;
        result.operationSnapshot=snapshotFor(*operation).value();
        result.receipt=std::move(receipt);
        return Result<std::optional<FeedbackProcessingResult<TPayload>>,
                      SerializedOperationRuntimeError>::success(std::move(result));
    }

    bool feedbackLossLatched() const {
        return feedbackInbox_->feedbackLossLatched();
    }

    Result<bool,SerializedOperationRuntimeError> acknowledgeFeedbackLoss() {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<bool,SerializedOperationRuntimeError>::failure(owner.error());
        if(feedbackInbox_->pendingCount()!=0)
            return Result<bool,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::FeedbackLossUnresolved);
        feedbackInbox_->acknowledgeFeedbackLoss();
        return Result<bool,SerializedOperationRuntimeError>::success(true);
    }

private:
    struct StepRecord final {
        StepId id;
        StepStatus status;
        std::optional<CommandId> commandId;
    };
    struct OperationRecord final {
        OperationId id;
        OperationStatus status;
        std::uint64_t revision;
        std::vector<StepRecord> steps;
    };

    SerializedOperationRuntime(
        MicroscopeId microscopeId,RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,
        std::unique_ptr<BoundedFeedbackInbox<TPayload>> feedbackInbox)
        :microscopeId_(std::move(microscopeId)),
         runtimeId_(std::move(runtimeId)),
         runtimeGeneration_(runtimeGeneration),
         feedbackInbox_(std::move(feedbackInbox)) {}

    Result<bool,SerializedOperationRuntimeError> requireOwnerThread() const {
        if(!ownerThreadBound_)
            return Result<bool,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::OwnerThreadNotBound);
        if(ownerThread_!=std::this_thread::get_id())
            return Result<bool,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::WrongOwnerThread);
        return Result<bool,SerializedOperationRuntimeError>::success(true);
    }

    static bool isTerminal(OperationStatus status) noexcept {
        return status==OperationStatus::Succeeded||
               status==OperationStatus::Failed||
               status==OperationStatus::PartiallyCompleted||
               status==OperationStatus::Cancelled;
    }

    static bool canIncrementRevision(const OperationRecord& operation) noexcept {
        return operation.revision!=std::numeric_limits<std::uint64_t>::max();
    }

    Result<OperationSnapshot,SerializedOperationRuntimeError> snapshotFor(
        const OperationRecord& operation) const {
        auto snapshot=OperationSnapshot::create(
            microscopeId_,runtimeId_,runtimeGeneration_,operation.id,
            operation.status,operation.revision);
        if(!snapshot)
            return Result<OperationSnapshot,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);
        return Result<OperationSnapshot,SerializedOperationRuntimeError>::success(
            std::move(snapshot).value());
    }

    OperationRecord* findOperation(const OperationId& id) {
        for(auto& operation:operations_)
            if(operation.id==id)return &operation;
        return nullptr;
    }
    const OperationRecord* findOperation(const OperationId& id) const {
        for(const auto& operation:operations_)
            if(operation.id==id)return &operation;
        return nullptr;
    }
    StepRecord* findStep(OperationRecord& operation,const StepId& id) {
        for(auto& step:operation.steps)
            if(step.id==id)return &step;
        return nullptr;
    }
    const StepRecord* findStep(const OperationRecord& operation,const StepId& id) const {
        for(const auto& step:operation.steps)
            if(step.id==id)return &step;
        return nullptr;
    }
    const RuntimeDeviceSessionBinding* findDeviceSession(const DeviceId& id) const {
        for(const auto& session:deviceSessions_)
            if(session.identity().deviceId==id)return &session;
        return nullptr;
    }

    Result<StepStatus,SerializedOperationRuntimeError> applyControlStepEvent(
        const OperationId& operationId,const StepId& stepId,StepLifecycleEvent event) {
        auto owner=requireOwnerThread();
        if(!owner)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(owner.error());
        auto* operation=findOperation(operationId);
        if(!operation)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownOperation);
        if(isTerminal(operation->status))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::OperationTerminal);
        auto* step=findStep(*operation,stepId);
        if(!step)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::UnknownStep);
        if(!canIncrementRevision(*operation))
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::SnapshotRevisionExhausted);
        auto next=applyStepLifecycleEvent(step->status,event);
        if(!next)
            return Result<StepStatus,SerializedOperationRuntimeError>::failure(
                SerializedOperationRuntimeError::InvalidTransition);
        step->status=next.value();
        ++operation->revision;
        return Result<StepStatus,SerializedOperationRuntimeError>::success(step->status);
    }

    static StepLifecycleEvent stepEvent(FeedbackKind kind) noexcept {
        switch(kind) {
        case FeedbackKind::DeviceAcknowledged:return StepLifecycleEvent::DeviceAcknowledged;
        case FeedbackKind::DeviceProgress:return StepLifecycleEvent::DeviceProgress;
        case FeedbackKind::DeviceActionCompleted:return StepLifecycleEvent::DeviceActionCompleted;
        case FeedbackKind::DeviceStateObserved:return StepLifecycleEvent::DeviceStateObserved;
        case FeedbackKind::AdapterFailure:return StepLifecycleEvent::AdapterFailure;
        }
        return StepLifecycleEvent::AdapterFailure;
    }

    MicroscopeId microscopeId_;
    RuntimeId runtimeId_;
    RuntimeGeneration runtimeGeneration_;
    std::unique_ptr<BoundedFeedbackInbox<TPayload>> feedbackInbox_;
    std::vector<RuntimeDeviceSessionBinding> deviceSessions_;
    std::vector<OperationRecord> operations_;
    std::thread::id ownerThread_{};
    bool ownerThreadBound_{false};
};

} // namespace microscope::core
