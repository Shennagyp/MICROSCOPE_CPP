#pragma once
#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/stage/StagePosition3D.h"
#include "microscope/core/runtime/SerializedOperationRuntime.h"
#include <algorithm>
#include <optional>
#include <utility>
#include <variant>

namespace microscope::application::motion {
enum class StagePosition3DFeedbackDisposition{
    PositionObservationApplied,IgnoredNonCurrentFeedback,IgnoredNonPositionPayload,
    IgnoredAxisOutsideVolume,InvalidatedAfterFeedbackLoss,HomeReferenceApplied
};
enum class StagePosition3DFeedbackErrorCode{
    MissingReceipt,PositionStateRejectedObservation,FeedbackNotAppliedToOperation,
    InvalidHomeReferenceFact,InvalidHomeReferencePayload,HomeStepNotVerified,
    RuntimeStepQueryFailed,RequestedAxesDoNotMatchStage,HomeReferenceAxisSetMismatch
};
struct StagePosition3DFeedbackError final{
    StagePosition3DFeedbackErrorCode code;
    std::optional<core::StagePosition3DError> positionError;
    std::optional<core::SerializedOperationRuntimeError> runtimeError;
};
struct StagePosition3DFeedbackReduction final{
    core::StagePosition3D position;
    StagePosition3DFeedbackDisposition disposition;
};

// Reducer for the microscope's independent X/Y/focus-Z machine coordinates.
// The serialized worker remains the sole writer; the returned aggregate is immutable.
class StagePosition3DFeedbackReducer final{
public:
    using FeedbackResult=core::FeedbackProcessingResult<core::StageMotionFeedbackPayload>;
    using Runtime=core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>;
    using Result=core::Result<StagePosition3DFeedbackReduction,StagePosition3DFeedbackError>;

    static Result reduce(const core::StagePosition3D& current,const FeedbackResult& feedback,
        bool feedbackLossUnresolved){
        if(feedbackLossUnresolved)
            return Result::success({current.invalidateMachineReference(),
                StagePosition3DFeedbackDisposition::InvalidatedAfterFeedbackLoss});
        if(feedback.disposition!=core::FeedbackProcessingDisposition::AppliedToOperation&&
           feedback.disposition!=core::FeedbackProcessingDisposition::UncorrelatedDeviceFeedback)
            return Result::success({current,StagePosition3DFeedbackDisposition::IgnoredNonCurrentFeedback});
        if(!feedback.receipt)
            return Result::failure({StagePosition3DFeedbackErrorCode::MissingReceipt,std::nullopt,std::nullopt});
        const auto& envelope=feedback.receipt->feedback();
        const auto* observation=std::get_if<core::AxisPositionObservation>(&envelope.payload());
        if(!observation)
            return Result::success({current,StagePosition3DFeedbackDisposition::IgnoredNonPositionPayload});
        const auto& axis=observation->position().axisId();
        if(axis!=current.x().axisId()&&axis!=current.y().axisId()&&axis!=current.z().axisId())
            return Result::success({current,StagePosition3DFeedbackDisposition::IgnoredAxisOutsideVolume});
        auto updated=current.afterPositionObservation(*observation,envelope.metadata().source());
        if(!updated)
            return Result::failure({StagePosition3DFeedbackErrorCode::PositionStateRejectedObservation,
                updated.error(),std::nullopt});
        return Result::success({std::move(updated).value(),
            StagePosition3DFeedbackDisposition::PositionObservationApplied});
    }

    // A complete XYZ HomeReferenceEstablished fact is applied only after the
    // exact correlated operation step is Verified by the Core runtime.
    static Result applyVerifiedHomeReference(const core::StagePosition3D& current,
        const FeedbackResult& feedback,const core::StageMotionCommandContext& context,
        const core::HomeAxes& requestedHome,const Runtime& runtime){
        if(feedback.disposition!=core::FeedbackProcessingDisposition::AppliedToOperation)
            return Result::failure({StagePosition3DFeedbackErrorCode::FeedbackNotAppliedToOperation,std::nullopt,std::nullopt});
        if(!feedback.receipt)
            return Result::failure({StagePosition3DFeedbackErrorCode::MissingReceipt,std::nullopt,std::nullopt});
        const auto& envelope=feedback.receipt->feedback();
        const auto& metadata=envelope.metadata();
        const auto& actual=metadata.correlation().runtimeDevice;
        const auto& expected=context.runtimeDevice();
        const auto& operation=metadata.correlation().operation;
        const auto& expectedOperation=context.operation();
        const bool sameDevice=actual.microscopeId==expected.microscopeId&&
            actual.runtimeId==expected.runtimeId&&actual.runtimeGeneration==expected.runtimeGeneration&&
            actual.deviceId==expected.deviceId&&actual.deviceSessionGeneration==expected.deviceSessionGeneration;
        if(metadata.kind()!=core::FeedbackKind::HomeReferenceEstablished||!sameDevice||!operation||
            operation->operationId!=expectedOperation.operationId||
            operation->stepId!=expectedOperation.stepId||operation->commandId!=expectedOperation.commandId||
            feedback.operationId!=std::optional<core::OperationId>(expectedOperation.operationId)||
            feedback.stepId!=std::optional<core::StepId>(expectedOperation.stepId))
            return Result::failure({StagePosition3DFeedbackErrorCode::InvalidHomeReferenceFact,std::nullopt,std::nullopt});

        auto step=runtime.stepStatus(expectedOperation.operationId,expectedOperation.stepId);
        if(!step)return Result::failure({StagePosition3DFeedbackErrorCode::RuntimeStepQueryFailed,std::nullopt,step.error()});
        if(step.value()!=core::StepStatus::Verified)
            return Result::failure({StagePosition3DFeedbackErrorCode::HomeStepNotVerified,std::nullopt,std::nullopt});

        const auto* home=std::get_if<core::StageHomeReferenceObservation>(&envelope.payload());
        if(!home)return Result::failure({StagePosition3DFeedbackErrorCode::InvalidHomeReferencePayload,std::nullopt,std::nullopt});
        // A home fact may reference any non-empty subset of this machine's
        // three axes. Slide-plane navigation requests exactly X+Y, leaving
        // focus Z untouched; a separate focus workflow may request Z alone.
        const auto& axes=requestedHome.axes().axes();
        if(axes.empty()||axes.size()>3||
            std::any_of(axes.begin(),axes.end(),[&](const auto& axis){
                return axis!=current.x().axisId()&&axis!=current.y().axisId()&&
                    axis!=current.z().axisId();
            }))
            return Result::failure({StagePosition3DFeedbackErrorCode::RequestedAxesDoNotMatchStage,std::nullopt,std::nullopt});
        if(!home->matchesAxes(requestedHome.axes()))
            return Result::failure({StagePosition3DFeedbackErrorCode::HomeReferenceAxisSetMismatch,std::nullopt,std::nullopt});

        auto updated=current;
        for(const auto& observation:home->positions()){
            auto next=updated.afterVerifiedHomingObservation(observation,metadata.source());
            if(!next)return Result::failure({StagePosition3DFeedbackErrorCode::PositionStateRejectedObservation,
                next.error(),std::nullopt});
            updated=std::move(next).value();
        }
        return Result::success({std::move(updated),StagePosition3DFeedbackDisposition::HomeReferenceApplied});
    }

    static core::StagePosition3D invalidateForFeedbackLoss(const core::StagePosition3D& current){
        return current.invalidateMachineReference();
    }
};
} // namespace microscope::application::motion
