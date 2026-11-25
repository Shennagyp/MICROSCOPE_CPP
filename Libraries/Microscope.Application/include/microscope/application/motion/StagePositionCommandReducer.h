#pragma once

#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/motion/StagePositionState.h"
#include "microscope/core/runtime/SerializedOperationRuntime.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace microscope::application::motion {

enum class StageAxisMotionErrorContributionError {
    EmptyBudget,
    DuplicateAxisId,
    InvalidBound
};

// A conservative, calibrated motion-error bound for one physical axis.
// nullopt means the active calibration cannot provide a defensible bound;
// it must never be interpreted as zero.
class StageAxisMotionErrorContribution final {
public:
    [[nodiscard]] static core::Result<
        StageAxisMotionErrorContribution,
        StageAxisMotionErrorContributionError> create(
            core::StageAxisId axisId,
            core::AxisCoordinateUnit unit,
            std::optional<double> conservativeBound) {
        if (conservativeBound &&
            (!std::isfinite(*conservativeBound) || *conservativeBound < 0.0)) {
            return core::Result<
                StageAxisMotionErrorContribution,
                StageAxisMotionErrorContributionError>::failure(
                    StageAxisMotionErrorContributionError::InvalidBound);
        }

        return core::Result<
            StageAxisMotionErrorContribution,
            StageAxisMotionErrorContributionError>::success(
                StageAxisMotionErrorContribution(
                    std::move(axisId), unit, conservativeBound));
    }

    [[nodiscard]] const core::StageAxisId& axisId() const noexcept {
        return axisId_;
    }

    [[nodiscard]] core::AxisCoordinateUnit unit() const noexcept {
        return unit_;
    }

    [[nodiscard]] const std::optional<double>& conservativeBound() const noexcept {
        return conservativeBound_;
    }

private:
    StageAxisMotionErrorContribution(
        core::StageAxisId axisId,
        core::AxisCoordinateUnit unit,
        std::optional<double> conservativeBound)
        : axisId_(std::move(axisId)),
          unit_(unit),
          conservativeBound_(conservativeBound) {}

    core::StageAxisId axisId_;
    core::AxisCoordinateUnit unit_;
    std::optional<double> conservativeBound_;
};

class StageMotionErrorBudget final {
public:
    [[nodiscard]] static core::Result<
        StageMotionErrorBudget,
        StageAxisMotionErrorContributionError> create(
            std::vector<StageAxisMotionErrorContribution> contributions) {
        if (contributions.empty()) {
            return core::Result<
                StageMotionErrorBudget,
                StageAxisMotionErrorContributionError>::failure(
                    StageAxisMotionErrorContributionError::EmptyBudget);
        }

        for (std::size_t i = 0; i < contributions.size(); ++i) {
            for (std::size_t j = i + 1; j < contributions.size(); ++j) {
                if (contributions[i].axisId() == contributions[j].axisId()) {
                    return core::Result<
                        StageMotionErrorBudget,
                        StageAxisMotionErrorContributionError>::failure(
                            StageAxisMotionErrorContributionError::DuplicateAxisId);
                }
            }
        }

        return core::Result<
            StageMotionErrorBudget,
            StageAxisMotionErrorContributionError>::success(
                StageMotionErrorBudget(std::move(contributions)));
    }

    [[nodiscard]] const StageAxisMotionErrorContribution* contributionFor(
        const core::StageAxisId& axisId) const noexcept {
        const auto found = std::find_if(
            contributions_.begin(), contributions_.end(),
            [&axisId](const auto& contribution) {
                return contribution.axisId() == axisId;
            });
        return found == contributions_.end() ? nullptr : &*found;
    }

    [[nodiscard]] const std::vector<StageAxisMotionErrorContribution>&
    contributions() const noexcept {
        return contributions_;
    }

private:
    explicit StageMotionErrorBudget(
        std::vector<StageAxisMotionErrorContribution> contributions)
        : contributions_(std::move(contributions)) {}

    std::vector<StageAxisMotionErrorContribution> contributions_;
};

enum class StagePositionCommandReductionErrorCode {
    FeedbackLossUnresolved,
    FeedbackNotAppliedToOperation,
    MissingReceipt,
    InvalidFeedbackKind,
    CommandContextMismatch,
    InvalidStepStatus,
    InvalidCompletionPayload,
    WrongCompletedAction,
    CompletedAxisSetMismatch,
    ErrorBudgetAxisSetMismatch,
    ErrorBudgetUnitMismatch,
    PositionStateRejectedCommand
};

struct StagePositionCommandReductionError final {
    StagePositionCommandReductionErrorCode code;
    std::optional<core::StageAxisPositionStateError> positionStateError;
};

struct StagePositionCommandReduction final {
    core::StagePositionState position;
};

// Applies the Core-derived target estimate only after the exact correlated
// DeviceActionCompleted fact has been accepted by SerializedOperationRuntime.
// The conservative error contribution is provided per axis by the host's
// active motion calibration. Missing contributions remain unbounded.
class StagePositionCommandReducer final {
public:
    using FeedbackResult =
        core::FeedbackProcessingResult<core::StageMotionFeedbackPayload>;
    using Result = core::Result<
        StagePositionCommandReduction,
        StagePositionCommandReductionError>;

    [[nodiscard]] static Result reduceCompletedMoveAxesTo(
        const core::StagePositionState& current,
        const FeedbackResult& feedback,
        const core::StageMotionCommandContext& commandContext,
        const core::MoveAxesTo& command,
        const StageMotionErrorBudget& errorBudget,
        bool feedbackLossUnresolved) {
        if (feedbackLossUnresolved) {
            return failure(
                StagePositionCommandReductionErrorCode::FeedbackLossUnresolved);
        }

        if (feedback.disposition !=
            core::FeedbackProcessingDisposition::AppliedToOperation) {
            return failure(
                StagePositionCommandReductionErrorCode::FeedbackNotAppliedToOperation);
        }

        if (!feedback.receipt) {
            return failure(StagePositionCommandReductionErrorCode::MissingReceipt);
        }

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
            feedback.stepId != std::optional<core::StepId>(expectedOperation.stepId)) {
            return failure(
                StagePositionCommandReductionErrorCode::CommandContextMismatch);
        }

        if (!feedback.stepStatus ||
            (*feedback.stepStatus != core::StepStatus::AwaitingObservation &&
             *feedback.stepStatus != core::StepStatus::Verifying)) {
            return failure(
                StagePositionCommandReductionErrorCode::InvalidStepStatus);
        }

        const auto* completion =
            std::get_if<core::StageMotionActionCompletion>(&envelope.payload());
        if (completion == nullptr) {
            return failure(
                StagePositionCommandReductionErrorCode::InvalidCompletionPayload);
        }
        if (completion->action() != core::StageMotionAction::MoveAxesTo) {
            return failure(
                StagePositionCommandReductionErrorCode::WrongCompletedAction);
        }

        const auto& targets = command.targets();
        if (targets.size() != 2 ||
            targets.size() != completion->axes().axes().size() ||
            targets.size() != errorBudget.contributions().size()) {
            return failure(
                StagePositionCommandReductionErrorCode::CompletedAxisSetMismatch);
        }

        for (const auto& target : targets) {
            const auto& axisId = target.axisId();
            if (axisId != current.horizontal().axisId() &&
                axisId != current.transverse().axisId()) {
                return failure(
                    StagePositionCommandReductionErrorCode::CompletedAxisSetMismatch);
            }

            if (std::find(
                    completion->axes().axes().begin(),
                    completion->axes().axes().end(),
                    axisId) == completion->axes().axes().end()) {
                return failure(
                    StagePositionCommandReductionErrorCode::CompletedAxisSetMismatch);
            }

            const auto* contribution = errorBudget.contributionFor(axisId);
            if (contribution == nullptr) {
                return failure(
                    StagePositionCommandReductionErrorCode::ErrorBudgetAxisSetMismatch);
            }
            if (contribution->unit() != target.unit()) {
                return failure(
                    StagePositionCommandReductionErrorCode::ErrorBudgetUnitMismatch);
            }
        }

        auto updated = current;
        for (const auto& target : targets) {
            const auto* contribution = errorBudget.contributionFor(target.axisId());
            auto next = updated.afterCommandedPosition(
                target, contribution->conservativeBound());
            if (!next) {
                return Result::failure(StagePositionCommandReductionError{
                    StagePositionCommandReductionErrorCode::PositionStateRejectedCommand,
                    next.error()});
            }
            updated = std::move(next).value();
        }

        return Result::success(StagePositionCommandReduction{
            std::move(updated)});
    }

private:
    [[nodiscard]] static Result failure(
        StagePositionCommandReductionErrorCode code) {
        return Result::failure(StagePositionCommandReductionError{
            code, std::nullopt});
    }
};

} // namespace microscope::application::motion
