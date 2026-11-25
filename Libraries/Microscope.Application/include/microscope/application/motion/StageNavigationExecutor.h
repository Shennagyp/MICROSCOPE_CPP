#pragma once

#include "microscope/application/motion/StageMotionCommandDispatcher.h"
#include "microscope/core/navigation/StageNavigationPlanner.h"

#include <optional>
#include <utility>
#include <variant>

namespace microscope::application::motion {

using StageNavigationDispatchOutcome =
    std::variant<StageMotionDispatchReceipt, StageMotionDispatchError>;

enum class StageNavigationExecutionErrorCode {
    PlanningRejected,
    CommandContextRejected,
    NotAwaitingHoming,
    HomeStepNotVerified,
    RestoredTargetRejected,
    MoveStepDispatchRejected,
    ActiveStepNotVerified,
    OperationVerificationRejected
};

struct StageNavigationExecutionError final {
    StageNavigationExecutionErrorCode code;
    std::optional<core::StageNavigationPlanningError> planningError;
    std::optional<core::StageMotionCommandContextError> commandContextError;
    std::optional<core::SerializedOperationRuntimeError> runtimeError;
};

// Owner-thread execution context for one slide-local navigation request.
// The host creates the operation and the pending step before calling begin().
// It also applies feedback reducers and marks a step Verified only after its
// capability-specific evidence policy succeeds.
struct StageNavigationExecution final {
    core::StageNavigationPlan plan;
    core::StageMotionCommandContext commandContext;
    StageNavigationDispatchOutcome dispatch;
    bool homingPerformed;
};

class StageNavigationExecutor final {
public:
    using Runtime = core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>;
    using Result = core::Result<StageNavigationExecution, StageNavigationExecutionError>;
    using CompletionResult =
        core::Result<core::OperationSnapshot, StageNavigationExecutionError>;

    // Requires an existing Running operation and a Pending step identified by
    // operationId/stepId/commandId. begin() chooses direct coordinated motion
    // or the two-axis homing path from the Core planner and dispatches that
    // first action through the typed motion port.
    [[nodiscard]] static Result begin(
        const core::DualSlideCarrierState& carrier,
        const core::SlideId& slideId,
        core::SlideLocalPoint destination,
        const core::StagePositionState& position,
        const core::StageHomingPolicy& homingPolicy,
        core::RuntimeDeviceCorrelation runtimeDevice,
        core::OperationId operationId,
        core::StepId stepId,
        core::CommandId commandId,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        auto plan = core::StageNavigationPlanner::planToSlidePoint(
            carrier, slideId, destination, position, homingPolicy);
        if (!plan) {
            return Result::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::PlanningRejected,
                plan.error(), std::nullopt, std::nullopt});
        }

        auto context = core::StageMotionCommandContext::create(
            runtimeDevice,
            core::OperationCorrelation{
                operationId, stepId, std::move(commandId)});
        if (!context) {
            return Result::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::CommandContextRejected,
                std::nullopt, context.error(), std::nullopt});
        }

        auto outcome = dispatchPlan(plan.value(), context.value(), port, runtime);
        return Result::success(StageNavigationExecution{
            std::move(plan).value(),
            std::move(context).value(),
            std::move(outcome),
            false});
    }

    // Continues a HomeThenMove execution only after the original home step has
    // been verified and the caller has applied its complete home-reference
    // payload to StagePositionState. The new absolute target is recomputed from
    // the preserved slide-local target and current registration revision.
    //
    // The caller must create the restored move step in the same Running
    // operation before calling this method.
    [[nodiscard]] static Result continueAfterVerifiedHoming(
        const StageNavigationExecution& execution,
        const core::DualSlideCarrierState& carrier,
        const core::StagePositionState& positionAfterHoming,
        const core::StageHomingPolicy& homingPolicy,
        core::StepId restoredMoveStepId,
        core::CommandId restoredMoveCommandId,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        const auto* homePlan = std::get_if<core::StageHomeThenMovePlan>(&execution.plan);
        if (homePlan == nullptr) {
            return Result::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::NotAwaitingHoming,
                std::nullopt, std::nullopt, std::nullopt});
        }

        const auto& homeOperation = execution.commandContext.operation();
        auto homeStepStatus = runtime.stepStatus(
            homeOperation.operationId, homeOperation.stepId);
        if (!homeStepStatus) {
            return Result::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::HomeStepNotVerified,
                std::nullopt, std::nullopt, homeStepStatus.error()});
        }
        if (homeStepStatus.value() != core::StepStatus::Verified) {
            return Result::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::HomeStepNotVerified,
                std::nullopt, std::nullopt, std::nullopt});
        }

        auto restoredCommand = core::StageNavigationPlanner::resolveAfterHoming(
            carrier, homePlan->target, positionAfterHoming, homingPolicy);
        if (!restoredCommand) {
            return Result::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::RestoredTargetRejected,
                restoredCommand.error(), std::nullopt, std::nullopt});
        }

        auto moveContext = core::StageMotionCommandContext::create(
            execution.commandContext.runtimeDevice(),
            core::OperationCorrelation{
                homeOperation.operationId,
                std::move(restoredMoveStepId),
                std::move(restoredMoveCommandId)});
        if (!moveContext) {
            return Result::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::CommandContextRejected,
                std::nullopt, moveContext.error(), std::nullopt});
        }

        core::StageDirectMovePlan movePlan{
            homePlan->target, std::move(restoredCommand).value()};
        auto outcome = StageMotionCommandDispatcher::moveAxesTo(
            moveContext.value(), movePlan.command, port, runtime);

        return Result::success(StageNavigationExecution{
            core::StageNavigationPlan{std::move(movePlan)},
            std::move(moveContext).value(),
            outcome ? StageNavigationDispatchOutcome{std::move(outcome).value()}
                    : StageNavigationDispatchOutcome{std::move(outcome).error()},
            true});
    }

    // The active command step must already be Verified by the stage-specific
    // evidence policy. Core additionally requires every operation step to be
    // Verified and feedback loss to be reconciled before final success.
    [[nodiscard]] static CompletionResult complete(
        const StageNavigationExecution& execution,
        Runtime& runtime) {
        const auto& operation = execution.commandContext.operation();
        if (std::holds_alternative<core::StageHomeThenMovePlan>(execution.plan)) {
            return CompletionResult::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::NotAwaitingHoming,
                std::nullopt, std::nullopt, std::nullopt});
        }

        auto status = runtime.stepStatus(operation.operationId, operation.stepId);
        if (!status || status.value() != core::StepStatus::Verified) {
            return CompletionResult::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::ActiveStepNotVerified,
                std::nullopt, std::nullopt,
                status ? std::nullopt
                       : std::optional<core::SerializedOperationRuntimeError>(status.error())});
        }

        auto verification = runtime.beginOperationVerification(operation.operationId);
        if (!verification) {
            return CompletionResult::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::OperationVerificationRejected,
                std::nullopt, std::nullopt, verification.error()});
        }

        auto completed = runtime.recordOperationVerification(
            operation.operationId, true);
        if (!completed) {
            return CompletionResult::failure(StageNavigationExecutionError{
                StageNavigationExecutionErrorCode::OperationVerificationRejected,
                std::nullopt, std::nullopt, completed.error()});
        }
        return CompletionResult::success(std::move(completed).value());
    }

private:
    static StageNavigationDispatchOutcome dispatchPlan(
        const core::StageNavigationPlan& plan,
        const core::StageMotionCommandContext& context,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        if (const auto* direct =
                std::get_if<core::StageDirectMovePlan>(&plan)) {
            auto result = StageMotionCommandDispatcher::moveAxesTo(
                context, direct->command, port, runtime);
            return result
                ? StageNavigationDispatchOutcome{std::move(result).value()}
                : StageNavigationDispatchOutcome{std::move(result).error()};
        }

        const auto& home = std::get<core::StageHomeThenMovePlan>(plan);
        auto result = StageMotionCommandDispatcher::homeAxes(
            context, home.home, port, runtime);
        return result
            ? StageNavigationDispatchOutcome{std::move(result).value()}
            : StageNavigationDispatchOutcome{std::move(result).error()};
    }
};

} // namespace microscope::application::motion
