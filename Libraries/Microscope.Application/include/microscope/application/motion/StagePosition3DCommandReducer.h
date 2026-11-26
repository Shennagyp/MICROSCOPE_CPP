#pragma once

#include "microscope/application/motion/StagePositionCommandReducer.h"
#include "microscope/core/stage/StagePosition3D.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace microscope::application::motion {

enum class StagePosition3DCommandReductionErrorCode {
    FeedbackLossUnresolved,
    FeedbackNotAppliedToOperation,
    MissingReceipt,
    CommandContextMismatch,
    InvalidStepStatus,
    InvalidCompletionPayload,
    WrongCompletedAction,
    CompletedAxisSetMismatch,
    ErrorBudgetAxisSetMismatch,
    ErrorBudgetUnitMismatch,
    PositionStateRejectedCommand
};

struct StagePosition3DCommandReductionError final {
    StagePosition3DCommandReductionErrorCode code;
    std::optional<core::StagePosition3DError> positionError;
};

struct StagePosition3DCommandReduction final {
    core::StagePosition3D position;
};

// Applies commanded X/Y estimates only after the exact correlated
// DeviceActionCompleted fact was accepted by SerializedOperationRuntime.
// Focus Z is never modified by a slide-plane move.
class StagePosition3DCommandReducer final {
public:
    using FeedbackResult =
        core::FeedbackProcessingResult<core::StageMotionFeedbackPayload>;
    using Result = core::Result<
        StagePosition3DCommandReduction,
        StagePosition3DCommandReductionError>;

    [[nodiscard]] static Result reduceCompletedMoveAxesTo(
        const core::StagePosition3D& current,
        const FeedbackResult& feedback,
        const core::StageMotionCommandContext& commandContext,
        const core::MoveAxesTo& command,
        const StageMotionErrorBudget& errorBudget,
        bool feedbackLossUnresolved) {
        if (feedbackLossUnresolved)
            return failure(StagePosition3DCommandReductionErrorCode::FeedbackLossUnresolved);
        if (feedback.disposition != core::FeedbackProcessingDisposition::AppliedToOperation)
            return failure(StagePosition3DCommandReductionErrorCode::FeedbackNotAppliedToOperation);
        if (!feedback.receipt)
            return failure(StagePosition3DCommandReductionErrorCode::MissingReceipt);

        const auto& envelope = feedback.receipt->feedback();
        const auto& metadata = envelope.metadata();
        const auto& actualDevice = metadata.correlation().runtimeDevice;
        const auto& expectedDevice = commandContext.runtimeDevice();
        const auto& actualOperation = metadata.correlation().operation;
        const auto& expectedOperation = commandContext.operation();

        if (metadata.kind() != core::FeedbackKind::DeviceActionCompleted ||
            actualDevice.microscopeId != expectedDevice.microscopeId ||
            actualDevice.runtimeId != expectedDevice.runtimeId ||
            actualDevice.runtimeGeneration != expectedDevice.runtimeGeneration ||
            actualDevice.deviceId != expectedDevice.deviceId ||
            actualDevice.deviceSessionGeneration != expectedDevice.deviceSessionGeneration ||
            !actualOperation ||
            actualOperation->operationId != expectedOperation.operationId ||
            actualOperation->stepId != expectedOperation.stepId ||
            actualOperation->commandId != expectedOperation.commandId ||
            feedback.operationId !=
                std::optional<core::OperationId>(expectedOperation.operationId) ||
            feedback.stepId != std::optional<core::StepId>(expectedOperation.stepId))
            return failure(StagePosition3DCommandReductionErrorCode::CommandContextMismatch);

        if (!feedback.stepStatus ||
            (*feedback.stepStatus != core::StepStatus::AwaitingObservation &&
             *feedback.stepStatus != core::StepStatus::Verifying))
            return failure(StagePosition3DCommandReductionErrorCode::InvalidStepStatus);

        const auto* completion =
            std::get_if<core::StageMotionActionCompletion>(&envelope.payload());
        if (completion == nullptr)
            return failure(StagePosition3DCommandReductionErrorCode::InvalidCompletionPayload);
        if (completion->action() != core::StageMotionAction::MoveAxesTo)
            return failure(StagePosition3DCommandReductionErrorCode::WrongCompletedAction);

        const auto& targets = command.targets();
        const auto& completedAxes = completion->axes().axes();
        if (targets.size() != 2 || completedAxes.size() != 2 ||
            errorBudget.contributions().size() != 2)
            return failure(StagePosition3DCommandReductionErrorCode::CompletedAxisSetMismatch);

        bool sawX = false;
        bool sawY = false;
        for (const auto& target : targets) {
            const auto& axisId = target.axisId();
            if (axisId == current.x().axisId()) {
                if (sawX) return failure(
                    StagePosition3DCommandReductionErrorCode::CompletedAxisSetMismatch);
                sawX = true;
            } else if (axisId == current.y().axisId()) {
                if (sawY) return failure(
                    StagePosition3DCommandReductionErrorCode::CompletedAxisSetMismatch);
                sawY = true;
            } else {
                return failure(StagePosition3DCommandReductionErrorCode::CompletedAxisSetMismatch);
            }
            if (std::find(completedAxes.begin(), completedAxes.end(), axisId) ==
                completedAxes.end())
                return failure(StagePosition3DCommandReductionErrorCode::CompletedAxisSetMismatch);

            const auto* contribution = errorBudget.contributionFor(axisId);
            if (contribution == nullptr)
                return failure(StagePosition3DCommandReductionErrorCode::ErrorBudgetAxisSetMismatch);
            if (contribution->unit() != target.unit())
                return failure(StagePosition3DCommandReductionErrorCode::ErrorBudgetUnitMismatch);
        }
        if (!sawX || !sawY)
            return failure(StagePosition3DCommandReductionErrorCode::CompletedAxisSetMismatch);

        for (const auto& axisId : completedAxes) {
            if (axisId != current.x().axisId() && axisId != current.y().axisId())
                return failure(StagePosition3DCommandReductionErrorCode::CompletedAxisSetMismatch);
        }
        for (const auto& contribution : errorBudget.contributions()) {
            if (contribution.axisId() != current.x().axisId() &&
                contribution.axisId() != current.y().axisId())
                return failure(StagePosition3DCommandReductionErrorCode::ErrorBudgetAxisSetMismatch);
        }

        auto updated = current;
        for (const auto& target : targets) {
            const auto* contribution = errorBudget.contributionFor(target.axisId());
            auto next = updated.afterCommandedPosition(
                target, contribution->conservativeBound());
            if (!next)
                return Result::failure(StagePosition3DCommandReductionError{
                    StagePosition3DCommandReductionErrorCode::PositionStateRejectedCommand,
                    next.error()});
            updated = std::move(next).value();
        }
        return Result::success(StagePosition3DCommandReduction{std::move(updated)});
    }

private:
    [[nodiscard]] static Result failure(StagePosition3DCommandReductionErrorCode code) {
        return Result::failure(StagePosition3DCommandReductionError{code, std::nullopt});
    }
};

} // namespace microscope::application::motion
