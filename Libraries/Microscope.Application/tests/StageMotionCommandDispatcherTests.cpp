#include "microscope/application/motion/StageMotionCommandDispatcher.h"
#include "microscope/application/motion/StageMotionPlanDispatcher.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"
#include "controller/MicroscopeInstance.h"

#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
using namespace microscope;

template <typename T, typename E>
T take(core::Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("stage dispatcher fixture rejected");
    }
    return std::move(result).value();
}

template <typename T>
T id(const char* value) {
    return take(T::create(value));
}

class RecordingStagePort final : public core::IStageMotionPort {
public:
    core::StageMotionSubmissionResult nextSubmission =
        core::StageMotionSubmissionResult::accepted();
    bool throwOnMoveTo{false};
    unsigned moveToCalls{0};
    unsigned moveByCalls{0};
    unsigned homeCalls{0};
    unsigned stopCalls{0};
    std::optional<core::OperationCorrelation> lastOperation;
    std::optional<std::size_t> lastMoveTargetCount;

    core::StageMotionSubmissionResult moveAxesTo(
        const core::StageMotionCommandContext& context,
        const core::MoveAxesTo& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++moveToCalls;
        lastOperation = context.operation();
        lastMoveTargetCount = command.targets().size();
        if (throwOnMoveTo) {
            throw std::runtime_error("simulated adapter submission exception");
        }
        return nextSubmission;
    }

    core::StageMotionSubmissionResult moveAxesBy(
        const core::StageMotionCommandContext&,
        const core::MoveAxesBy&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++moveByCalls;
        return nextSubmission;
    }

    core::StageMotionSubmissionResult homeAxes(
        const core::StageMotionCommandContext&,
        const core::HomeAxes&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++homeCalls;
        return nextSubmission;
    }

    core::StageMotionSubmissionResult stopAxes(
        const core::StageMotionCommandContext&,
        const core::StopAxes&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++stopCalls;
        return nextSubmission;
    }
};

struct RuntimeFixture final {
    application::runtime::RuntimeWakeSignal wake;
    core::RuntimeDeviceCorrelation device{
        id<core::MicroscopeId>("scope"),
        id<core::RuntimeId>("runtime"),
        take(core::RuntimeGeneration::create(1)),
        take(core::DeviceSessionGeneration::create(1)),
        id<core::DeviceId>("stage")};
    std::unique_ptr<core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>> runtime;

    RuntimeFixture() {
        runtime = take(core::SerializedOperationRuntime<
            core::StageMotionFeedbackPayload>::create(
                device.microscopeId, device.runtimeId, device.runtimeGeneration, 8, wake));
        take(runtime->bindOwnerThread());
        take(runtime->openDeviceSession(device));
    }

    core::StageMotionCommandContext prepare(
        const char* operationText,
        const char* stepText,
        const char* commandText) {
        const auto operation = id<core::OperationId>(operationText);
        const auto step = id<core::StepId>(stepText);
        const auto command = id<core::CommandId>(commandText);
        take(runtime->createOperation(operation));
        take(runtime->startOperation(operation));
        take(runtime->createStep(operation, step));
        return take(core::StageMotionCommandContext::create(
            device, core::OperationCorrelation{operation, step, command}));
    }
};

controller::StageSpecification controllerStageSpecification() {
    const auto xId=id<core::StageAxisId>("controller-x");
    const auto yId=id<core::StageAxisId>("controller-y");
    const auto zId=id<core::StageAxisId>("controller-z");
    auto x=take(controller::LinearAxis::create(
        xId,controller::LinearAxisRole::X,
        take(controller::NanometreRange::create(0,500'000'000)),
        100,1'000,500));
    auto y=take(controller::LinearAxis::create(
        yId,controller::LinearAxisRole::Y,
        take(controller::NanometreRange::create(0,300'000'000)),
        100,1'000,500));
    auto z=take(controller::LinearAxis::create(
        zId,controller::LinearAxisRole::Z,
        take(controller::NanometreRange::create(0,100'000'000)),
        50,500,250));
    const auto configId=id<core::StageConfigurationId>("slides");
    auto slot=take(controller::StageSlot::create(
        id<core::StageSlotId>("slot-1"),{0,0},
        take(controller::NanometreRange::create(0,75'000'000)),
        take(controller::NanometreRange::create(0,25'000'000))));
    auto config=take(controller::StageConfiguration::create(configId,
        {{xId,take(controller::NanometreRange::create(0,500'000'000))},
         {yId,take(controller::NanometreRange::create(0,300'000'000))},
         {zId,take(controller::NanometreRange::create(0,100'000'000))}},
        {},{std::move(slot)}));
    return take(controller::StageSpecification::create(
        id<core::EquipmentComponentId>("stage"),
        {std::move(x),std::move(y),std::move(z)},{},{std::move(config)}));
}

core::AxisSpecification installedAxis(
    core::StageAxisId id,double minimum,double maximum) {
    const auto range=take(core::AxisOperatingRange::create(
        minimum,maximum,core::AxisCoordinateUnit::Micrometers));
    const auto resolution=take(core::PositiveAxisResolution::create(0.1));
    return take(core::AxisSpecification::create(
        std::move(id),core::AxisMotionType::Linear,
        core::AxisCoordinateUnit::Micrometers,range,resolution,
        std::nullopt,std::nullopt,std::nullopt,
        core::AxisPositionFeedbackCapability::None));
}

core::StageSpecification installedStageSpecification() {
    return take(core::StageSpecification::create(id<core::EquipmentComponentId>("stage"),{
        installedAxis(id<core::StageAxisId>("controller-x"),0,500'000),
        installedAxis(id<core::StageAxisId>("controller-y"),0,300'000),
        installedAxis(id<core::StageAxisId>("controller-z"),0,100'000)
    }));
}

core::MoveAxesTo moveTarget() {
    const auto x = take(core::AxisPosition::create(
        id<core::StageAxisId>("x"), 1200.0,
        core::AxisCoordinateUnit::Micrometers));
    const auto y = take(core::AxisPosition::create(
        id<core::StageAxisId>("y"), 2.5,
        core::AxisCoordinateUnit::Millimeters));
    return take(core::MoveAxesTo::create({x, y}));
}

core::MoveAxesBy relativeMove() {
    return take(core::MoveAxesBy::create({
        take(core::AxisDisplacement::create(
            id<core::StageAxisId>("x"), 250.0,
            core::AxisCoordinateUnit::Micrometers)),
        take(core::AxisDisplacement::create(
            id<core::StageAxisId>("y"), -0.5,
            core::AxisCoordinateUnit::Millimeters))}));
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> acknowledgement(
    const core::StageMotionCommandContext& context) {
    const auto axes = take(core::StageAxisSet::create({
        id<core::StageAxisId>("x"), id<core::StageAxisId>("y")}));
    const auto payload = take(core::StageMotionAcknowledgement::create(
        core::StageMotionAction::MoveAxesTo, axes));
    const auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{context.runtimeDevice(), context.operation()},
        core::FeedbackKind::DeviceAcknowledged,
        core::ObservationSource::DeviceEvent));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{payload});
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    RuntimeFixture identityFixture;
    auto identityContext=identityFixture.prepare(
        "wrong-scope-operation","wrong-scope-step","wrong-scope-command");
    RecordingStagePort identityPort;
    auto identityStage=take(controller::Stage::create(
        controllerStageSpecification(),id<core::StageConfigurationId>("slides")));
    controller::MicroscopeInstance wrongMicroscope(
        id<core::MicroscopeId>("another-scope"),std::move(identityStage));
    const auto identityPlan=take(wrongMicroscope.stage().home());
    const auto identityRejected=
        application::motion::StageMotionPlanDispatcher::dispatch(
            wrongMicroscope,identityPlan,installedStageSpecification(),
            identityContext,identityPort,*identityFixture.runtime);
    check(!identityRejected&&
          std::holds_alternative<application::motion::StageMotionPlanIdentityError>(
              identityRejected.error().cause),
          "rejects dispatch from a different microscope identity");
    check(identityPort.homeCalls==0&&
          !wrongMicroscope.stage().validateForDispatch(identityPlan),
          "identity rejection does not reach hardware and releases the pending Stage plan");

    RuntimeFixture wrongDeviceFixture;
    wrongDeviceFixture.device.deviceId=id<core::DeviceId>("wrong-stage-device");
    auto wrongDeviceContext=wrongDeviceFixture.prepare(
        "wrong-device-operation","wrong-device-step","wrong-device-command");
    RecordingStagePort wrongDevicePort;
    auto wrongDeviceStage=take(controller::Stage::create(
        controllerStageSpecification(),id<core::StageConfigurationId>("slides")));
    controller::MicroscopeInstance wrongDeviceMicroscope(
        id<core::MicroscopeId>("scope"),std::move(wrongDeviceStage));
    const auto wrongDevicePlan=take(wrongDeviceMicroscope.stage().home());
    const auto wrongDeviceRejected=
        application::motion::StageMotionPlanDispatcher::dispatch(
            wrongDeviceMicroscope,wrongDevicePlan,installedStageSpecification(),
            wrongDeviceContext,wrongDevicePort,*wrongDeviceFixture.runtime);
    check(!wrongDeviceRejected&&
          std::holds_alternative<application::motion::StageMotionPlanIdentityError>(
              wrongDeviceRejected.error().cause),
          "rejects dispatch when the runtime device ID differs from the Stage component ID");
    check(wrongDevicePort.homeCalls==0&&
          !wrongDeviceMicroscope.stage().validateForDispatch(wrongDevicePlan),
          "wrong-device rejection never reaches hardware and releases the pending plan");

    RuntimeFixture planFixture;
    auto planContext=planFixture.prepare(
        "controller-plan-operation","controller-plan-step","controller-plan-command");
    RecordingStagePort planPort;
    auto controllerStage=controllerStageSpecification();
    auto installedStage=installedStageSpecification();
    auto liveStage=take(controller::Stage::create(
        controllerStage,id<core::StageConfigurationId>("slides")));
    controller::MicroscopeInstance microscopeInstance(
        id<core::MicroscopeId>("scope"),std::move(liveStage));
    auto& ownedStage=microscopeInstance.stage();
    const auto homePlan=take(ownedStage.home());
    const auto planReceipt=take(
        application::motion::StageMotionPlanDispatcher::dispatch(
            microscopeInstance,homePlan,installedStage,planContext,
            planPort,*planFixture.runtime));
    check(planReceipt.planSequence==homePlan.sequence&&
          planReceipt.dispatch.action==core::StageMotionAction::HomeAxes&&
          planPort.homeCalls==1,
          "Controller Stage home plans reach the existing correlated Core motion dispatcher");

    std::vector<core::AxisPositionObservation> homePositions;
    for(const auto& axisId:homePlan.axesToHome) {
        auto axisPosition=take(core::AxisPosition::create(
            axisId,0.0,core::AxisCoordinateUnit::Micrometers));
        homePositions.push_back(take(core::AxisPositionObservation::create(
            std::move(axisPosition),0.0,core::StagePositionEvidence::PhysicalEndstop)));
    }
    const auto homeReference=take(
        core::StageHomeReferenceObservation::create(std::move(homePositions)));
    const auto homeCompletion=take(core::StageMotionActionCompletion::create(
        core::StageMotionAction::HomeAxes,
        take(core::StageAxisSet::create(homePlan.axesToHome))));
    check(static_cast<bool>(ownedStage.recordCompletedHome(
        homePlan,homeCompletion,homeReference)),
        "only correlated home completion plus physical reference evidence establishes global origin");

    RuntimeFixture slotMoveFixture;
    auto slotMoveContext=slotMoveFixture.prepare(
        "controller-slot-operation","controller-slot-step","controller-slot-command");
    RecordingStagePort slotMovePort;
    const auto slotMovePlan=take(ownedStage.moveTo(
        controller::SlotRelativePosition{20'000'000,10'000'000},
        id<core::StageSlotId>("slot-1")));
    const auto slotDispatch=take(
        application::motion::StageMotionPlanDispatcher::dispatch(
            microscopeInstance,slotMovePlan,installedStageSpecification(),slotMoveContext,
            slotMovePort,*slotMoveFixture.runtime));
    check(slotDispatch.planSequence==slotMovePlan.sequence&&
          slotDispatch.dispatch.action==core::StageMotionAction::MoveAxesTo&&
          slotMovePort.moveToCalls==1&&slotMovePort.lastMoveTargetCount==
              std::optional<std::size_t>(2),
          "slot-relative Stage plans dispatch coordinated XY targets through the existing motion port");
    check(ownedStage.currentPosition().x==0&&ownedStage.currentPosition().y==0,
        "adapter submission acceptance alone does not advance Stage position");

    const auto moveCompletion=take(core::StageMotionActionCompletion::create(
        core::StageMotionAction::MoveAxesTo,
        take(core::StageAxisSet::create({
            id<core::StageAxisId>("controller-x"),
            id<core::StageAxisId>("controller-y")}))));
    check(static_cast<bool>(ownedStage.recordCompletedMotion(
        slotMovePlan,moveCompletion)),
        "correlated MoveAxesTo completion reconciles the Stage position");
    check(ownedStage.currentPosition().x==20'000'000&&
          ownedStage.currentPosition().y==10'000'000&&
          ownedStage.currentSlotId()==id<core::StageSlotId>("slot-1"),
        "completed slot-relative motion updates global nanometre coordinates and derived slot identity");

    RuntimeFixture acceptedFixture;
    auto acceptedContext = acceptedFixture.prepare(
        "move-operation", "move-step", "move-command");
    RecordingStagePort acceptedPort;
    const auto command = moveTarget();

    const auto accepted = take(
        application::motion::StageMotionCommandDispatcher::moveAxesTo(
            acceptedContext, command, acceptedPort, *acceptedFixture.runtime));
    check(accepted.action == core::StageMotionAction::MoveAxesTo &&
          accepted.submission.status() == core::StageMotionSubmissionStatus::Accepted &&
          accepted.stepStatus == core::StepStatus::AdapterAccepted,
          "accepted adapter submission advances only to AdapterAccepted");
    check(acceptedPort.moveToCalls == 1 &&
          acceptedPort.lastMoveTargetCount == std::optional<std::size_t>(2) &&
          acceptedPort.lastOperation &&
          acceptedPort.lastOperation->commandId == acceptedContext.operation().commandId,
          "typed coordinated X/Y target and exact command context reach the motion port");

    const auto queuedAcknowledgement = acknowledgement(acceptedContext);
    check(acceptedFixture.runtime->feedbackIngress().submit(queuedAcknowledgement) ==
              core::FeedbackSubmitStatus::Enqueued,
          "port feedback is submitted through the runtime ingress");
    const auto feedback = take(acceptedFixture.runtime->processNextFeedback());
    check(feedback && feedback->disposition ==
              core::FeedbackProcessingDisposition::AppliedToOperation &&
          feedback->stepStatus == core::StepStatus::DeviceAcknowledged,
          "device acknowledgement is a later feedback-driven transition");

    RuntimeFixture rejectedFixture;
    auto rejectedContext = rejectedFixture.prepare(
        "rejected-operation", "rejected-step", "rejected-command");
    RecordingStagePort rejectedPort;
    rejectedPort.nextSubmission = core::StageMotionSubmissionResult::rejected(
        core::StageMotionSubmissionRejection::UnsupportedOperation);
    const auto rejected = take(
        application::motion::StageMotionCommandDispatcher::homeAxes(
            rejectedContext,
            core::HomeAxes::create(take(core::StageAxisSet::create({
                id<core::StageAxisId>("x"), id<core::StageAxisId>("y")}))),
            rejectedPort, *rejectedFixture.runtime));
    check(rejected.action == core::StageMotionAction::HomeAxes &&
          rejected.submission.status() == core::StageMotionSubmissionStatus::Rejected &&
          rejected.submission.rejection() ==
              std::optional<core::StageMotionSubmissionRejection>(
                  core::StageMotionSubmissionRejection::UnsupportedOperation) &&
          rejected.stepStatus == core::StepStatus::Failed &&
          rejectedFixture.runtime->operationSnapshot(
              id<core::OperationId>("rejected-operation")).value().status() ==
              core::OperationStatus::Failed,
          "adapter rejection is recorded as failure and never mistaken for a homing result");

    RuntimeFixture relativeFixture;
    auto relativeContext = relativeFixture.prepare(
        "relative-operation", "relative-step", "relative-command");
    RecordingStagePort relativePort;
    const auto relative = application::motion::StageMotionCommandDispatcher::moveAxesBy(
        relativeContext, relativeMove(), relativePort, *relativeFixture.runtime);
    check(bool(relative) &&
          relative.value().action == core::StageMotionAction::MoveAxesBy &&
          relative.value().submission.status() == core::StageMotionSubmissionStatus::Accepted &&
          relative.value().stepStatus == core::StepStatus::AdapterAccepted &&
          relativePort.moveByCalls == 1,
          "relative physical displacement is dispatched through the typed adapter port");

    RuntimeFixture stopFixture;
    auto stopContext = stopFixture.prepare(
        "stop-operation", "stop-step", "stop-command");
    RecordingStagePort stopPort;
    const auto stopAxes = core::StopAxes::create(take(core::StageAxisSet::create({
        id<core::StageAxisId>("x"), id<core::StageAxisId>("y")})));
    const auto stop = application::motion::StageMotionCommandDispatcher::stopAxes(
        stopContext, stopAxes, stopPort, *stopFixture.runtime);
    check(bool(stop) &&
          stop.value().action == core::StageMotionAction::StopAxes &&
          stop.value().submission.status() == core::StageMotionSubmissionStatus::Accepted &&
          stop.value().stepStatus == core::StepStatus::AdapterAccepted &&
          stopPort.stopCalls == 1,
          "controlled stop is dispatched as a distinct typed adapter command");

    RuntimeFixture throwingFixture;
    auto throwingContext = throwingFixture.prepare(
        "throw-operation", "throw-step", "throw-command");
    RecordingStagePort throwingPort;
    throwingPort.throwOnMoveTo = true;
    const auto threw = application::motion::StageMotionCommandDispatcher::moveAxesTo(
        throwingContext, command, throwingPort, *throwingFixture.runtime);
    check(!threw &&
          threw.error().code ==
              application::motion::StageMotionDispatchErrorCode::AdapterSubmissionOutcomeUnknown &&
          throwingFixture.runtime->stepStatus(
              id<core::OperationId>("throw-operation"),
              id<core::StepId>("throw-step")).value() == core::StepStatus::Dispatched &&
          throwingFixture.runtime->operationSnapshot(
              id<core::OperationId>("throw-operation")).value().status() ==
              core::OperationStatus::Running,
          "a throwing adapter submission remains indeterminate and is not mislabeled as rejection");

    RuntimeFixture wrongSessionFixture;
    auto wrongContext = wrongSessionFixture.prepare(
        "wrong-session-operation", "wrong-session-step", "wrong-session-command");
    auto wrongDevice = wrongContext.runtimeDevice();
    wrongDevice.deviceSessionGeneration =
        take(core::DeviceSessionGeneration::create(2));
    auto staleContext = take(core::StageMotionCommandContext::create(
        wrongDevice, wrongContext.operation()));
    RecordingStagePort untouchedPort;
    const auto wrongSession = application::motion::StageMotionCommandDispatcher::moveAxesTo(
        staleContext, command, untouchedPort, *wrongSessionFixture.runtime);
    check(!wrongSession &&
          wrongSession.error().code ==
              application::motion::StageMotionDispatchErrorCode::RuntimeDispatchRejected &&
          wrongSession.error().runtimeError ==
              std::optional<core::SerializedOperationRuntimeError>(
                  core::SerializedOperationRuntimeError::StaleDeviceSession) &&
          untouchedPort.moveToCalls == 0,
          "stale device session is rejected before the driver port is invoked");

    if (failures != 0) {
        std::cerr << failures << " stage motion dispatcher test(s) failed\n";
        return 1;
    }
    std::cout << "All stage motion dispatcher tests passed\n";
    return 0;
}
