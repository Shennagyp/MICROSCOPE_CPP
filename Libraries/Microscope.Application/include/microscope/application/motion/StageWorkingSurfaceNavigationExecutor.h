#pragma once

#include "microscope/application/motion/StageMotionCommandDispatcher.h"
#include "microscope/application/motion/StagePosition3DFeedbackReducer.h"
#include "microscope/core/navigation/StageWorkingSurfaceNavigationPlanner.h"

#include <optional>
#include <utility>
#include <variant>

namespace microscope::application::motion {

using StageWorkingSurfaceNavigationDispatchOutcome =
    std::variant<StageMotionDispatchReceipt, StageMotionDispatchError>;

enum class StageWorkingSurfaceNavigationExecutionErrorCode {
    PlanningRejected,
    CommandContextRejected,
    NotAwaitingHoming,
    HomeStepNotVerified,
    HomeReferenceNotApplied,
    FeedbackLossUnresolved,
    RestoredTargetRejected,
    ActiveStepNotVerified,
    OperationVerificationRejected
};

struct StageWorkingSurfaceNavigationExecutionError final {
    StageWorkingSurfaceNavigationExecutionErrorCode code;
    std::optional<core::StageWorkingSurfaceNavigationPlanningError> planningError;
    std::optional<core::StageMotionCommandContextError> commandContextError;
    std::optional<StagePosition3DFeedbackErrorCode> feedbackReductionError;
    std::optional<core::SerializedOperationRuntimeError> runtimeError;
};

struct StageWorkingSurfaceNavigationExecution final {
    core::StageWorkingSurfaceNavigationPlan plan;
    core::StageMotionCommandContext commandContext;
    StageWorkingSurfaceNavigationDispatchOutcome dispatch;
    bool homingPerformed;
};

class StageWorkingSurfaceNavigationExecutor final {
public:
    using Runtime = core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>;
    using Result = core::Result<
        StageWorkingSurfaceNavigationExecution,
        StageWorkingSurfaceNavigationExecutionError>;
    using CompletionResult =
        core::Result<core::OperationSnapshot, StageWorkingSurfaceNavigationExecutionError>;

    // Host/application allocates the operation, step and command identities.
    // The operation must already be Running and the step Pending.
    [[nodiscard]] static Result begin(
        const core::StageWorkingSurface2D& surface,
        const core::SlideId& slideId,
        core::SlidePoint2D destination,
        const core::StagePosition3D& position,
        const core::StageXYHomingPolicy& homingPolicy,
        core::RuntimeDeviceCorrelation runtimeDevice,
        core::OperationId operationId,
        core::StepId stepId,
        core::CommandId commandId,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        auto plan = core::StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
            surface, slideId, destination, position, homingPolicy);
        if (!plan) {
            return Result::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::PlanningRejected,
                plan.error(), std::nullopt, std::nullopt, std::nullopt});
        }

        auto context = core::StageMotionCommandContext::create(
            std::move(runtimeDevice),
            core::OperationCorrelation{operationId, stepId, std::move(commandId)});
        if (!context) {
            return Result::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::CommandContextRejected,
                std::nullopt, context.error(), std::nullopt, std::nullopt});
        }

        auto outcome = dispatchPlan(plan.value(), context.value(), port, runtime);
        return Result::success(StageWorkingSurfaceNavigationExecution{
            std::move(plan).value(), std::move(context).value(),
            std::move(outcome), false});
    }

    // The caller must have applied and verified the exact correlated home
    // reference fact and must create the continuation step before calling.
    [[nodiscard]] static Result continueAfterVerifiedHoming(
        const StageWorkingSurfaceNavigationExecution& execution,
        const core::StageWorkingSurface2D& surface,
        const StagePosition3DFeedbackReduction& verifiedHomeReduction,
        const core::StageXYHomingPolicy& homingPolicy,
        core::StepId restoredMoveStepId,
        core::CommandId restoredMoveCommandId,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        const auto* homePlan =
            std::get_if<core::StageWorkingSurfaceHomeThenMovePlan>(&execution.plan);
        if (homePlan == nullptr) {
            return Result::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::NotAwaitingHoming,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt});
        }

        const auto& homeOperation = execution.commandContext.operation();
        auto homeStepStatus = runtime.stepStatus(
            homeOperation.operationId, homeOperation.stepId);
        if (!homeStepStatus || homeStepStatus.value() != core::StepStatus::Verified) {
            return Result::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::HomeStepNotVerified,
                std::nullopt, std::nullopt, std::nullopt,
                homeStepStatus ? std::nullopt
                    : std::optional<core::SerializedOperationRuntimeError>(homeStepStatus.error())});
        }
        if (runtime.feedbackLossLatched()) {
            return Result::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::FeedbackLossUnresolved,
                std::nullopt, std::nullopt, std::nullopt,
                core::SerializedOperationRuntimeError::FeedbackLossUnresolved});
        }
        if (verifiedHomeReduction.disposition !=
            StagePosition3DFeedbackDisposition::HomeReferenceApplied) {
            return Result::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::HomeReferenceNotApplied,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt});
        }

        auto restoredCommand = core::StageWorkingSurfaceNavigationPlanner::resolveAfterHoming(
            surface, homePlan->target, verifiedHomeReduction.position, homingPolicy);
        if (!restoredCommand) {
            return Result::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::RestoredTargetRejected,
                restoredCommand.error(), std::nullopt, std::nullopt, std::nullopt});
        }

        auto moveContext = core::StageMotionCommandContext::create(
            execution.commandContext.runtimeDevice(),
            core::OperationCorrelation{
                homeOperation.operationId,
                std::move(restoredMoveStepId),
                std::move(restoredMoveCommandId)});
        if (!moveContext) {
            return Result::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::CommandContextRejected,
                std::nullopt, moveContext.error(), std::nullopt, std::nullopt});
        }

        core::StageWorkingSurfaceDirectMovePlan movePlan{
            homePlan->target, std::move(restoredCommand).value()};
        auto outcome = StageMotionCommandDispatcher::moveAxesTo(
            moveContext.value(), movePlan.command, port, runtime);

        return Result::success(StageWorkingSurfaceNavigationExecution{
            core::StageWorkingSurfaceNavigationPlan{std::move(movePlan)},
            std::move(moveContext).value(),
            outcome ? StageWorkingSurfaceNavigationDispatchOutcome{std::move(outcome).value()}
                    : StageWorkingSurfaceNavigationDispatchOutcome{std::move(outcome).error()},
            true});
    }

    [[nodiscard]] static CompletionResult complete(
        const StageWorkingSurfaceNavigationExecution& execution,
        Runtime& runtime) {
        const auto& operation = execution.commandContext.operation();
        if (std::holds_alternative<core::StageWorkingSurfaceHomeThenMovePlan>(execution.plan)) {
            return CompletionResult::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::NotAwaitingHoming,
                std::nullopt, std::nullopt, std::nullopt, std::nullopt});
        }

        auto status = runtime.stepStatus(operation.operationId, operation.stepId);
        if (!status || status.value() != core::StepStatus::Verified) {
            return CompletionResult::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::ActiveStepNotVerified,
                std::nullopt, std::nullopt, std::nullopt,
                status ? std::nullopt
                    : std::optional<core::SerializedOperationRuntimeError>(status.error())});
        }

        auto verification = runtime.beginOperationVerification(operation.operationId);
        if (!verification) {
            return CompletionResult::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::OperationVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, verification.error()});
        }
        auto completed = runtime.recordOperationVerification(operation.operationId, true);
        if (!completed) {
            return CompletionResult::failure(StageWorkingSurfaceNavigationExecutionError{
                StageWorkingSurfaceNavigationExecutionErrorCode::OperationVerificationRejected,
                std::nullopt, std::nullopt, std::nullopt, completed.error()});
        }
        return CompletionResult::success(std::move(completed).value());
    }

private:
    static StageWorkingSurfaceNavigationDispatchOutcome dispatchPlan(
        const core::StageWorkingSurfaceNavigationPlan& plan,
        const core::StageMotionCommandContext& context,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        if (const auto* direct =
                std::get_if<core::StageWorkingSurfaceDirectMovePlan>(&plan)) {
            auto result = StageMotionCommandDispatcher::moveAxesTo(
                context, direct->command, port, runtime);
            return result
                ? StageWorkingSurfaceNavigationDispatchOutcome{std::move(result).value()}
                : StageWorkingSurfaceNavigationDispatchOutcome{std::move(result).error()};
        }

        const auto& home = std::get<core::StageWorkingSurfaceHomeThenMovePlan>(plan);
        auto result = StageMotionCommandDispatcher::homeAxes(
            context, home.home, port, runtime);
        return result
            ? StageWorkingSurfaceNavigationDispatchOutcome{std::move(result).value()}
            : StageWorkingSurfaceNavigationDispatchOutcome{std::move(result).error()};
    }
};

} // namespace microscope::application::motion
