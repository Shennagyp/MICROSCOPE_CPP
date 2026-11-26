#include "microscope/application/motion/StageWorkingSurfaceNavigationRuntime.h"
#include "microscope/application/motion/StageWorkingSurfaceNavigationControlQueue.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
using namespace microscope;

int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template<class T, class E>
T take(core::Result<T, E> result) {
    if (!result) throw std::runtime_error("working-surface runtime fixture rejected");
    return std::move(result).value();
}
template<class T>
T id(const char* value) { return take(T::create(value)); }

core::AxisSpecification axis(const char* name) {
    return take(core::AxisSpecification::create(
        id<core::StageAxisId>(name),
        core::AxisMotionType::Linear,
        core::AxisCoordinateUnit::Micrometers,
        take(core::AxisOperatingRange::create(
            -100000.0, 150000.0, core::AxisCoordinateUnit::Micrometers)),
        take(core::PositiveAxisResolution::create(0.1)),
        std::nullopt, std::nullopt, std::nullopt,
        core::AxisPositionFeedbackCapability::DeviceReported));
}

core::StageSpecification stageSpecification() {
    return take(core::StageSpecification::create(
        id<core::EquipmentComponentId>("stage"),
        {axis("x"), axis("y"), axis("focus-z")}));
}

core::StageWorkingSurface2D workingSurface(const core::StageSpecification& stage) {
    auto surface = take(core::StageWorkingSurface2D::createEmpty(
        stage, id<core::StageAxisId>("x"), id<core::StageAxisId>("y"),
        {0.0, 0.0}, take(core::StageTravelRange2D::create(150000.0, 60000.0))));

    // A is a +X length / +Y width rectangle.
    surface = take(surface.withSlideRectangle(take(
        core::SlideRectangleDefinition2D::create(
            id<core::SlideId>("slide-a"), id<core::CalibrationRevisionId>("slide-a-v1"),
            {10000.0, 20000.0}, {85000.0, 45000.0},
            core::SlideRectangleLengthAxis::StageX, 75000.0, 25000.0))));

    // B uses stage Y as its length axis and touches A at Y=20000.
    surface = take(surface.withSlideRectangle(take(
        core::SlideRectangleDefinition2D::create(
            id<core::SlideId>("slide-b"), id<core::CalibrationRevisionId>("slide-b-v1"),
            {0.0, 0.0}, {15000.0, 20000.0},
            core::SlideRectangleLengthAxis::StageY, 20000.0, 15000.0))));

    // C is an independently drawn third slide; it has no fixed carrier slot.
    surface = take(surface.withSlideRectangle(take(
        core::SlideRectangleDefinition2D::create(
            id<core::SlideId>("slide-c"), id<core::CalibrationRevisionId>("slide-c-v1"),
            {100000.0, 0.0}, {120000.0, 15000.0},
            core::SlideRectangleLengthAxis::StageX, 20000.0, 15000.0))));
    return surface;
}

core::AxisPositionObservation observation(
    const char* axisId, double value, double uncertainty,
    core::StagePositionEvidence evidence = core::StagePositionEvidence::EncoderMeasured) {
    return take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>(axisId), value,
            core::AxisCoordinateUnit::Micrometers)),
        uncertainty, evidence));
}

core::StagePosition3D referencedPosition(
    const core::StageWorkingSurface2D& surface,
    const core::StageSpecification& stage) {
    auto position = take(core::StagePosition3D::unknown(
        surface, stage, id<core::StageAxisId>("focus-z")));
    position = take(position.afterVerifiedHomingObservation(
        observation("x", 20000.0, 0.2), core::ObservationSource::HardwareReadback));
    position = take(position.afterVerifiedHomingObservation(
        observation("y", 25000.0, 0.2), core::ObservationSource::HardwareReadback));
    return position;
}

application::motion::StageMotionErrorBudget errorBudget() {
    const auto x = take(application::motion::StageAxisMotionErrorContribution::create(
        id<core::StageAxisId>("x"), core::AxisCoordinateUnit::Micrometers, 0.1));
    const auto y = take(application::motion::StageAxisMotionErrorContribution::create(
        id<core::StageAxisId>("y"), core::AxisCoordinateUnit::Micrometers, 0.1));
    return take(application::motion::StageMotionErrorBudget::create({x, y}));
}

core::StageXYHomingPolicy homingPolicy() {
    return take(core::StageXYHomingPolicy::create(5.0, 5.0));
}

application::motion::StageWorkingSurfaceNavigationVerificationPolicy verificationPolicy(
    application::motion::StageWorkingSurfaceNavigationPositionVerificationMode mode) {
    return take(application::motion::StageWorkingSurfaceNavigationVerificationPolicy::create(
        2.0, 2.0, 0.5, 0.5, mode));
}

application::motion::StageWorkingSurfaceNavigationOperationIdentity navigationIds(
    const char* operation, const char* step, const char* command,
    const char* restoredStep, const char* restoredCommand) {
    return {
        id<core::OperationId>(operation),
        id<core::StepId>(step),
        id<core::CommandId>(command),
        id<core::StepId>(restoredStep),
        id<core::CommandId>(restoredCommand)};
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> actionCompleted(
    const core::RuntimeDeviceCorrelation& device,
    const core::OperationId& operation, const core::StepId& step,
    const core::CommandId& command) {
    auto axes = take(core::StageAxisSet::create({
        id<core::StageAxisId>("x"), id<core::StageAxisId>("y")}));
    auto payload = take(core::StageMotionActionCompletion::create(
        core::StageMotionAction::MoveAxesTo, axes));
    auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{device, core::OperationCorrelation{operation, step, command}},
        core::FeedbackKind::DeviceActionCompleted,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{std::move(payload)});
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> positionFeedback(
    const core::RuntimeDeviceCorrelation& device,
    const core::OperationId& operation, const core::StepId& step,
    const core::CommandId& command, const char* axisId, double value) {
    auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{device, core::OperationCorrelation{operation, step, command}},
        core::FeedbackKind::DeviceStateObserved,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{observation(axisId, value, 0.2)});
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> homeReference(
    const core::RuntimeDeviceCorrelation& device,
    const core::OperationId& operation, const core::StepId& step,
    const core::CommandId& command) {
    auto payload = take(core::StageHomeReferenceObservation::create({
        observation("x", 0.0, 0.2), observation("y", 0.0, 0.2)}));
    auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{device, core::OperationCorrelation{operation, step, command}},
        core::FeedbackKind::HomeReferenceEstablished,
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
        const core::StageMotionCommandContext&, const core::MoveAxesTo& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++moveToCalls;
        lastMove = command;
        return core::StageMotionSubmissionResult::accepted();
    }
    core::StageMotionSubmissionResult moveAxesBy(
        const core::StageMotionCommandContext&, const core::MoveAxesBy&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        return core::StageMotionSubmissionResult::rejected(
            core::StageMotionSubmissionRejection::UnsupportedOperation);
    }
    core::StageMotionSubmissionResult homeAxes(
        const core::StageMotionCommandContext&, const core::HomeAxes& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++homeCalls;
        lastHome = command;
        return core::StageMotionSubmissionResult::accepted();
    }
    core::StageMotionSubmissionResult stopAxes(
        const core::StageMotionCommandContext&, const core::StopAxes&,
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

    core::FeedbackProcessingResult<core::StageMotionFeedbackPayload> process(
        core::FeedbackEnvelope<core::StageMotionFeedbackPayload> feedback) {
        if (runtime->feedbackIngress().submit(std::move(feedback)) !=
            core::FeedbackSubmitStatus::Enqueued)
            throw std::runtime_error("stage feedback was not enqueued");
        auto processed = take(runtime->processNextFeedback());
        if (!processed) throw std::runtime_error("feedback queue unexpectedly empty");
        return std::move(*processed);
    }
};

void testDirectNavigationSupportsAnArbitrarySlideIdentity() {
    RuntimeFixture fixture;
    const auto stage = stageSpecification();
    const auto surface = workingSurface(stage);
    RecordingStagePort port;
    auto navigation = take(application::motion::StageWorkingSurfaceNavigationRuntime::create(
        surface, referencedPosition(surface, stage), homingPolicy(), errorBudget(),
        verificationPolicy(application::motion::StageWorkingSurfaceNavigationPositionVerificationMode::BoundedCommandEstimate),
        fixture.device, port, *fixture.runtime));
    take(navigation.bindOwnerThread());

    auto started = take(navigation.beginRequest(
        id<core::SlideId>("slide-c"), {10000.0, 5000.0},
        navigationIds("direct-op", "direct-step", "direct-cmd", "direct-home-step", "direct-home-cmd")));
    check(started.disposition ==
        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::MoveDispatched,
        "the third user-drawn slide dispatches directly without a fixed slot");
    check(port.moveToCalls == 1 && port.homeCalls == 0 && port.lastMove &&
        port.lastMove->targets().size() == 2,
        "slide navigation commands exactly the XY axes");

    const auto targetForX = std::find_if(port.lastMove->targets().begin(), port.lastMove->targets().end(),
        [](const auto& p) { return p.axisId() == id<core::StageAxisId>("x"); });
    const auto targetForY = std::find_if(port.lastMove->targets().begin(), port.lastMove->targets().end(),
        [](const auto& p) { return p.axisId() == id<core::StageAxisId>("y"); });
    check(targetForX != port.lastMove->targets().end() && targetForY != port.lastMove->targets().end() &&
        std::abs(targetForX->value() - 110000.0) < 1e-9 &&
        std::abs(targetForY->value() - 5000.0) < 1e-9,
        "the slide-local point is transformed through the selected slide registration");

    auto feedback = fixture.process(actionCompleted(
        fixture.device, id<core::OperationId>("direct-op"),
        id<core::StepId>("direct-step"), id<core::CommandId>("direct-cmd")));
    auto completed = take(navigation.processFeedback(feedback));
    check(completed.disposition ==
        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::NavigationCompleted &&
        completed.operation && completed.operation->status() == core::OperationStatus::Succeeded,
        "a correlated completion verifies and completes the navigation operation");
    check(!navigation.position().z().positionIsUsable(),
        "focus Z remains unknown and unchanged by the XY slide move");
    auto local = take(navigation.currentSlidePosition(id<core::SlideId>("slide-c")));
    check(std::abs(local.position.xMicrometers - 10000.0) < 1e-9 &&
        std::abs(local.position.yMicrometers - 5000.0) < 1e-9,
        "the same authoritative machine aggregate maps back into local slide coordinates");
}

void testHomeReferencesOnlyXYThenRestoresTheSlideTarget() {
    RuntimeFixture fixture;
    const auto stage = stageSpecification();
    const auto surface = workingSurface(stage);
    auto unknown = take(core::StagePosition3D::unknown(
        surface, stage, id<core::StageAxisId>("focus-z")));
    RecordingStagePort port;
    auto navigation = take(application::motion::StageWorkingSurfaceNavigationRuntime::create(
        surface, unknown, homingPolicy(), errorBudget(),
        verificationPolicy(application::motion::StageWorkingSurfaceNavigationPositionVerificationMode::BoundedCommandEstimate),
        fixture.device, port, *fixture.runtime));
    take(navigation.bindOwnerThread());

    auto started = take(navigation.beginRequest(
        id<core::SlideId>("slide-a"), {20000.0, 10000.0},
        navigationIds("home-op", "home-step", "home-cmd", "restored-step", "restored-cmd")));
    check(started.disposition ==
        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::HomingDispatched &&
        port.homeCalls == 1 && port.moveToCalls == 0,
        "unknown X/Y references cause homing before slide motion");
    check(port.lastHome && port.lastHome->axes().axes().size() == 2 &&
        port.lastHome->axes().axes()[0] == surface.xAxisId() &&
        port.lastHome->axes().axes()[1] == surface.yAxisId(),
        "slide navigation homes X and Y only");

    auto home = fixture.process(homeReference(
        fixture.device, id<core::OperationId>("home-op"),
        id<core::StepId>("home-step"), id<core::CommandId>("home-cmd")));
    auto restored = take(navigation.processFeedback(home));
    check(restored.disposition ==
        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::RestoredMoveDispatched &&
        port.moveToCalls == 1,
        "the exact correlated X/Y home fact restores the original slide-local target");
    check(port.lastMove && port.lastMove->targets().size() == 2,
        "the restored absolute move still contains only XY");

    auto completion = fixture.process(actionCompleted(
        fixture.device, id<core::OperationId>("home-op"),
        id<core::StepId>("restored-step"), id<core::CommandId>("restored-cmd")));
    auto completed = take(navigation.processFeedback(completion));
    check(completed.disposition ==
        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::NavigationCompleted &&
        completed.operation && completed.operation->status() == core::OperationStatus::Succeeded,
        "home and restored movement complete as one operation");
    check(navigation.position().x().positionIsUsable() &&
        navigation.position().y().positionIsUsable() &&
        !navigation.position().z().positionIsUsable(),
        "successful slide homing references X/Y without changing focus Z");
}

void testDeviceReadbackVerifiesBothAxesAndNotOne() {
    RuntimeFixture fixture;
    const auto stage = stageSpecification();
    const auto surface = workingSurface(stage);
    RecordingStagePort port;
    auto navigation = take(application::motion::StageWorkingSurfaceNavigationRuntime::create(
        surface, referencedPosition(surface, stage), homingPolicy(), errorBudget(),
        verificationPolicy(application::motion::StageWorkingSurfaceNavigationPositionVerificationMode::DevicePositionObservations),
        fixture.device, port, *fixture.runtime));
    take(navigation.bindOwnerThread());

    take(navigation.beginRequest(
        id<core::SlideId>("slide-a"), {20000.0, 10000.0},
        navigationIds("observed-op", "observed-step", "observed-cmd", "observed-home-step", "observed-home-cmd")));
    auto complete = fixture.process(actionCompleted(
        fixture.device, id<core::OperationId>("observed-op"),
        id<core::StepId>("observed-step"), id<core::CommandId>("observed-cmd")));
    auto awaiting = take(navigation.processFeedback(complete));
    check(awaiting.disposition ==
        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::AwaitingPositionFeedback,
        "device-readback policy waits for both fresh correlated axis observations");

    auto x = fixture.process(positionFeedback(
        fixture.device, id<core::OperationId>("observed-op"),
        id<core::StepId>("observed-step"), id<core::CommandId>("observed-cmd"), "x", 30000.2));
    auto waitingForY = take(navigation.processFeedback(x));
    check(waitingForY.disposition ==
        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::AwaitingPositionFeedback,
        "one axis observation cannot verify a coordinated XY move");

    auto y = fixture.process(positionFeedback(
        fixture.device, id<core::OperationId>("observed-op"),
        id<core::StepId>("observed-step"), id<core::CommandId>("observed-cmd"), "y", 30000.2));
    auto completed = take(navigation.processFeedback(y));
    check(completed.disposition ==
        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::NavigationCompleted,
        "both in-tolerance axis readbacks complete the operation");
    check(navigation.position().x().evidence() == core::StagePositionEvidence::EncoderMeasured &&
        navigation.position().y().evidence() == core::StagePositionEvidence::EncoderMeasured,
        "the runtime retains measured X/Y feedback rather than relabeling it as command-derived");
}

void testBoundedControlQueueDispatchesOnTheOwnerThread() {
    RuntimeFixture fixture;
    const auto stage = stageSpecification();
    const auto surface = workingSurface(stage);
    RecordingStagePort port;
    auto navigation = take(application::motion::StageWorkingSurfaceNavigationRuntime::create(
        surface, referencedPosition(surface, stage), homingPolicy(), errorBudget(),
        verificationPolicy(application::motion::StageWorkingSurfaceNavigationPositionVerificationMode::BoundedCommandEstimate),
        fixture.device, port, *fixture.runtime));

    auto queue = take(application::motion::StageWorkingSurfaceNavigationControlQueue::create(
        1, fixture.wake));
    const application::motion::StageWorkingSurfaceNavigationControlRequest request{
        id<core::SlideId>("slide-b"), {10000.0, 10000.0},
        navigationIds("queue-op", "queue-step", "queue-cmd", "queue-home-step", "queue-home-cmd")};
    check(queue->submit(request) ==
        application::motion::StageWorkingSurfaceNavigationControlSubmitStatus::Enqueued,
        "the generic bounded queue accepts a variable-slide request");

    auto overflow = request;
    overflow.identities.operationId = id<core::OperationId>("overflow-op");
    check(queue->submit(overflow) ==
        application::motion::StageWorkingSurfaceNavigationControlSubmitStatus::QueueFull,
        "the queue rejects overflow without replacing an accepted request");

    auto drained = take(queue->drain(navigation));
    check(drained.outcomes.size() == 1 && port.moveToCalls == 1 &&
        drained.outcomes.front().update &&
        drained.outcomes.front().update->disposition ==
            application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::MoveDispatched,
        "the serialized owner drain dispatches one accepted generic navigation request");
}

} // namespace

int main() {
    testDirectNavigationSupportsAnArbitrarySlideIdentity();
    testHomeReferencesOnlyXYThenRestoresTheSlideTarget();
    testDeviceReadbackVerifiesBothAxesAndNotOne();
    testBoundedControlQueueDispatchesOnTheOwnerThread();
    if (failures != 0) {
        std::cerr << failures << " working-surface navigation runtime test(s) failed\n";
        return 1;
    }
    std::cout << "All working-surface navigation runtime tests passed\n";
    return 0;
}
