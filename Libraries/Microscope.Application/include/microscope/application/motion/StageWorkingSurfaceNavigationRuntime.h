#pragma once

#include "microscope/application/motion/StageWorkingSurfaceNavigationExecutor.h"
#include "microscope/application/motion/StagePosition3DCommandReducer.h"
#include "microscope/application/motion/StagePosition3DFeedbackReducer.h"

#include <cmath>
#include <optional>
#include <thread>
#include <utility>

namespace microscope::application::motion {

// Open-loop drivers can verify a bounded Core-derived estimate after an action
// completes. Controllers with position readback can require fresh, command-
// correlated X/Y observations before the same navigation operation is verified.
enum class StageWorkingSurfaceNavigationPositionVerificationMode {
    BoundedCommandEstimate,
    DevicePositionObservations
};

enum class StageWorkingSurfaceNavigationVerificationPolicyError {
    InvalidXTolerance,
    InvalidYTolerance,
    InvalidXHomeUncertainty,
    InvalidYHomeUncertainty,
    InvalidMode
};

class StageWorkingSurfaceNavigationVerificationPolicy final {
public:
    [[nodiscard]] static core::Result<
        StageWorkingSurfaceNavigationVerificationPolicy,
        StageWorkingSurfaceNavigationVerificationPolicyError> create(
            double xTargetToleranceMicrometers,
            double yTargetToleranceMicrometers,
            double xHomeUncertaintyMicrometers,
            double yHomeUncertaintyMicrometers,
            StageWorkingSurfaceNavigationPositionVerificationMode mode) {
        if (!std::isfinite(xTargetToleranceMicrometers) ||
            xTargetToleranceMicrometers <= 0.0) {
            return core::Result<StageWorkingSurfaceNavigationVerificationPolicy,
                StageWorkingSurfaceNavigationVerificationPolicyError>::failure(
                    StageWorkingSurfaceNavigationVerificationPolicyError::InvalidXTolerance);
        }
        if (!std::isfinite(yTargetToleranceMicrometers) ||
            yTargetToleranceMicrometers <= 0.0) {
            return core::Result<StageWorkingSurfaceNavigationVerificationPolicy,
                StageWorkingSurfaceNavigationVerificationPolicyError>::failure(
                    StageWorkingSurfaceNavigationVerificationPolicyError::InvalidYTolerance);
        }
        if (!std::isfinite(xHomeUncertaintyMicrometers) ||
            xHomeUncertaintyMicrometers < 0.0) {
            return core::Result<StageWorkingSurfaceNavigationVerificationPolicy,
                StageWorkingSurfaceNavigationVerificationPolicyError>::failure(
                    StageWorkingSurfaceNavigationVerificationPolicyError::InvalidXHomeUncertainty);
        }
        if (!std::isfinite(yHomeUncertaintyMicrometers) ||
            yHomeUncertaintyMicrometers < 0.0) {
            return core::Result<StageWorkingSurfaceNavigationVerificationPolicy,
                StageWorkingSurfaceNavigationVerificationPolicyError>::failure(
                    StageWorkingSurfaceNavigationVerificationPolicyError::InvalidYHomeUncertainty);
        }
        if (mode != StageWorkingSurfaceNavigationPositionVerificationMode::BoundedCommandEstimate &&
            mode != StageWorkingSurfaceNavigationPositionVerificationMode::DevicePositionObservations) {
            return core::Result<StageWorkingSurfaceNavigationVerificationPolicy,
                StageWorkingSurfaceNavigationVerificationPolicyError>::failure(
                    StageWorkingSurfaceNavigationVerificationPolicyError::InvalidMode);
        }

        return core::Result<StageWorkingSurfaceNavigationVerificationPolicy,
            StageWorkingSurfaceNavigationVerificationPolicyError>::success(
                StageWorkingSurfaceNavigationVerificationPolicy(
                    xTargetToleranceMicrometers,
                    yTargetToleranceMicrometers,
                    xHomeUncertaintyMicrometers,
                    yHomeUncertaintyMicrometers,
                    mode));
    }

    [[nodiscard]] double xTargetToleranceMicrometers() const noexcept {
        return xTargetToleranceMicrometers_;
    }
    [[nodiscard]] double yTargetToleranceMicrometers() const noexcept {
        return yTargetToleranceMicrometers_;
    }
    [[nodiscard]] double xHomeUncertaintyMicrometers() const noexcept {
        return xHomeUncertaintyMicrometers_;
    }
    [[nodiscard]] double yHomeUncertaintyMicrometers() const noexcept {
        return yHomeUncertaintyMicrometers_;
    }
    [[nodiscard]] StageWorkingSurfaceNavigationPositionVerificationMode mode() const noexcept {
        return mode_;
    }

private:
    StageWorkingSurfaceNavigationVerificationPolicy(
        double xTolerance,
        double yTolerance,
        double xHomeUncertainty,
        double yHomeUncertainty,
        StageWorkingSurfaceNavigationPositionVerificationMode mode)
        : xTargetToleranceMicrometers_(xTolerance),
          yTargetToleranceMicrometers_(yTolerance),
          xHomeUncertaintyMicrometers_(xHomeUncertainty),
          yHomeUncertaintyMicrometers_(yHomeUncertainty),
          mode_(mode) {}

    double xTargetToleranceMicrometers_;
    double yTargetToleranceMicrometers_;
    double xHomeUncertaintyMicrometers_;
    double yHomeUncertaintyMicrometers_;
    StageWorkingSurfaceNavigationPositionVerificationMode mode_;
};

// IDs are allocated by the Application boundary. The continuation IDs are
// consumed only when the initial plan requires homing before the restored move.
struct StageWorkingSurfaceNavigationOperationIdentity final {
    core::OperationId operationId;
    core::StepId initialStepId;
    core::CommandId initialCommandId;
    core::StepId restoredMoveStepId;
    core::CommandId restoredMoveCommandId;
};

enum class StageWorkingSurfaceNavigationRuntimeDisposition {
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

struct StageWorkingSurfaceNavigationRuntimeUpdate final {
    StageWorkingSurfaceNavigationRuntimeDisposition disposition;
    std::optional<core::OperationSnapshot> operation;
};

enum class StageWorkingSurfaceNavigationRuntimeErrorCode {
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
    SurfaceReplacementDuringNavigation
};

struct StageWorkingSurfaceNavigationRuntimeError final {
    StageWorkingSurfaceNavigationRuntimeErrorCode code;
    std::optional<core::StageWorkingSurfaceNavigationPlanningError> planningError;
    std::optional<core::StageWorkingSurfaceNavigationExecutionErrorCode> executionError;
    std::optional<core::StagePosition3DCommandReductionErrorCode> commandReductionError;
    std::optional<core::StagePosition3DFeedbackErrorCode> feedbackReductionError;
    std::optional<core::StagePosition3DError> positionError;
    std::optional<core::SerializedOperationRuntimeError> runtimeError;
};

// One owner-thread stage subsystem for one microscope runtime. The host invokes
// beginNavigation() from its typed control-request drain and processFeedback()
// from SerializedRuntimeWorker's feedback handler. Neither method is callable
// concurrently; the serialized worker is the state owner.
//
// The caller creates and starts the operation and its initial pending step.
// This class owns the surface/position state and creates the continuation step
// only after the complete home-reference fact has been verified.
class StageWorkingSurfaceNavigationRuntime final {
public:
    using Runtime =
        core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>;
    using FeedbackResult =
        core::FeedbackProcessingResult<core::StageMotionFeedbackPayload>;
    using Result = core::Result<
        StageWorkingSurfaceNavigationRuntimeUpdate,
        StageWorkingSurfaceNavigationRuntimeError>;

    [[nodiscard]] static core::Result<
        StageWorkingSurfaceNavigationRuntime,
        StageWorkingSurfaceNavigationRuntimeError> create(
            core::StageWorkingSurface2D surface,
            core::StagePosition3D position,
            core::StageXYHomingPolicy homingPolicy,
            StageMotionErrorBudget errorBudget,
            StageWorkingSurfaceNavigationVerificationPolicy verificationPolicy,
            core::RuntimeDeviceCorrelation runtimeDevice,
            core::IStageMotionPort& port,
            Runtime& operationRuntime) {
        if (position.x().stageComponentId() != surface.stageComponentId() ||
            position.y().stageComponentId() != surface.stageComponentId() ||
            position.z().stageComponentId() != surface.stageComponentId()) {
            return creationFailure(StageWorkingSurfaceNavigationRuntimeErrorCode::StagePositionMismatch);
        }
        if (position.x().axisId() != surface.xAxisId() ||
            position.y().axisId() != surface.yAxisId()) {
            return creationFailure(StageWorkingSurfaceNavigationRuntimeErrorCode::StageAxisMismatch);
        }
        if (position.x().unit() != surface.xAxisUnit() ||
            position.y().unit() != surface.yAxisUnit()) {
            return creationFailure(StageWorkingSurfaceNavigationRuntimeErrorCode::StageUnitMismatch);
        }

        const auto* xBudget = errorBudget.contributionFor(surface.xAxisId());
        const auto* yBudget = errorBudget.contributionFor(surface.yAxisId());
        if (errorBudget.contributions().size() != 2 ||
            xBudget == nullptr || yBudget == nullptr ||
            xBudget->unit() != surface.xAxisUnit() ||
            yBudget->unit() != surface.yAxisUnit()) {
            return creationFailure(StageWorkingSurfaceNavigationRuntimeErrorCode::ErrorBudgetMismatch);
        }

        if (verificationPolicy.mode() !=
                StageWorkingSurfaceNavigationPositionVerificationMode::BoundedCommandEstimate &&
            verificationPolicy.mode() !=
                StageWorkingSurfaceNavigationPositionVerificationMode::DevicePositionObservations) {
            return creationFailure(StageWorkingSurfaceNavigationRuntimeErrorCode::VerificationPolicyMismatch);
        }

        return core::Result<StageWorkingSurfaceNavigationRuntime, StageWorkingSurfaceNavigationRuntimeError>::success(
            StageWorkingSurfaceNavigationRuntime(
                std::move(surface), std::move(position), std::move(homingPolicy),
                std::move(errorBudget), std::move(verificationPolicy),
                std::move(runtimeDevice), port, operationRuntime));
    }

    StageWorkingSurfaceNavigationRuntime(const StageWorkingSurfaceNavigationRuntime&) = delete;
    StageWorkingSurfaceNavigationRuntime& operator=(const StageWorkingSurfaceNavigationRuntime&) = delete;
    StageWorkingSurfaceNavigationRuntime(StageWorkingSurfaceNavigationRuntime&&) = default;
    StageWorkingSurfaceNavigationRuntime& operator=(StageWorkingSurfaceNavigationRuntime&&) = default;

    // Call from the same host-owned worker thread after
    // SerializedRuntimeWorker has bound the Core operation runtime.
    [[nodiscard]] core::Result<bool, StageWorkingSurfaceNavigationRuntimeError> bindOwnerThread() {
        const auto current = std::this_thread::get_id();
        if (ownerThreadBound_ && ownerThread_ != current) {
            return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::failure(
                makeError(StageWorkingSurfaceNavigationRuntimeErrorCode::WrongOwnerThread));
        }
        auto runtimeBinding = operationRuntime_->bindOwnerThread();
        if (!runtimeBinding) {
            auto error = makeError(StageWorkingSurfaceNavigationRuntimeErrorCode::WrongOwnerThread);
            error.runtimeError = runtimeBinding.error();
            return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::failure(
                std::move(error));
        }
        const bool newlyBound = !ownerThreadBound_;
        ownerThread_ = current;
        ownerThreadBound_ = true;
        return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::success(newlyBound);
    }

    // Application-control entry point. It validates the requested slide
    // target before allocating operation state, then creates the operation and
    // its first Pending step on the serialized owner thread.
    [[nodiscard]] Result beginRequest(
        const core::SlideId& slideId,
        core::SlidePoint2D destination,
        StageWorkingSurfaceNavigationOperationIdentity identities) {
        auto owner = requireOwnerThread();
        if (!owner) {
            return Result::failure(owner.error());
        }
        if (active_) {
            return failure(StageWorkingSurfaceNavigationRuntimeErrorCode::NavigationAlreadyActive);
        }
        if (operationRuntime_->feedbackLossLatched()) {
            position_ = position_.invalidateMachineReference();
            feedbackLossObserved_ = true;
            return failure(StageWorkingSurfaceNavigationRuntimeErrorCode::FeedbackLossUnresolved);
        }

        auto plan = core::StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
            surface_, slideId, destination, position_, homingPolicy_);
        if (!plan) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::PlanningRejected,
                plan.error());
        }

        auto created = operationRuntime_->createOperation(identities.operationId);
        if (!created) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, created.error());
        }
        auto started = operationRuntime_->startOperation(identities.operationId);
        if (!started) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, started.error());
        }
        auto step = operationRuntime_->createStep(
            identities.operationId, identities.initialStepId);
        if (!step) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, step.error());
        }

        return beginNavigation(slideId, destination, std::move(identities));
    }

    // operationId must already be Running and initialStepId must already be
    // Pending in the supplied serialized operation runtime.
    [[nodiscard]] Result beginNavigation(
        const core::SlideId& slideId,
        core::SlidePoint2D destination,
        StageWorkingSurfaceNavigationOperationIdentity identities) {
        auto owner = requireOwnerThread();
        if (!owner) {
            return Result::failure(owner.error());
        }
        if (active_) {
            return failure(StageWorkingSurfaceNavigationRuntimeErrorCode::NavigationAlreadyActive);
        }
        if (operationRuntime_->feedbackLossLatched()) {
            position_ = position_.invalidateMachineReference();
            feedbackLossObserved_ = true;
            return failure(StageWorkingSurfaceNavigationRuntimeErrorCode::FeedbackLossUnresolved);
        }
        if (identities.initialStepId == identities.restoredMoveStepId ||
            identities.initialCommandId == identities.restoredMoveCommandId) {
            return failure(StageWorkingSurfaceNavigationRuntimeErrorCode::ExecutionRejected);
        }

        auto operation = operationRuntime_->operationSnapshot(identities.operationId);
        if (!operation || operation.value().status() != core::OperationStatus::Running) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::OperationNotRunning,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                operation ? std::nullopt :
                    std::optional<core::SerializedOperationRuntimeError>(operation.error()));
        }
        auto step = operationRuntime_->stepStatus(
            identities.operationId, identities.initialStepId);
        if (!step || step.value() != core::StepStatus::Pending) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::InitialStepNotPending,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                step ? std::nullopt :
                    std::optional<core::SerializedOperationRuntimeError>(step.error()));
        }

        auto execution = StageWorkingSurfaceNavigationExecutor::begin(
            surface_, slideId, destination, position_, homingPolicy_, runtimeDevice_,
            identities.operationId, identities.initialStepId,
            identities.initialCommandId, *port_, *operationRuntime_);
        if (!execution) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::ExecutionRejected,
                execution.error().planningError,
                execution.error().code, std::nullopt, std::nullopt,
                std::nullopt, execution.error().runtimeError);
        }

        active_.emplace(ActiveNavigation{
            std::move(execution).value(), std::move(identities),
            false, std::nullopt, std::nullopt, std::nullopt});

        const auto dispatch = inspectDispatch(active_->execution);
        if (dispatch == DispatchDisposition::Accepted) {
            return success(std::holds_alternative<core::StageWorkingSurfaceHomeThenMovePlan>(
                               active_->execution.plan)
                    ? StageWorkingSurfaceNavigationRuntimeDisposition::HomingDispatched
                    : StageWorkingSurfaceNavigationRuntimeDisposition::MoveDispatched);
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
                        StageWorkingSurfaceNavigationRuntimeErrorCode::DispatchRejected,
                        std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                        std::nullopt, failed.error());
                }
            }
            const auto snapshot = operationRuntime_->operationSnapshot(
                active_->identities.operationId);
            active_.reset();
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::NavigationFailed,
                           snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                    : std::nullopt);
        }

        // The adapter may have received the command before the submission call
        // failed. Keep the correlation context and invalidate position instead
        // of claiming that the stage stayed still.
        position_ = position_.invalidateMachineReference();
        return success(StageWorkingSurfaceNavigationRuntimeDisposition::SubmissionIndeterminate);
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
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::IgnoredFeedback);
        }

        const auto& envelope = feedback.receipt->feedback();
        const auto& metadata = envelope.metadata();
        if (!sameDevice(metadata.correlation().runtimeDevice, runtimeDevice_)) {
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::IgnoredFeedback);
        }

        if (active_ &&
            feedback.disposition == core::FeedbackProcessingDisposition::AppliedToOperation &&
            !matchesCommand(feedback, active_->execution.commandContext)) {
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::IgnoredFeedback);
        }

        if (metadata.kind() == core::FeedbackKind::AdapterFailure &&
            active_ && matchesCommand(feedback, active_->execution.commandContext)) {
            position_ = position_.invalidateMachineReference();
            const auto snapshot =
                operationRuntime_->operationSnapshot(active_->identities.operationId);
            active_.reset();
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::NavigationFailed,
                           snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                    : std::nullopt);
        }

        auto positionReduction = StagePosition3DFeedbackReducer::reduce(
            position_, feedback, false);
        if (!positionReduction) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::PositionFeedbackRejected,
                std::nullopt, std::nullopt, std::nullopt,
                positionReduction.error().code,
                positionReduction.error().positionStateError,
                positionReduction.error().runtimeError);
        }
        position_ = std::move(positionReduction).value().position;

        if (!active_) {
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::PositionUpdated);
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
                if (observation->position().axisId() == surface_.xAxisId()) {
                    active_->xObservation = CorrelatedAxisObservation{
                        *observation, metadata.source()};
                } else if (observation->position().axisId() == surface_.yAxisId()) {
                    active_->yObservation = CorrelatedAxisObservation{
                        *observation, metadata.source()};
                }
                if (active_->commandCompleted &&
                    verificationPolicy_.mode() ==
                        StageWorkingSurfaceNavigationPositionVerificationMode::DevicePositionObservations) {
                    return tryCompleteMove();
                }
            }
        }

        return success(StageWorkingSurfaceNavigationRuntimeDisposition::AwaitingPositionFeedback);
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
                return success(StageWorkingSurfaceNavigationRuntimeDisposition::FeedbackLossDetected);
            }
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::IgnoredFeedback);
        }
        feedbackLossObserved_ = false;
        return success(StageWorkingSurfaceNavigationRuntimeDisposition::PositionUpdated);
    }

    [[nodiscard]] bool feedbackLossUnresolved() const noexcept {
        return operationRuntime_->feedbackLossLatched();
    }

    // Carrier occupancy and registrations may change only between navigation
    // operations. A new registration revision automatically invalidates any
    // previously captured SlideNavigationTarget2D.
    [[nodiscard]] core::Result<bool, StageWorkingSurfaceNavigationRuntimeError> replaceWorkingSurface(
        core::StageWorkingSurface2D surface) {
        auto owner = requireOwnerThread();
        if (!owner) {
            return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::failure(owner.error());
        }
        if (active_) {
            return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::failure(
                makeError(StageWorkingSurfaceNavigationRuntimeErrorCode::SurfaceReplacementDuringNavigation));
        }
        if (surface.stageComponentId() != position_.x().stageComponentId() ||
            surface.stageComponentId() != position_.y().stageComponentId() ||
            surface.stageComponentId() != position_.z().stageComponentId() ||
            surface.xAxisId() != position_.x().axisId() ||
            surface.yAxisId() != position_.y().axisId() ||
            surface.xAxisUnit() != position_.x().unit() ||
            surface.yAxisUnit() != position_.y().unit()) {
            return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::failure(
                makeError(StageWorkingSurfaceNavigationRuntimeErrorCode::StagePositionMismatch));
        }
        surface_ = std::move(surface);
        return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::success(true);
    }

    // Read this value on the serialized owner thread. Front ends should consume
    // immutable Application snapshots rather than retain this reference.
    [[nodiscard]] const core::StagePosition3D& position() const noexcept {
        return position_;
    }

    [[nodiscard]] const core::StageWorkingSurface2D& surface() const noexcept {
        return surface_;
    }

    // Returns the selected physical slide's current local coordinates through
    // its own registration. Unknown position/error or an out-of-slide mapping
    // remains an explicit error.
    [[nodiscard]] core::Result<
        core::SlidePositionEstimate2D,
        core::StagePosition3DError> currentSlidePosition(
            const core::SlideId& slideId) const {
        return position_.positionOnSlide(surface_, slideId);
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
        StageWorkingSurfaceNavigationExecution execution;
        StageWorkingSurfaceNavigationOperationIdentity identities;
        bool commandCompleted;
        std::optional<core::StagePosition3D> commandEstimate;
        std::optional<CorrelatedAxisObservation> xObservation;
        std::optional<CorrelatedAxisObservation> yObservation;
    };

    enum class DispatchDisposition { Accepted, Rejected, Indeterminate };

    StageWorkingSurfaceNavigationRuntime(
        core::StageWorkingSurface2D surface,
        core::StagePosition3D position,
        core::StageXYHomingPolicy homingPolicy,
        StageMotionErrorBudget errorBudget,
        StageWorkingSurfaceNavigationVerificationPolicy verificationPolicy,
        core::RuntimeDeviceCorrelation runtimeDevice,
        core::IStageMotionPort& port,
        Runtime& operationRuntime)
        : surface_(std::move(surface)),
          position_(std::move(position)),
          homingPolicy_(std::move(homingPolicy)),
          errorBudget_(std::move(errorBudget)),
          verificationPolicy_(std::move(verificationPolicy)),
          runtimeDevice_(std::move(runtimeDevice)),
          port_(&port),
          operationRuntime_(&operationRuntime) {}

    [[nodiscard]] static StageWorkingSurfaceNavigationRuntimeError makeError(
        StageWorkingSurfaceNavigationRuntimeErrorCode code) {
        return StageWorkingSurfaceNavigationRuntimeError{
            code, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt};
    }

    [[nodiscard]] static core::Result<
        StageWorkingSurfaceNavigationRuntime,
        StageWorkingSurfaceNavigationRuntimeError> creationFailure(
            StageWorkingSurfaceNavigationRuntimeErrorCode code) {
        return core::Result<StageWorkingSurfaceNavigationRuntime, StageWorkingSurfaceNavigationRuntimeError>::failure(
            makeError(code));
    }

    [[nodiscard]] static Result failure(
        StageWorkingSurfaceNavigationRuntimeErrorCode code,
        std::optional<core::StageWorkingSurfaceNavigationPlanningError> planning = std::nullopt,
        std::optional<StageWorkingSurfaceNavigationExecutionErrorCode> execution = std::nullopt,
        std::optional<StagePosition3DCommandReductionErrorCode> command = std::nullopt,
        std::optional<StagePosition3DFeedbackErrorCode> feedback = std::nullopt,
        std::optional<core::StagePosition3DError> position = std::nullopt,
        std::optional<core::SerializedOperationRuntimeError> runtime = std::nullopt) {
        return Result::failure(StageWorkingSurfaceNavigationRuntimeError{
            code, planning, execution, command, feedback, position, runtime});
    }

    [[nodiscard]] Result success(
        StageWorkingSurfaceNavigationRuntimeDisposition disposition,
        std::optional<core::OperationSnapshot> operation = std::nullopt) const {
        return Result::success(StageWorkingSurfaceNavigationRuntimeUpdate{
            disposition, std::move(operation)});
    }

    [[nodiscard]] core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>
    requireOwnerThread() {
        const auto current = std::this_thread::get_id();
        if (!ownerThreadBound_) {
            return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::failure(
                makeError(StageWorkingSurfaceNavigationRuntimeErrorCode::OwnerThreadNotBound));
        }
        if (ownerThread_ != current) {
            return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::failure(
                makeError(StageWorkingSurfaceNavigationRuntimeErrorCode::WrongOwnerThread));
        }
        return core::Result<bool, StageWorkingSurfaceNavigationRuntimeError>::success(true);
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
        const StageWorkingSurfaceNavigationExecution& execution) const noexcept {
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
        if (!home.matchesAxes(std::get<core::StageWorkingSurfaceHomeThenMovePlan>(
                active_->execution.plan).home.axes())) {
            return false;
        }

        for (const auto& observation : home.positions()) {
            const auto& position = observation.position();
            double maximumUncertainty = 0.0;
            if (position.axisId() == surface_.xAxisId()) {
                maximumUncertainty =
                    verificationPolicy_.xHomeUncertaintyMicrometers();
                if (position.unit() != surface_.xAxisUnit()) {
                    return false;
                }
            } else if (position.axisId() == surface_.yAxisId()) {
                maximumUncertainty =
                    verificationPolicy_.yHomeUncertaintyMicrometers();
                if (position.unit() != surface_.yAxisUnit()) {
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
            std::get_if<core::StageWorkingSurfaceHomeThenMovePlan>(&active_->execution.plan);
        if (homePlan == nullptr || !matchesCommand(
                feedback, active_->execution.commandContext) ||
            feedback.stepStatus !=
                std::optional<core::StepStatus>(core::StepStatus::Verifying)) {
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::IgnoredFeedback);
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
                    StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, failed.error());
            }
            position_ = position_.invalidateMachineReference();
            active_.reset();
            const auto snapshot = operationRuntime_->operationSnapshot(operationId);
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::NavigationFailed,
                           snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                    : std::nullopt);
        }

        const auto operationId = active_->identities.operationId;
        const auto stepId = active_->execution.commandContext.operation().stepId;
        auto verified = operationRuntime_->recordStepVerification(
            operationId, stepId, true);
        if (!verified) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, verified.error());
        }

        auto reference = StagePosition3DFeedbackReducer::applyVerifiedHomeReference(
            position_, feedback, active_->execution.commandContext,
            homePlan->home, *operationRuntime_);
        if (!reference) {
            position_ = position_.invalidateMachineReference();
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::HomeReferenceRejected,
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
                StageWorkingSurfaceNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, created.error());
        }

        auto continuation = StageWorkingSurfaceNavigationExecutor::continueAfterVerifiedHoming(
            active_->execution, surface_, reference.value(), homingPolicy_,
            active_->identities.restoredMoveStepId,
            active_->identities.restoredMoveCommandId,
            *port_, *operationRuntime_);
        if (!continuation) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::ExecutionRejected,
                std::nullopt, continuation.error().code, std::nullopt,
                std::nullopt, std::nullopt, continuation.error().runtimeError);
        }

        active_->execution = std::move(continuation).value();
        active_->commandCompleted = false;
        active_->commandEstimate.reset();
        active_->xObservation.reset();
        active_->yObservation.reset();

        const auto dispatch = inspectDispatch(active_->execution);
        if (dispatch == DispatchDisposition::Accepted) {
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::RestoredMoveDispatched);
        }
        if (dispatch == DispatchDisposition::Indeterminate) {
            position_ = position_.invalidateMachineReference();
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::SubmissionIndeterminate);
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
                    StageWorkingSurfaceNavigationRuntimeErrorCode::DispatchRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, failed.error());
            }
        }
        const auto snapshot = operationRuntime_->operationSnapshot(operationId);
        active_.reset();
        return success(StageWorkingSurfaceNavigationRuntimeDisposition::NavigationFailed,
                       snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                : std::nullopt);
    }

    [[nodiscard]] Result processMoveCompletion(const FeedbackResult& feedback) {
        const auto* direct =
            std::get_if<core::StageWorkingSurfaceDirectMovePlan>(&active_->execution.plan);
        if (direct == nullptr ||
            (feedback.stepStatus !=
                 std::optional<core::StepStatus>(core::StepStatus::AwaitingObservation) &&
             feedback.stepStatus !=
                 std::optional<core::StepStatus>(core::StepStatus::Verifying))) {
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::IgnoredFeedback);
        }

        auto reduction = StagePosition3DCommandReducer::reduceCompletedMoveAxesTo(
            position_, feedback, active_->execution.commandContext,
            direct->command, errorBudget_, operationRuntime_->feedbackLossLatched());
        if (!reduction) {
            position_ = position_.invalidateMachineReference();
            const auto failed = failMoveVerification();
            if (!failed) {
                return failed;
            }
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::CommandReductionRejected,
                std::nullopt, std::nullopt, reduction.error().code);
        }

        active_->commandEstimate = reduction.value().position;
        position_ = reduction.value().position;
        if (active_->xObservation) {
            auto updated = position_.afterPositionObservation(
                active_->xObservation->observation,
                active_->xObservation->source);
            if (!updated) {
                position_ = position_.invalidateMachineReference();
                return failure(
                    StageWorkingSurfaceNavigationRuntimeErrorCode::PositionFeedbackRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    updated.error());
            }
            position_ = std::move(updated).value();
        }
        if (active_->yObservation) {
            auto updated = position_.afterPositionObservation(
                active_->yObservation->observation,
                active_->yObservation->source);
            if (!updated) {
                return failure(
                    StageWorkingSurfaceNavigationRuntimeErrorCode::PositionFeedbackRejected,
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
            return success(StageWorkingSurfaceNavigationRuntimeDisposition::AwaitingPositionFeedback);
        }

        core::StagePosition3D verificationPosition = *active_->commandEstimate;
        if (verificationPolicy_.mode() ==
            StageWorkingSurfaceNavigationPositionVerificationMode::DevicePositionObservations) {
            if (!active_->xObservation || !active_->yObservation) {
                return success(StageWorkingSurfaceNavigationRuntimeDisposition::AwaitingPositionFeedback);
            }
            auto x = verificationPosition.afterPositionObservation(
                active_->xObservation->observation,
                active_->xObservation->source);
            if (!x) {
                return failure(
                    StageWorkingSurfaceNavigationRuntimeErrorCode::PositionFeedbackRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    x.error());
            }
            verificationPosition = std::move(x).value();
            auto y = verificationPosition.afterPositionObservation(
                active_->yObservation->observation,
                active_->yObservation->source);
            if (!y) {
                return failure(
                    StageWorkingSurfaceNavigationRuntimeErrorCode::PositionFeedbackRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    y.error());
            }
            verificationPosition = std::move(y).value();
            position_ = verificationPosition;
        } else {
            position_ = verificationPosition;
        }

        const auto* direct =
            std::get_if<core::StageWorkingSurfaceDirectMovePlan>(&active_->execution.plan);
        if (direct == nullptr) {
            return failure(StageWorkingSurfaceNavigationRuntimeErrorCode::ExecutionRejected);
        }
        if (!positionWithinTarget(verificationPosition, direct->command)) {
            return failMoveVerification();
        }

        const auto& operation = active_->execution.commandContext.operation();
        auto status = operationRuntime_->stepStatus(
            operation.operationId, operation.stepId);
        if (!status) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, status.error());
        }
        if (status.value() == core::StepStatus::AwaitingObservation) {
            auto started = operationRuntime_->beginStepVerification(
                operation.operationId, operation.stepId);
            if (!started) {
                return failure(
                    StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, started.error());
            }
        } else if (status.value() != core::StepStatus::Verifying) {
            return failure(StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected);
        }

        auto verified = operationRuntime_->recordStepVerification(
            operation.operationId, operation.stepId, true);
        if (!verified) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, verified.error());
        }

        auto completed = StageWorkingSurfaceNavigationExecutor::complete(
            active_->execution, *operationRuntime_);
        if (!completed) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::OperationCompletionRejected,
                std::nullopt, completed.error().code, std::nullopt,
                std::nullopt, std::nullopt, completed.error().runtimeError);
        }
        active_.reset();
        return success(StageWorkingSurfaceNavigationRuntimeDisposition::NavigationCompleted,
                       std::move(completed).value());
    }

    [[nodiscard]] Result failMoveVerification() {
        const auto& operation = active_->execution.commandContext.operation();
        auto status = operationRuntime_->stepStatus(
            operation.operationId, operation.stepId);
        if (!status) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, status.error());
        }
        if (status.value() == core::StepStatus::AwaitingObservation) {
            auto started = operationRuntime_->beginStepVerification(
                operation.operationId, operation.stepId);
            if (!started) {
                return failure(
                    StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, started.error());
            }
        } else if (status.value() != core::StepStatus::Verifying) {
            return failure(StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected);
        }
        auto failed = operationRuntime_->recordStepVerification(
            operation.operationId, operation.stepId, false);
        if (!failed) {
            return failure(
                StageWorkingSurfaceNavigationRuntimeErrorCode::StepVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt, failed.error());
        }
        const auto snapshot = operationRuntime_->operationSnapshot(operation.operationId);
        active_.reset();
        return success(StageWorkingSurfaceNavigationRuntimeDisposition::NavigationFailed,
                       snapshot ? std::optional<core::OperationSnapshot>(snapshot.value())
                                : std::nullopt);
    }

    [[nodiscard]] bool positionWithinTarget(
        const core::StagePosition3D& state,
        const core::MoveAxesTo& command) const noexcept {
        const auto& x = state.x();
        const auto& y = state.y();
        if (!x.positionIsUsable() || !y.positionIsUsable() ||
            !x.position() || !y.position() ||
            !x.accumulatedPositionErrorBound() ||
            !y.accumulatedPositionErrorBound()) {
            return false;
        }

        if (verificationPolicy_.mode() ==
            StageWorkingSurfaceNavigationPositionVerificationMode::BoundedCommandEstimate) {
            if (x.evidence() != core::StagePositionEvidence::CommandDerivedEstimate ||
                y.evidence() != core::StagePositionEvidence::CommandDerivedEstimate) {
                return false;
            }
        } else {
            if (x.evidence() == core::StagePositionEvidence::CommandDerivedEstimate ||
                x.evidence() == core::StagePositionEvidence::Unknown ||
                y.evidence() == core::StagePositionEvidence::CommandDerivedEstimate ||
                y.evidence() == core::StagePositionEvidence::Unknown) {
                return false;
            }
        }

        const core::AxisPosition* xTarget = nullptr;
        const core::AxisPosition* yTarget = nullptr;
        for (const auto& target : command.targets()) {
            if (target.axisId() == x.axisId()) {
                xTarget = &target;
            } else if (target.axisId() == y.axisId()) {
                yTarget = &target;
            }
        }
        if (xTarget == nullptr || yTarget == nullptr ||
            xTarget->unit() != x.unit() ||
            yTarget->unit() != y.unit()) {
            return false;
        }

        const double xError =
            std::abs(toMicrometers(
                x.position()->value() - xTarget->value(),
                x.unit())) +
            toMicrometers(
                *x.accumulatedPositionErrorBound(), x.unit());
        const double yError =
            std::abs(toMicrometers(
                y.position()->value() - yTarget->value(),
                y.unit())) +
            toMicrometers(
                *y.accumulatedPositionErrorBound(), y.unit());

        return std::isfinite(xError) &&
               std::isfinite(yError) &&
               xError <= verificationPolicy_.xTargetToleranceMicrometers() &&
               yError <= verificationPolicy_.yTargetToleranceMicrometers();
    }

        core::StageWorkingSurface2D surface_;
    core::StagePosition3D position_;
    core::StageXYHomingPolicy homingPolicy_;
    StageMotionErrorBudget errorBudget_;
    StageWorkingSurfaceNavigationVerificationPolicy verificationPolicy_;
    core::RuntimeDeviceCorrelation runtimeDevice_;
    core::IStageMotionPort* port_;
    Runtime* operationRuntime_;
    std::optional<ActiveNavigation> active_;
    std::thread::id ownerThread_{};
    bool ownerThreadBound_{false};
    bool feedbackLossObserved_{false};
};

} // namespace microscope::application::motion
