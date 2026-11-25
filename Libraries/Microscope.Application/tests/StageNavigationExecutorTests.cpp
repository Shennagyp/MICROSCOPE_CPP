#include "microscope/application/motion/StageNavigationExecutor.h"
#include "microscope/application/motion/StagePositionFeedbackReducer.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace {
using namespace microscope;
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename T, typename E>
T take(core::Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("stage navigation executor fixture rejected");
    }
    return std::move(result).value();
}

template <typename T>
T id(const char* value) {
    return take(T::create(value));
}

core::AxisSpecification axis(const char* name) {
    return take(core::AxisSpecification::create(
        id<core::StageAxisId>(name),
        core::AxisMotionType::Linear,
        core::AxisCoordinateUnit::Micrometers,
        take(core::AxisOperatingRange::create(
            -100000, 150000, core::AxisCoordinateUnit::Micrometers)),
        take(core::PositiveAxisResolution::create(0.1)),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        core::AxisPositionFeedbackCapability::DeviceReported));
}

core::DualSlideCarrierSpecification carrierSpecification() {
    return take(core::DualSlideCarrierSpecification::create(
        take(core::StageSpecification::create(
            id<core::EquipmentComponentId>("stage"), {axis("x"), axis("y")})),
        id<core::StageAxisId>("x"),
        id<core::StageAxisId>("y"),
        core::CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
}

core::SlideRegistration registration(
    const char* slideId,
    core::DualSlideSlot slot,
    double originX,
    const char* revision) {
    const auto geometry = take(core::SlideGeometry::create(75000, 25000));
    const auto source = core::ObservationSource::HardwareReadback;
    const auto tl = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::TopLeft, {originX, 20000}, source, 0.25));
    const auto tr = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::TopRight, {originX + 75000, 20000}, source, 0.25));
    const auto br = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::BottomRight, {originX + 75000, 45000}, source, 0.25));
    const auto bl = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::BottomLeft, {originX, 45000}, source, 0.25));
    return take(core::SlideRegistration::create(
        id<core::SlideId>(slideId),
        id<core::CalibrationRevisionId>(revision),
        slot, geometry, {tl, tr, br, bl}, 0.5));
}

core::DualSlideCarrierState carrier() {
    const auto specification = carrierSpecification();
    const auto left = registration(
        "slide-left", core::DualSlideSlot::Left, 5000, "left-v1");
    const auto right = registration(
        "slide-right", core::DualSlideSlot::Right, 95000, "right-v1");
    return take(core::DualSlideCarrierState::create(
        specification,
        take(core::SlideSlotState::create(
            core::DualSlideSlot::Left, id<core::SlideId>("slide-left"), left)),
        take(core::SlideSlotState::create(
            core::DualSlideSlot::Right, id<core::SlideId>("slide-right"), right))));
}

core::AxisPositionObservation positionObservation(
    const char* axisId,
    double value,
    double uncertainty,
    core::StagePositionEvidence evidence =
        core::StagePositionEvidence::EncoderMeasured) {
    return take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>(axisId), value,
            core::AxisCoordinateUnit::Micrometers)),
        uncertainty, evidence));
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> homeReference(
    const core::StageMotionCommandContext& context) {
    auto payload = take(core::StageHomeReferenceObservation::create({
        positionObservation("x", 0.0, 0.5, core::StagePositionEvidence::ControllerReported),
        positionObservation("y", 0.0, 0.25, core::StagePositionEvidence::EncoderMeasured)}));
    auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{context.runtimeDevice(), context.operation()},
        core::FeedbackKind::HomeReferenceEstablished,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{std::move(payload)});
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> axisPositionFeedback(
    const core::StageMotionCommandContext& context,
    const char* axisId,
    double value) {
    auto payload = positionObservation(axisId, value, 0.2);
    auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{context.runtimeDevice(), context.operation()},
        core::FeedbackKind::DeviceStateObserved,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{std::move(payload)});
}

class RecordingStagePort final : public core::IStageMotionPort {
public:
    unsigned moveToCalls{0};
    unsigned homeCalls{0};
    std::optional<core::MoveAxesTo> lastMove;
    std::optional<core::HomeAxes> lastHome;

    core::StageMotionSubmissionResult moveAxesTo(
        const core::StageMotionCommandContext&,
        const core::MoveAxesTo& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++moveToCalls;
        lastMove = command;
        return core::StageMotionSubmissionResult::accepted();
    }

    core::StageMotionSubmissionResult moveAxesBy(
        const core::StageMotionCommandContext&,
        const core::MoveAxesBy&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        return core::StageMotionSubmissionResult::rejected(
            core::StageMotionSubmissionRejection::UnsupportedOperation);
    }

    core::StageMotionSubmissionResult homeAxes(
        const core::StageMotionCommandContext&,
        const core::HomeAxes& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++homeCalls;
        lastHome = command;
        return core::StageMotionSubmissionResult::accepted();
    }

    core::StageMotionSubmissionResult stopAxes(
        const core::StageMotionCommandContext&,
        const core::StopAxes&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        return core::StageMotionSubmissionResult::accepted();
    }
};

struct RuntimeFixture final {
    application::runtime::RuntimeWakeSignal wake;
    core::RuntimeDeviceCorrelation device{
        id<core::MicroscopeId>("scope"),
        id<core::RuntimeId>("runtime"),
        take(core::RuntimeGeneration::create(1)),
        take(core::DeviceSessionGeneration::create(1)),
        id<core::DeviceId>("stage-device")};
    std::unique_ptr<core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>> runtime;

    RuntimeFixture() {
        runtime = take(core::SerializedOperationRuntime<
            core::StageMotionFeedbackPayload>::create(
                device.microscopeId, device.runtimeId, device.runtimeGeneration, 16, wake));
        take(runtime->bindOwnerThread());
        take(runtime->openDeviceSession(device));
    }

    void createRunningOperation(const char* operationId, const char* firstStepId) {
        take(runtime->createOperation(id<core::OperationId>(operationId)));
        take(runtime->startOperation(id<core::OperationId>(operationId)));
        take(runtime->createStep(
            id<core::OperationId>(operationId), id<core::StepId>(firstStepId)));
    }
};

void testDirectNavigation() {
    RuntimeFixture fixture;
    const auto installedCarrier = carrier();
    auto position = take(core::StagePositionState::create(
        installedCarrier.specification()));
    position = take(position.afterVerifiedHomingObservation(
        positionObservation("x", 25000.0, 0.5),
        core::ObservationSource::HardwareReadback));
    position = take(position.afterVerifiedHomingObservation(
        positionObservation("y", 30000.0, 0.5),
        core::ObservationSource::HardwareReadback));
    const auto policy = take(core::StageHomingPolicy::create(5.0, 5.0));

    fixture.createRunningOperation("direct-operation", "direct-step");
    RecordingStagePort port;

    auto execution = take(application::motion::StageNavigationExecutor::begin(
        installedCarrier,
        id<core::SlideId>("slide-left"),
        {20000.0, 10000.0},
        position,
        policy,
        fixture.device,
        id<core::OperationId>("direct-operation"),
        id<core::StepId>("direct-step"),
        id<core::CommandId>("direct-command"),
        port,
        *fixture.runtime));

    check(std::holds_alternative<core::StageDirectMovePlan>(execution.plan),
          "valid position reference selects a direct coordinated move");
    check(port.moveToCalls == 1 && port.homeCalls == 0 &&
          port.lastMove && port.lastMove->targets().size() == 2,
          "direct plan is dispatched as one coordinated X/Y MoveAxesTo");
    check(std::holds_alternative<application::motion::StageMotionDispatchReceipt>(
              execution.dispatch) &&
          std::get<application::motion::StageMotionDispatchReceipt>(
              execution.dispatch).stepStatus == core::StepStatus::AdapterAccepted,
          "execution receipt preserves adapter acceptance without claiming arrival");

    auto observationX = axisPositionFeedback(execution.commandContext, "x", 25000.0);
    auto observationY = axisPositionFeedback(execution.commandContext, "y", 30000.0);
    check(fixture.runtime->feedbackIngress().submit(std::move(observationX)) ==
              core::FeedbackSubmitStatus::Enqueued &&
          fixture.runtime->feedbackIngress().submit(std::move(observationY)) ==
              core::FeedbackSubmitStatus::Enqueued,
          "both axis arrival observations enter the bounded feedback inbox");

    auto xResult = take(fixture.runtime->processNextFeedback());
    auto yResult = take(fixture.runtime->processNextFeedback());
    check(xResult && yResult &&
          xResult->stepStatus == core::StepStatus::Verifying &&
          yResult->stepStatus == core::StepStatus::Verifying,
          "separate X/Y observations keep the coordinated move in Verifying");
    take(fixture.runtime->recordStepVerification(
        id<core::OperationId>("direct-operation"),
        id<core::StepId>("direct-step"), true));

    auto completed = take(application::motion::StageNavigationExecutor::complete(
        execution, *fixture.runtime));
    check(completed.status() == core::OperationStatus::Succeeded,
          "verified direct navigation finalizes the operation");
}

void testHomeThenRestoredMove() {
    RuntimeFixture fixture;
    const auto installedCarrier = carrier();
    auto unknownPosition = take(core::StagePositionState::create(
        installedCarrier.specification()));
    const auto policy = take(core::StageHomingPolicy::create(5.0, 5.0));

    fixture.createRunningOperation("home-operation", "home-step");
    RecordingStagePort port;

    auto homeExecution = take(application::motion::StageNavigationExecutor::begin(
        installedCarrier,
        id<core::SlideId>("slide-left"),
        {20000.0, 10000.0},
        unknownPosition,
        policy,
        fixture.device,
        id<core::OperationId>("home-operation"),
        id<core::StepId>("home-step"),
        id<core::CommandId>("home-command"),
        port,
        *fixture.runtime));

    check(std::holds_alternative<core::StageHomeThenMovePlan>(homeExecution.plan) &&
          port.homeCalls == 1 && port.moveToCalls == 0,
          "unknown reference dispatches HomeAxes before any absolute slide target");

    const auto homeFeedback = homeReference(homeExecution.commandContext);
    check(fixture.runtime->feedbackIngress().submit(homeFeedback) ==
              core::FeedbackSubmitStatus::Enqueued,
          "complete home-reference fact is accepted for the active home step");
    auto homeResult = take(fixture.runtime->processNextFeedback());
    check(homeResult && homeResult->stepStatus == core::StepStatus::Verifying,
          "home-reference fact moves the home step into verification");

    take(fixture.runtime->recordStepVerification(
        id<core::OperationId>("home-operation"),
        id<core::StepId>("home-step"), true));
    const auto& homePlan = std::get<core::StageHomeThenMovePlan>(homeExecution.plan);
    auto referenced = take(application::motion::StagePositionFeedbackReducer::
        applyVerifiedHomeReference(
            unknownPosition,
            *homeResult,
            homeExecution.commandContext,
            homePlan.home,
            *fixture.runtime));

    check(referenced.position.horizontal().positionIsUsable() &&
          referenced.position.transverse().positionIsUsable(),
          "verified home evidence establishes both stage-plane references");

    take(fixture.runtime->createStep(
        id<core::OperationId>("home-operation"),
        id<core::StepId>("restored-move-step")));
    auto moveExecution = take(
        application::motion::StageNavigationExecutor::continueAfterVerifiedHoming(
            homeExecution,
            installedCarrier,
            referenced.position,
            policy,
            id<core::StepId>("restored-move-step"),
            id<core::CommandId>("restored-move-command"),
            port,
            *fixture.runtime));

    check(moveExecution.homingPerformed &&
          std::holds_alternative<core::StageDirectMovePlan>(moveExecution.plan) &&
          port.homeCalls == 1 && port.moveToCalls == 1,
          "verified homing restores the preserved slide-local target as a new absolute move");
    check(port.lastMove &&
          std::abs(port.lastMove->targets()[0].value() - 25000.0) < 1e-9 &&
          std::abs(port.lastMove->targets()[1].value() - 30000.0) < 1e-9,
          "restored stage target is recalculated from slide-local coordinates");

    auto observationX = axisPositionFeedback(moveExecution.commandContext, "x", 25000.0);
    auto observationY = axisPositionFeedback(moveExecution.commandContext, "y", 30000.0);
    check(fixture.runtime->feedbackIngress().submit(std::move(observationX)) ==
              core::FeedbackSubmitStatus::Enqueued &&
          fixture.runtime->feedbackIngress().submit(std::move(observationY)) ==
              core::FeedbackSubmitStatus::Enqueued,
          "restored move receives independent X/Y feedback");

    auto xResult = take(fixture.runtime->processNextFeedback());
    auto afterX = take(application::motion::StagePositionFeedbackReducer::reduce(
        referenced.position, *xResult, fixture.runtime->feedbackLossLatched()));
    auto yResult = take(fixture.runtime->processNextFeedback());
    auto afterY = take(application::motion::StagePositionFeedbackReducer::reduce(
        afterX.position, *yResult, fixture.runtime->feedbackLossLatched()));
    check(xResult && yResult &&
          std::abs(afterY.position.horizontal().position()->value() - 25000.0) < 1e-9 &&
          std::abs(afterY.position.transverse().position()->value() - 30000.0) < 1e-9,
          "feedback reduction records the final physical X/Y position");

    take(fixture.runtime->recordStepVerification(
        id<core::OperationId>("home-operation"),
        id<core::StepId>("restored-move-step"), true));
    auto completed = take(application::motion::StageNavigationExecutor::complete(
        moveExecution, *fixture.runtime));
    check(completed.status() == core::OperationStatus::Succeeded,
          "home plus restored move complete only after both operation steps are verified");
}

void testHomeContinuationRequiresVerifiedStep() {
    RuntimeFixture fixture;
    const auto installedCarrier = carrier();
    auto unknownPosition = take(core::StagePositionState::create(
        installedCarrier.specification()));
    const auto policy = take(core::StageHomingPolicy::create(5.0, 5.0));
    fixture.createRunningOperation("guard-operation", "guard-home-step");
    RecordingStagePort port;

    auto execution = take(application::motion::StageNavigationExecutor::begin(
        installedCarrier,
        id<core::SlideId>("slide-left"),
        {20000.0, 10000.0},
        unknownPosition,
        policy,
        fixture.device,
        id<core::OperationId>("guard-operation"),
        id<core::StepId>("guard-home-step"),
        id<core::CommandId>("guard-home-command"),
        port,
        *fixture.runtime));

    take(fixture.runtime->createStep(
        id<core::OperationId>("guard-operation"),
        id<core::StepId>("guard-move-step")));
    auto continuation = application::motion::StageNavigationExecutor::
        continueAfterVerifiedHoming(
            execution,
            installedCarrier,
            unknownPosition,
            policy,
            id<core::StepId>("guard-move-step"),
            id<core::CommandId>("guard-move-command"),
            port,
            *fixture.runtime);
    check(!continuation &&
          continuation.error().code ==
              application::motion::StageNavigationExecutionErrorCode::HomeStepNotVerified &&
          port.moveToCalls == 0,
          "the restored move cannot dispatch before the home step is Verified");
}
} // namespace

int main() {
    testDirectNavigation();
    testHomeThenRestoredMove();
    testHomeContinuationRequiresVerifiedStep();
    if (failures != 0) {
        std::cerr << failures << " stage navigation executor test(s) failed\n";
        return 1;
    }
    std::cout << "All stage navigation executor tests passed\n";
    return 0;
}
