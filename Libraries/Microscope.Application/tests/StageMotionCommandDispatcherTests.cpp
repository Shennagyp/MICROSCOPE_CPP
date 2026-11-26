#include "microscope/application/motion/StageMotionCommandDispatcher.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"

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
