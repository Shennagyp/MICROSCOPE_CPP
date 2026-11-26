#pragma once

// LEGACY COMPATIBILITY PATH.
// This runtime is bound to DualSlideCarrierState and its fixed left/right slots.
// It remains for existing integrations and regression coverage only. Do not use
// it for new stage navigation. New navigation must use
// StageWorkingSurfaceNavigationRuntime with StageWorkingSurface2D, which
// supports an empty canvas and an arbitrary number of independently drawn slides.

#include "microscope/application/motion/StageNavigationExecutor.h"
#include "microscope/application/motion/StagePositionCommandReducer.h"
#include "microscope/application/motion/StagePositionFeedbackReducer.h"

#include <cmath>
#include <optional>
#include <thread>
#include <utility>

namespace microscope::application::motion {

// Open-loop drivers can verify a bounded Core-derived estimate after an action
// completes. Controllers with position readback can require fresh, command-
// correlated X/Y observations before the same navigation operation is verified.
enum class StageNavigationPositionVerificationMode {
    BoundedCommandEstimate,
    DevicePositionObservations
};

enum class StageNavigationVerificationPolicyError {
    InvalidHorizontalTolerance,
    InvalidTransverseTolerance,
    InvalidHorizontalHomeUncertainty,
    InvalidTransverseHomeUncertainty,
    InvalidMode
};

class StageNavigationVerificationPolicy final {
public:
    [[nodiscard]] static core::Result<
        StageNavigationVerificationPolicy,
        StageNavigationVerificationPolicyError> create(
            double horizontalTargetToleranceMicrometers,
            double transverseTargetToleranceMicrometers,
            double horizontalHomeUncertaintyMicrometers,
            double transverseHomeUncertaintyMicrometers,
            StageNavigationPositionVerificationMode mode) {
        if (!std::isfinite(horizontalTargetToleranceMicrometers) ||
            horizontalTargetToleranceMicrometers <= 0.0) {
            return core::Result<StageNavigationVerificationPolicy,
                StageNavigationVerificationPolicyError>::failure(
                    StageNavigationVerificationPolicyError::InvalidHorizontalTolerance);
        }
        if (!std::isfinite(transverseTargetToleranceMicrometers) ||
            transverseTargetToleranceMicrometers <= 0.0) {
            return core::Result<StageNavigationVerificationPolicy,
                StageNavigationVerificationPolicyError>::failure(
                    StageNavigationVerificationPolicyError::InvalidTransverseTolerance);
        }
        if (!std::isfinite(horizontalHomeUncertaintyMicrometers) ||
            horizontalHomeUncertaintyMicrometers < 0.0) {
            return core::Result<StageNavigationVerificationPolicy,
                StageNavigationVerificationPolicyError>::failure(
                    StageNavigationVerificationPolicyError::InvalidHorizontalHomeUncertainty);
        }
        if (!std::isfinite(transverseHomeUncertaintyMicrometers) ||
            transverseHomeUncertaintyMicrometers < 0.0) {
            return core::Result<StageNavigationVerificationPolicy,
                StageNavigationVerificationPolicyError>::failure(
                    StageNavigationVerificationPolicyError::InvalidTransverseHomeUncertainty);
        }
        if (mode != StageNavigationPositionVerificationMode::BoundedCommandEstimate &&
            mode != StageNavigationPositionVerificationMode::DevicePositionObservations) {
            return core::Result<StageNavigationVerificationPolicy,
                StageNavigationVerificationPolicyError>::failure(
                    StageNavigationVerificationPolicyError::InvalidMode);
        }

        return core::Result<StageNavigationVerificationPolicy,
            StageNavigationVerificationPolicyError>::success(
                StageNavigationVerificationPolicy(
                    horizontalTargetToleranceMicrometers,
                    transverseTargetToleranceMicrometers,
                    horizontalHomeUncertaintyMicrometers,
                    transverseHomeUncertaintyMicrometers,
                    mode));
    }

    [[nodiscard]] double horizontalTargetToleranceMicrometers() const noexcept {
        return horizontalTargetToleranceMicrometers_;
    }
    [[nodiscard]] double transverseTargetToleranceMicrometers() const noexcept {
        return transverseTargetToleranceMicrometers_;
    }
    [[nodiscard]] double horizontalHomeUncertaintyMicrometers() const noexcept {
        return horizontalHomeUncertaintyMicrometers_;
    }
    [[nodiscard]] double transverseHomeUncertaintyMicrometers() const noexcept {
        return transverseHomeUncertaintyMicrometers_;
    }
    [[nodiscard]] StageNavigationPositionVerificationMode mode() const noexcept {
        return mode_;
    }

private:
    StageNavigationVerificationPolicy(
        double horizontalTolerance,
        double transverseTolerance,
        double horizontalHomeUncertainty,
        double transverseHomeUncertainty,
        StageNavigationPositionVerificationMode mode)
        : horizontalTargetToleranceMicrometers_(horizontalTolerance),
          transverseTargetToleranceMicrometers_(transverseTolerance),
          horizontalHomeUncertaintyMicrometers_(horizontalHomeUncertainty),
          transverseHomeUncertaintyMicrometers_(transverseHomeUncertainty),
          mode_(mode) {}

    double horizontalTargetToleranceMicrometers_;
    double transverseTargetToleranceMicrometers_;
    double horizontalHomeUncertaintyMicrometers_;
    double transverseHomeUncertaintyMicrometers_;
    StageNavigationPositionVerificationMode mode_;
};

// IDs are allocated by the Application boundary. The continuation IDs are
// consumed only when the initial plan requires homing before the restored move.
struct StageNavigationOperationIdentity final {
    core::OperationId operationId;
    core::StepId initialStepId;
    core::CommandId initialCommandId;
    core::StepId restoredMoveStepId;
    core::CommandId restoredMoveCommandId;
};

enum class StageNavigationRuntimeDisposition {
    MoveDispatched,
    HomingDispatched,
    RestoredMoveDispatched,
    AwaitingPositionFeedback,
    NavigationCompleted,
    NavigationFailed,
    SubmissionIndeterminate,
    FeedbackLossDetected,
    PositionUpdated,
    IgnoredFeedback
};

struct StageNavigationRuntimeUpdate final {
    StageNavigationRuntimeDisposition disposition;
    std::optional<core::OperationSnapshot> operation;
};

enum class StageNavigationRuntimeErrorCode {
    StagePositionMismatch,
    StageAxisMismatch,
    StageUnitMismatch,
    ErrorBudgetMismatch,
    VerificationPolicyMismatch,
    OwnerThreadNotBound,
    WrongOwnerThread,
    NavigationAlreadyActive,
    OperationNotRunning,
    InitialStepNotPending,
    PlanningRejected,
    ExecutionRejected,
    DispatchRejected,
    FeedbackLossUnresolved,
    PositionFeedbackRejected,
    CommandReductionRejected,
    HomeReferenceRejected,
    StepVerificationRejected,
    OperationCompletionRejected,
    CarrierReplacementDuringNavigation
};

struct StageNavigationRuntimeError final {
    StageNavigationRuntimeErrorCode code;
    std::optional<core::StageNavigationPlanningError> planningError;
    std::optional<StageNavigationExecutionErrorCode> executionError;
    std::optional<StagePositionCommandReductionErrorCode> commandReductionError;
    std::optional<StagePositionFeedbackReducerErrorCode> feedbackReductionError;
    std::optional<core::StageAxisPositionStateError> positionError;
    std::optional<core::SerializedOperationRuntimeError> runtimeError;
};

// One owner-thread stage subsystem for one microscope runtime. The host invokes
// beginNavigation() from its typed control-request drain and processFeedback()
// from SerializedRuntimeWorker's feedback handler. Neither method is callable
// concurrently; the serialized worker is the state owner.
//
// The caller creates and starts the operation and its initial pending step.
// This class owns the carrier/position state and creates the continuation step
// only after the complete home-reference fact has been verified.
class StageNavigationRuntime final {
public:
    using Runtime =
        core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>;
    using FeedbackResult =
        core::FeedbackProcessingResult<core::StageMotionFeedbackPayload>;
    using Result = core::Result<
        StageNavigationRuntimeUpdate,
        StageNavigationRuntimeError>;

    [[nodiscard]] static core::Result<
        StageNavigationRuntime,
        StageNavigationRuntimeError> create(
            core::DualSlideCarrierState carrier,
            core::StagePositionState position,
            core::StageHomingPolicy homingPolicy,
            StageMotionErrorBudget errorBudget,
            StageNavigationVerificationPolicy verificationPolicy,
            core::RuntimeDeviceCorrelation runtimeDevice,
            core::IStageMotionPort& port,
            Runtime& operationRuntime) {
        const auto& specification = carrier.specification();
        if (position.stageComponentId() != specification.stageComponentId()) {
            return creationFailure(StageNavigationRuntimeErrorCode::StagePositionMismatch);
        }
        if (position.horizontal().axisId() != specification.horizontalAxisId() ||
            position.transverse().axisId() != specification.transverseAxisId()) {
            return creationFailure(StageNavigationRuntimeErrorCode::StageAxisMismatch);
        }
        if (position.horizontal().unit() != specification.horizontalUnit() ||
            position.transverse().unit() != specification.transverseUnit()) {
            return creationFailure(StageNavigationRuntimeErrorCode::StageUnitMismatch);
        }

        const auto* horizontalBudget =
            errorBudget.contributionFor(specification.horizontalAxisId());
        const auto* transverseBudget =
            errorBudget.contributionFor(specification.transverseAxisId());
        if (errorBudget.contributions().size() != 2 ||
            horizontalBudget == nullptr ||
            transverseBudget == nullptr ||
            horizontalBudget->unit() != specification.horizontalUnit() ||
            transverseBudget->unit() != specification.transverseUnit()) {
            return creationFailure(StageNavigationRuntimeErrorCode::ErrorBudgetMismatch);
        }

        if (verificationPolicy.mode() !=
                StageNavigationPositionVerificationMode::BoundedCommandEstimate &&
            verificationPolicy.mode() !=
                StageNavigationPositionVerificationMode::DevicePositionObservations) {
            return creationFailure(StageNavigationRuntimeErrorCode::VerificationPolicyMismatch);
        }

        return core::Result<StageNavigationRuntime, StageNavigationRuntimeError>::success(
            StageNavigationRuntime(
                std::move(carrier), std::move(position), std::move(homingPolicy),
                std::move(errorBudget), std::move(verificationPolicy),
                std::move(runtimeDevice), port, operationRuntime));
    }

    StageNavigationRuntime(const StageNavigationRuntime&) = delete;
    StageNavigationRuntime& operator=(const StageNavigationRuntime&) = delete;
    StageNavigationRuntime(StageNavigationRuntime&&) = default;
    StageNavigationRuntime& operator=(StageNavigationRuntime&&) = default;

    // Call from the same host-owned worker thread after
    // SerializedRuntimeWorker has bound the Core operation runtime.
    [[nodiscard]] core::Result<bool, StageNavigationRuntimeError> bindOwnerThread() {
        const auto current = std::this_thread::get_id();
        if (ownerThreadBound_ && ownerThread_ != current) {
            return core::Result<bool, StageNavigationRuntimeError>::failure(
                makeError(StageNavigationRuntimeErrorCode::WrongOwnerThread));
        }
        auto runtimeBinding = operationRuntime_->bindOwnerThread();
        if (!runtimeBinding) {
            auto error = makeError(StageNavigationRuntimeErrorCode::WrongOwnerThread);
            error.runtimeError = runtimeBinding.error();
            return core::Result<bool, StageNavigationRuntimeError>::failure(
                std::move(error));
        }
        const bool newlyBound = !ownerThreadBound_;
        ownerThread_ = current;
        ownerThreadBound_ = true;
        return core::Result<bool, StageNavigationRuntimeError>::success(newlyBound);
    }

    // Application-control entry point. It validates the requested slide
    // target before allocating operation state, then creates the operation and
    // its first Pending step on the serialized owner thread.
    [[nodiscard]] Result beginRequest(
        const core::SlideId& slideId,
        core::SlideLocalPoint destination,
        StageNavigationOperationIdentity identities) {
        auto owner = requireOwnerThread();
        if (!owner) {
            return Result::failure(owner.error());
        }
        if (active_) {
            return failure(StageNavigationRuntimeErrorCode::NavigationAlreadyActive);
        }
        if (operationRuntime_->feedbackLossLatched()) {
            position_ = position_.invalidateMachineReference();
            feedbackLossObserved_ = true;
            return failure(StageNavigationRuntimeErrorCode::FeedbackLossUnresolved);
        }

        auto plan = core::StageNavigationPlanner::planToSlidePoint(
            carrier_, slideId, destination, position_, homingPolicy_);
        if (!plan) {
            return failure(
                StageNavigationRuntimeErrorCode::PlanningRejected,
                plan.error());
        }

        auto created = operationRuntime_->createOperation(identities.operationId);
        if (!created) {
            return failure(
                StageNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, created.error());
        }
        auto started = operationRuntime_->startOperation(identities.operationId);
        if (!started) {
            return failure(
                StageNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, started.error());
        }
        auto step = operationRuntime_->createStep(
            identities.operationId, identities.initialStepId);
        if (!step) {
            return failure(
                StageNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, step.error());
        }

        return beginNavigation(slideId, destination, std::move(identities));
    }

    // operationId must already be Running and initialStepId must already be
    // Pending in the supplied serialized operation runtime.
    [[nodiscard]] Result beginNavigation(
        const core::SlideId& slideId,
        core::SlideLocalPoint destination,
        StageNavigationOperationIdentity identities) {
        auto owner = requireOwnerThread();
        if (!owner) {
            return Result::failure(owner.error());
        }
        if (active_) {
            return failure(StageNavigationRuntimeErrorCode::NavigationAlreadyActive);
        }
        if (operationRuntime_->feedbackLossLatched()) {
            position_ = position_.invalidateMachineReference();
            feedbackLossObserved_ = true;
            return failure(StageNavigationRuntimeErrorCode::FeedbackLossUnresolved);
        }
        if (identities.initialStepId == identities.restoredMoveStepId ||
            identities.initialCommandId == identities.restoredMoveCommandId) {
            return failure(StageNavigationRuntimeErrorCode::ExecutionRejected);
        }

        auto operation = operationRuntime_->operationSnapshot(identities.operationId);
        if (!operation || operation.value().status() != core::OperationStatus::Running) {
            return failure(
                StageNavigationRuntimeErrorCode::OperationNotRunning,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt,
                operation ? std::nullopt :
                    std::optional<core::SerializedOperationRuntimeError>(operation.error()));
        }
        auto step = operationRuntime_->stepStatus(
            identities.operationId, identities.initialStepId);
        if (!step || step.value() != core::StepStatus::Pending) {
            return failure(
                StageNavigationRuntimeErrorCode::InitialStepNotPending,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt,
                step ? std::nullopt :
                    std::optional<core::SerializedOperationRuntimeError>(step.error()));
        }

        auto execution = StageNavigationExecutor::begin(
            carrier_, slideId, destination, position_, homingPolicy_, runtimeDevice_,
            identities.operationId, identities.initialStepId,
            identities.initialCommandId, *port_, *operationRuntime_);
        if (!execution) {
            return failure(
                StageNavigationRuntimeErrorCode::ExecutionRejected,
                execution.error().planningError,
                execution.error().code, std::nullopt, std::nullopt,
                std::nullopt, execution.error().runtimeError);
        }

        active_.emplace(ActiveNavigation{
            std::move(execution).value(), std::move(identities),
            false, std::nullopt, std::nullopt, std::nullopt});

        const auto dispatch = inspectDispatch(active_->execution);
        if (dispatch == DispatchDisposition::Accepted) {
            return success(std::holds_alternative<core::StageHomeThenMovePlan>(
                               active_->execution.plan)
                    ? StageNavigationRuntimeDisposition::HomingDispatched
                    : StageNavigationRuntimeDisposition::MoveDispatched);
        }
        if (dispatch == DispatchDisposition::Rejected) {
            const auto* dispatchError =
                std::get_if<StageMotionDispatchError>(&active_->execution.dispatch);
            if (dispatchError != nullptr &&
                dispatchError->code == StageMotionDispatchErrorCode::RuntimeDispatchRejected) {
                auto failed = operationRuntime_->recordPreDispatchFailure(
                    active_->identities.operationId,
                    active_->execution.commandContext.operation().stepId);
                if (!failed) {
                    return failure(
                        StageNavigationRuntimeErrorCode::DispatchRejected,
                        std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                        std::nullopt, failed.error());
                }
            }
            const auto snapshot = operationRuntime_->operationSnapshot(
                active_->identities.operationId);
            active_.reset();
            return success(StageNavigationRuntimeDisposition::NavigationFailed,
                           snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                    : std::nullopt);
        }

        // The adapter may have received the command before the submission call
        // failed. Keep the correlation context and invalidate position instead
        // of claiming that the stage stayed still.
        position_ = position_.invalidateMachineReference();
        return success(StageNavigationRuntimeDisposition::SubmissionIndeterminate);
    }

    [[nodiscard]] Result processFeedback(const FeedbackResult& feedback) {
        // The first feedback callback may be the first stage-runtime callback
        // on the worker thread. Bind here when necessary; SerializedOperationRuntime
        // is already bound by SerializedRuntimeWorker before it invokes handlers.
        auto owner = ownerThreadBound_ ? requireOwnerThread() : bindOwnerThread();
        if (!owner) {
            return Result::failure(owner.error());
        }

        if (operationRuntime_->feedbackLossLatched()) {
            return synchronizeFeedbackLoss();
        }
        if (!feedback.receipt) {
            return success(StageNavigationRuntimeDisposition::IgnoredFeedback);
        }

        const auto& envelope = feedback.receipt->feedback();
        const auto& metadata = envelope.metadata();
        if (!sameDevice(metadata.correlation().runtimeDevice, runtimeDevice_)) {
            return success(StageNavigationRuntimeDisposition::IgnoredFeedback);
        }

        if (active_ &&
            feedback.disposition == core::FeedbackProcessingDisposition::AppliedToOperation &&
            !matchesCommand(feedback, active_->execution.commandContext)) {
            return success(StageNavigationRuntimeDisposition::IgnoredFeedback);
        }

        if (metadata.kind() == core::FeedbackKind::AdapterFailure &&
            active_ && matchesCommand(feedback, active_->execution.commandContext)) {
            position_ = position_.invalidateMachineReference();
            const auto snapshot =
                operationRuntime_->operationSnapshot(active_->identities.operationId);
            active_.reset();
            return success(StageNavigationRuntimeDisposition::NavigationFailed,
                           snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                    : std::nullopt);
        }

        auto positionReduction = StagePositionFeedbackReducer::reduce(
            position_, feedback, false);
        if (!positionReduction) {
            return failure(
                StageNavigationRuntimeErrorCode::PositionFeedbackRejected,
                std::nullopt, std::nullopt, std::nullopt,
                positionReduction.error().code,
                positionReduction.error().positionStateError,
                positionReduction.error().runtimeError);
        }
        position_ = std::move(positionReduction).value().position;

        if (!active_) {
            return success(StageNavigationRuntimeDisposition::PositionUpdated);
        }

        if (metadata.kind() == core::FeedbackKind::HomeReferenceEstablished &&
            matchesCommand(feedback, active_->execution.commandContext)) {
            return processHomeReference(feedback);
        }

        if (metadata.kind() == core::FeedbackKind::DeviceActionCompleted &&
            matchesCommand(feedback, active_->execution.commandContext)) {
            return processMoveCompletion(feedback);
        }

        if (metadata.kind() == core::FeedbackKind::DeviceStateObserved &&
            matchesCommand(feedback, active_->execution.commandContext)) {
            const auto* observation =
                std::get_if<core::AxisPositionObservation>(&envelope.payload());
            if (observation != nullptr) {
                if (observation->position().axisId() == carrier_.specification().horizontalAxisId()) {
                    active_->horizontalObservation = CorrelatedAxisObservation{
                        *observation, metadata.source()};
                } else if (observation->position().axisId() ==
                           carrier_.specification().transverseAxisId()) {
                    active_->transverseObservation = CorrelatedAxisObservation{
                        *observation, metadata.source()};
                }
                if (active_->commandCompleted &&
                    verificationPolicy_.mode() ==
                        StageNavigationPositionVerificationMode::DevicePositionObservations) {
                    return tryCompleteMove();
                }
            }
        }

        return success(StageNavigationRuntimeDisposition::AwaitingPositionFeedback);
    }

    // Called from the SerializedRuntimeWorker control drain as well as from
    // feedback handling. Queue overflow can occur without delivering a receipt,
    // so the owner must observe the independent feedback-loss latch explicitly.
    [[nodiscard]] Result synchronizeFeedbackLoss() {
        auto owner = ownerThreadBound_ ? requireOwnerThread() : bindOwnerThread();
        if (!owner) {
            return Result::failure(owner.error());
        }
        if (operationRuntime_->feedbackLossLatched()) {
            if (!feedbackLossObserved_) {
                position_ = position_.invalidateMachineReference();
                feedbackLossObserved_ = true;
                return success(StageNavigationRuntimeDisposition::FeedbackLossDetected);
            }
            return success(StageNavigationRuntimeDisposition::IgnoredFeedback);
        }
        feedbackLossObserved_ = false;
        return success(StageNavigationRuntimeDisposition::PositionUpdated);
    }

    [[nodiscard]] bool feedbackLossUnresolved() const noexcept {
        return operationRuntime_->feedbackLossLatched();
    }

    // Carrier occupancy and registrations may change only between navigation
    // operations. A new registration revision automatically invalidates any
    // previously captured SlideNavigationTarget.
    [[nodiscard]] core::Result<bool, StageNavigationRuntimeError> replaceCarrier(
        core::DualSlideCarrierState carrier) {
        auto owner = requireOwnerThread();
        if (!owner) {
            return core::Result<bool, StageNavigationRuntimeError>::failure(owner.error());
        }
        if (active_) {
            return core::Result<bool, StageNavigationRuntimeError>::failure(
                makeError(StageNavigationRuntimeErrorCode::CarrierReplacementDuringNavigation));
        }
        if (carrier.specification().stageComponentId() != position_.stageComponentId() ||
            carrier.specification().horizontalAxisId() != position_.horizontal().axisId() ||
            carrier.specification().transverseAxisId() != position_.transverse().axisId() ||
            carrier.specification().horizontalUnit() != position_.horizontal().unit() ||
            carrier.specification().transverseUnit() != position_.transverse().unit()) {
            return core::Result<bool, StageNavigationRuntimeError>::failure(
                makeError(StageNavigationRuntimeErrorCode::StagePositionMismatch));
        }
        carrier_ = std::move(carrier);
        return core::Result<bool, StageNavigationRuntimeError>::success(true);
    }

    // Read this value on the serialized owner thread. Front ends should consume
    // immutable Application snapshots rather than retain this reference.
    [[nodiscard]] const core::StagePositionState& position() const noexcept {
        return position_;
    }

    [[nodiscard]] const core::DualSlideCarrierState& carrier() const noexcept {
        return carrier_;
    }

    // Returns the selected physical slide's current local coordinates through
    // its own registration. Unknown position/error or an out-of-slide mapping
    // remains an explicit error.
    [[nodiscard]] core::Result<
        core::SlidePositionEstimate,
        core::CurrentSlidePositionError> currentSlidePosition(
            const core::SlideId& slideId) const {
        return position_.currentSlidePosition(carrier_, slideId);
    }

    [[nodiscard]] bool navigationActive() const noexcept {
        return active_.has_value();
    }

private:
    struct CorrelatedAxisObservation final {
        core::AxisPositionObservation observation;
        core::ObservationSource source;
    };

    struct ActiveNavigation final {
        StageNavigationExecution execution;
        StageNavigationOperationIdentity identities;
        bool commandCompleted;
        std::optional<core::StagePositionState> commandEstimate;
        std::optional<CorrelatedAxisObservation> horizontalObservation;
        std::optional<CorrelatedAxisObservation> transverseObservation;
    };

    enum class DispatchDisposition { Accepted, Rejected, Indeterminate };

    StageNavigationRuntime(
        core::DualSlideCarrierState carrier,
        core::StagePositionState position,
        core::StageHomingPolicy homingPolicy,
        StageMotionErrorBudget errorBudget,
        StageNavigationVerificationPolicy verificationPolicy,
        core::RuntimeDeviceCorrelation runtimeDevice,
        core::IStageMotionPort& port,
        Runtime& operationRuntime)
        : carrier_(std::move(carrier)),
          position_(std::move(position)),
          homingPolicy_(std::move(homingPolicy)),
          errorBudget_(std::move(errorBudget)),
          verificationPolicy_(std::move(verificationPolicy)),
          runtimeDevice_(std::move(runtimeDevice)),
          port_(&port),
          operationRuntime_(&operationRuntime) {}

    [[nodiscard]] static StageNavigationRuntimeError makeError(
        StageNavigationRuntimeErrorCode code) {
        return StageNavigationRuntimeError{
            code, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt};
    }

    [[nodiscard]] static core::Result<
        StageNavigationRuntime,
        StageNavigationRuntimeError> creationFailure(
            StageNavigationRuntimeErrorCode code) {
        return core::Result<StageNavigationRuntime, StageNavigationRuntimeError>::failure(
            makeError(code));
    }

    [[nodiscard]] static Result failure(
        StageNavigationRuntimeErrorCode code,
        std::optional<core::StageNavigationPlanningError> planning = std::nullopt,
        std::optional<StageNavigationExecutionErrorCode> execution = std::nullopt,
        std::optional<StagePositionCommandReductionErrorCode> command = std::nullopt,
        std::optional<StagePositionFeedbackReducerErrorCode> feedback = std::nullopt,
        std::optional<core::StageAxisPositionStateError> position = std::nullopt,
        std::optional<core::SerializedOperationRuntimeError> runtime = std::nullopt) {
        return Result::failure(StageNavigationRuntimeError{
            code, planning, execution, command, feedback, position, runtime});
    }

    [[nodiscard]] Result success(
        StageNavigationRuntimeDisposition disposition,
        std::optional<core::OperationSnapshot> operation = std::nullopt) const {
        return Result::success(StageNavigationRuntimeUpdate{
            disposition, std::move(operation)});
    }

    [[nodiscard]] core::Result<bool, StageNavigationRuntimeError>
    requireOwnerThread() {
        const auto current = std::this_thread::get_id();
        if (!ownerThreadBound_) {
            return core::Result<bool, StageNavigationRuntimeError>::failure(
                makeError(StageNavigationRuntimeErrorCode::OwnerThreadNotBound));
        }
        if (ownerThread_ != current) {
            return core::Result<bool, StageNavigationRuntimeError>::failure(
                makeError(StageNavigationRuntimeErrorCode::WrongOwnerThread));
        }
        return core::Result<bool, StageNavigationRuntimeError>::success(true);
    }

    [[nodiscard]] static bool sameDevice(
        const core::RuntimeDeviceCorrelation& left,
        const core::RuntimeDeviceCorrelation& right) noexcept {
        return left.microscopeId == right.microscopeId &&
               left.runtimeId == right.runtimeId &&
               left.runtimeGeneration == right.runtimeGeneration &&
               left.deviceId == right.deviceId &&
               left.deviceSessionGeneration == right.deviceSessionGeneration;
    }

    [[nodiscard]] bool matchesCommand(
        const FeedbackResult& feedback,
        const core::StageMotionCommandContext& context) const noexcept {
        if (feedback.disposition !=
                core::FeedbackProcessingDisposition::AppliedToOperation ||
            !feedback.receipt) {
            return false;
        }
        const auto& correlation =
            feedback.receipt->feedback().metadata().correlation();
        const auto& expectedDevice = context.runtimeDevice();
        const auto& expectedOperation = context.operation();
        if (!sameDevice(correlation.runtimeDevice, expectedDevice) ||
            !correlation.operation) {
            return false;
        }
        return correlation.operation->operationId == expectedOperation.operationId &&
               correlation.operation->stepId == expectedOperation.stepId &&
               correlation.operation->commandId == expectedOperation.commandId &&
               feedback.operationId ==
                   std::optional<core::OperationId>(expectedOperation.operationId) &&
               feedback.stepId ==
                   std::optional<core::StepId>(expectedOperation.stepId);
    }

    [[nodiscard]] DispatchDisposition inspectDispatch(
        const StageNavigationExecution& execution) const noexcept {
        const auto* error = std::get_if<StageMotionDispatchError>(&execution.dispatch);
        if (error != nullptr) {
            return error->code ==
                       StageMotionDispatchErrorCode::AdapterSubmissionOutcomeUnknown ||
                   error->code ==
                       StageMotionDispatchErrorCode::RuntimeRejectedSubmissionResult
                ? DispatchDisposition::Indeterminate
                : DispatchDisposition::Rejected;
        }
        const auto* receipt =
            std::get_if<StageMotionDispatchReceipt>(&execution.dispatch);
        if (receipt == nullptr ||
            receipt->submission.status() == core::StageMotionSubmissionStatus::Rejected) {
            return DispatchDisposition::Rejected;
        }
        return DispatchDisposition::Accepted;
    }

    [[nodiscard]] static double toMicrometers(
        double value, core::AxisCoordinateUnit unit) noexcept {
        return unit == core::AxisCoordinateUnit::Millimeters
            ? value * 1000.0 : value;
    }

    [[nodiscard]] bool homeReferenceWithinPolicy(
        const core::StageHomeReferenceObservation& home,
        const core::ObservationSource source) const {
        if (!home.matchesAxes(std::get<core::StageHomeThenMovePlan>(
                active_->execution.plan).home.axes())) {
            return false;
        }

        for (const auto& observation : home.positions()) {
            const auto& position = observation.position();
            double maximumUncertainty = 0.0;
            if (position.axisId() == carrier_.specification().horizontalAxisId()) {
                maximumUncertainty =
                    verificationPolicy_.horizontalHomeUncertaintyMicrometers();
                if (position.unit() != carrier_.specification().horizontalUnit()) {
                    return false;
                }
            } else if (position.axisId() ==
                       carrier_.specification().transverseAxisId()) {
                maximumUncertainty =
                    verificationPolicy_.transverseHomeUncertaintyMicrometers();
                if (position.unit() != carrier_.specification().transverseUnit()) {
                    return false;
                }
            } else {
                return false;
            }
            if (toMicrometers(observation.uncertaintyInAxisUnit(), position.unit()) >
                maximumUncertainty) {
                return false;
            }

            // Preflight the exact state update before committing the operation
            // step as Verified. This catches axis, unit and provenance errors.
            auto candidate = position_;
            auto preflight = candidate.afterVerifiedHomingObservation(
                observation, source);
            if (!preflight) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] Result processHomeReference(const FeedbackResult& feedback) {
        const auto* homePlan =
            std::get_if<core::StageHomeThenMovePlan>(&active_->execution.plan);
        if (homePlan == nullptr || !matchesCommand(
                feedback, active_->execution.commandContext) ||
            feedback.stepStatus !=
                std::optional<core::StepStatus>(core::StepStatus::Verifying)) {
            return success(StageNavigationRuntimeDisposition::IgnoredFeedback);
        }

        const auto& envelope = feedback.receipt->feedback();
        const auto* home =
            std::get_if<core::StageHomeReferenceObservation>(&envelope.payload());
        if (home == nullptr ||
            !homeReferenceWithinPolicy(*home, envelope.metadata().source())) {
            const auto operationId = active_->identities.operationId;
            const auto stepId = active_->execution.commandContext.operation().stepId;
            auto failed = operationRuntime_->recordStepVerification(
                operationId, stepId, false);
            if (!failed) {
                return failure(
                    StageNavigationRuntimeErrorCode::StepVerificationRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, failed.error());
            }
            position_ = position_.invalidateMachineReference();
            active_.reset();
            const auto snapshot = operationRuntime_->operationSnapshot(operationId);
            return success(StageNavigationRuntimeDisposition::NavigationFailed,
                           snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                    : std::nullopt);
        }

        const auto operationId = active_->identities.operationId;
        const auto stepId = active_->execution.commandContext.operation().stepId;
        auto verified = operationRuntime_->recordStepVerification(
            operationId, stepId, true);
        if (!verified) {
            return failure(
                StageNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, verified.error());
        }

        auto reference = StagePositionFeedbackReducer::applyVerifiedHomeReference(
            position_, feedback, active_->execution.commandContext,
            homePlan->home, *operationRuntime_);
        if (!reference) {
            position_ = position_.invalidateMachineReference();
            return failure(
                StageNavigationRuntimeErrorCode::HomeReferenceRejected,
                std::nullopt, std::nullopt, std::nullopt,
                reference.error().code,
                reference.error().positionStateError,
                reference.error().runtimeError);
        }
        position_ = reference.value().position;

        auto created = operationRuntime_->createStep(
            operationId, active_->identities.restoredMoveStepId);
        if (!created) {
            return failure(
                StageNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, created.error());
        }

        auto continuation = StageNavigationExecutor::continueAfterVerifiedHoming(
            active_->execution, carrier_, reference.value(), homingPolicy_,
            active_->identities.restoredMoveStepId,
            active_->identities.restoredMoveCommandId,
            *port_, *operationRuntime_);
        if (!continuation) {
            return failure(
                StageNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, continuation.error().code, std::nullopt,
                std::nullopt, std::nullopt, continuation.error().runtimeError);
        }

        active_->execution = std::move(continuation).value();
        active_->commandCompleted = false;
        active_->commandEstimate.reset();
        active_->horizontalObservation.reset();
        active_->transverseObservation.reset();

        const auto dispatch = inspectDispatch(active_->execution);
        if (dispatch == DispatchDisposition::Accepted) {
            return success(StageNavigationRuntimeDisposition::RestoredMoveDispatched);
        }
        if (dispatch == DispatchDisposition::Indeterminate) {
            position_ = position_.invalidateMachineReference();
            return success(StageNavigationRuntimeDisposition::SubmissionIndeterminate);
        }

        const auto* dispatchError =
            std::get_if<StageMotionDispatchError>(&active_->execution.dispatch);
        if (dispatchError != nullptr &&
            dispatchError->code == StageMotionDispatchErrorCode::RuntimeDispatchRejected) {
            const auto& operation = active_->execution.commandContext.operation();
            auto failed = operationRuntime_->recordPreDispatchFailure(
                operation.operationId, operation.stepId);
            if (!failed) {
                return failure(
                    StageNavigationRuntimeErrorCode::DispatchRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, failed.error());
            }
        }
        const auto snapshot = operationRuntime_->operationSnapshot(operationId);
        active_.reset();
        return success(StageNavigationRuntimeDisposition::NavigationFailed,
                       snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                : std::nullopt);
    }

    [[nodiscard]] Result processMoveCompletion(const FeedbackResult& feedback) {
        const auto* direct =
            std::get_if<core::StageDirectMovePlan>(&active_->execution.plan);
        if (direct == nullptr ||
            (feedback.stepStatus !=
                 std::optional<core::StepStatus>(core::StepStatus::AwaitingObservation) &&
             feedback.stepStatus !=
                 std::optional<core::StepStatus>(core::StepStatus::Verifying))) {
            return success(StageNavigationRuntimeDisposition::IgnoredFeedback);
        }

        auto reduction = StagePositionCommandReducer::reduceCompletedMoveAxesTo(
            position_, feedback, active_->execution.commandContext,
            direct->command, errorBudget_, operationRuntime_->feedbackLossLatched());
        if (!reduction) {
            position_ = position_.invalidateMachineReference();
            const auto failed = failMoveVerification();
            if (!failed) {
                return failed;
            }
            return failure(
                StageNavigationRuntimeErrorCode::CommandReductionRejected,
                std::nullopt, std::nullopt, reduction.error().code);
        }

        active_->commandEstimate = reduction.value().position;
        position_ = reduction.value().position;
        if (active_->horizontalObservation) {
            auto updated = position_.afterPositionObservation(
                active_->horizontalObservation->observation,
                active_->horizontalObservation->source);
            if (!updated) {
                position_ = position_.invalidateMachineReference();
                return failure(
                    StageNavigationRuntimeErrorCode::PositionFeedbackRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    updated.error());
            }
            position_ = std::move(updated).value();
        }
        if (active_->transverseObservation) {
            auto updated = position_.afterPositionObservation(
                active_->transverseObservation->observation,
                active_->transverseObservation->source);
            if (!updated) {
                return failure(
                    StageNavigationRuntimeErrorCode::PositionFeedbackRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    updated.error());
            }
            position_ = std::move(updated).value();
        }

        active_->commandCompleted = true;
        return tryCompleteMove();
    }

    [[nodiscard]] Result tryCompleteMove() {
        if (!active_ || !active_->commandCompleted || !active_->commandEstimate) {
            return success(StageNavigationRuntimeDisposition::AwaitingPositionFeedback);
        }

        core::StagePositionState verificationPosition = *active_->commandEstimate;
        if (verificationPolicy_.mode() ==
            StageNavigationPositionVerificationMode::DevicePositionObservations) {
            if (!active_->horizontalObservation || !active_->transverseObservation) {
                return success(StageNavigationRuntimeDisposition::AwaitingPositionFeedback);
            }
            auto horizontal = verificationPosition.afterPositionObservation(
                active_->horizontalObservation->observation,
                active_->horizontalObservation->source);
            if (!horizontal) {
                return failure(
                    StageNavigationRuntimeErrorCode::PositionFeedbackRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    horizontal.error());
            }
            verificationPosition = std::move(horizontal).value();
            auto transverse = verificationPosition.afterPositionObservation(
                active_->transverseObservation->observation,
                active_->transverseObservation->source);
            if (!transverse) {
                return failure(
                    StageNavigationRuntimeErrorCode::PositionFeedbackRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    transverse.error());
            }
            verificationPosition = std::move(transverse).value();
            position_ = verificationPosition;
        } else {
            position_ = verificationPosition;
        }

        const auto* direct =
            std::get_if<core::StageDirectMovePlan>(&active_->execution.plan);
        if (direct == nullptr) {
            return failure(StageNavigationRuntimeErrorCode::ExecutionRejected);
        }
        if (!positionWithinTarget(verificationPosition, direct->command)) {
            return failMoveVerification();
        }

        const auto& operation = active_->execution.commandContext.operation();
        auto status = operationRuntime_->stepStatus(
            operation.operationId, operation.stepId);
        if (!status) {
            return failure(
                StageNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, status.error());
        }
        if (status.value() == core::StepStatus::AwaitingObservation) {
            auto started = operationRuntime_->beginStepVerification(
                operation.operationId, operation.stepId);
            if (!started) {
                return failure(
                    StageNavigationRuntimeErrorCode::StepVerificationRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, started.error());
            }
        } else if (status.value() != core::StepStatus::Verifying) {
            return failure(StageNavigationRuntimeErrorCode::StepVerificationRejected);
        }

        auto verified = operationRuntime_->recordStepVerification(
            operation.operationId, operation.stepId, true);
        if (!verified) {
            return failure(
                StageNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, verified.error());
        }

        auto completed = StageNavigationExecutor::complete(
            active_->execution, *operationRuntime_);
        if (!completed) {
            return failure(
                StageNavigationRuntimeErrorCode::OperationCompletionRejected,
                std::nullopt, completed.error().code, std::nullopt,
                std::nullopt, std::nullopt, completed.error().runtimeError);
        }
        active_.reset();
        return success(StageNavigationRuntimeDisposition::NavigationCompleted,
                       std::move(completed).value());
    }

    [[nodiscard]] Result failMoveVerification() {
        const auto& operation = active_->execution.commandContext.operation();
        auto status = operationRuntime_->stepStatus(
            operation.operationId, operation.stepId);
        if (!status) {
            return failure(
                StageNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, status.error());
        }
        if (status.value() == core::StepStatus::AwaitingObservation) {
            auto started = operationRuntime_->beginStepVerification(
                operation.operationId, operation.stepId);
            if (!started) {
                return failure(
                    StageNavigationRuntimeErrorCode::StepVerificationRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, started.error());
            }
        } else if (status.value() != core::StepStatus::Verifying) {
            return failure(StageNavigationRuntimeErrorCode::StepVerificationRejected);
        }
        auto failed = operationRuntime_->recordStepVerification(
            operation.operationId, operation.stepId, false);
        if (!failed) {
            return failure(
                StageNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, failed.error());
        }
        const auto snapshot = operationRuntime_->operationSnapshot(operation.operationId);
        active_.reset();
        return success(StageNavigationRuntimeDisposition::NavigationFailed,
                       snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                : std::nullopt);
    }

    [[nodiscard]] bool positionWithinTarget(
        const core::StagePositionState& state,
        const core::MoveAxesTo& command) const noexcept {
        const auto& horizontal = state.horizontal();
        const auto& transverse = state.transverse();
        if (!horizontal.positionIsUsable() || !transverse.positionIsUsable() ||
            !horizontal.position() || !transverse.position() ||
            !horizontal.accumulatedPositionErrorBound() ||
            !transverse.accumulatedPositionErrorBound()) {
            return false;
        }

        if (verificationPolicy_.mode() ==
            StageNavigationPositionVerificationMode::BoundedCommandEstimate) {
            if (horizontal.evidence() != core::StagePositionEvidence::CommandDerivedEstimate ||
                transverse.evidence() != core::StagePositionEvidence::CommandDerivedEstimate) {
                return false;
            }
        } else {
            if (horizontal.evidence() == core::StagePositionEvidence::CommandDerivedEstimate ||
                horizontal.evidence() == core::StagePositionEvidence::Unknown ||
                transverse.evidence() == core::StagePositionEvidence::CommandDerivedEstimate ||
                transverse.evidence() == core::StagePositionEvidence::Unknown) {
                return false;
            }
        }

        const core::AxisPosition* horizontalTarget = nullptr;
        const core::AxisPosition* transverseTarget = nullptr;
        for (const auto& target : command.targets()) {
            if (target.axisId() == horizontal.axisId()) {
                horizontalTarget = &target;
            } else if (target.axisId() == transverse.axisId()) {
                transverseTarget = &target;
            }
        }
        if (horizontalTarget == nullptr || transverseTarget == nullptr ||
            horizontalTarget->unit() != horizontal.unit() ||
            transverseTarget->unit() != transverse.unit()) {
            return false;
        }

        const double horizontalError =
            std::abs(toMicrometers(
                horizontal.position()->value() - horizontalTarget->value(),
                horizontal.unit())) +
            toMicrometers(
                *horizontal.accumulatedPositionErrorBound(), horizontal.unit());
        const double transverseError =
            std::abs(toMicrometers(
                transverse.position()->value() - transverseTarget->value(),
                transverse.unit())) +
            toMicrometers(
                *transverse.accumulatedPositionErrorBound(), transverse.unit());

        return std::isfinite(horizontalError) &&
               std::isfinite(transverseError) &&
               horizontalError <= verificationPolicy_.horizontalTargetToleranceMicrometers() &&
               transverseError <= verificationPolicy_.transverseTargetToleranceMicrometers();
    }

        core::DualSlideCarrierState carrier_;
    core::StagePositionState position_;
    core::StageHomingPolicy homingPolicy_;
    StageMotionErrorBudget errorBudget_;
    StageNavigationVerificationPolicy verificationPolicy_;
    core::RuntimeDeviceCorrelation runtimeDevice_;
    core::IStageMotionPort* port_;
    Runtime* operationRuntime_;
    std::optional<ActiveNavigation> active_;
    std::thread::id ownerThread_{};
    bool ownerThreadBound_{false};
    bool feedbackLossObserved_{false};
};

} // namespace microscope::application::motion
