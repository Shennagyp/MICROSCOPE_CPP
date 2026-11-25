#include "microscope/application/motion/StageNavigationPlanDispatcher.h"
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

template <typename T, typename E>
T take(core::Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("stage navigation fixture rejected");
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
            id<core::EquipmentComponentId>("stage"),
            {axis("x"), axis("y")})),
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
    const auto topLeft = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::TopLeft, {originX, 20000}, source, 0.25));
    const auto topRight = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::TopRight, {originX + 75000, 20000}, source, 0.25));
    const auto bottomRight = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::BottomRight, {originX + 75000, 45000}, source, 0.25));
    const auto bottomLeft = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::BottomLeft, {originX, 45000}, source, 0.25));
    return take(core::SlideRegistration::create(
        id<core::SlideId>(slideId),
        id<core::CalibrationRevisionId>(revision),
        slot,
        geometry,
        {topLeft, topRight, bottomRight, bottomLeft},
        0.5));
}

struct CarrierFixture final {
    core::DualSlideCarrierSpecification specification;
    core::DualSlideCarrierState carrier;

    CarrierFixture()
        : specification(carrierSpecification()),
          carrier(take(core::DualSlideCarrierState::create(
              specification,
              take(core::SlideSlotState::create(
                  core::DualSlideSlot::Left,
                  id<core::SlideId>("left-slide"),
                  registration("left-slide", core::DualSlideSlot::Left,
                               5000, "left-registration-v1"))),
              take(core::SlideSlotState::create(
                  core::DualSlideSlot::Right,
                  id<core::SlideId>("right-slide"),
                  registration("right-slide", core::DualSlideSlot::Right,
                               95000, "right-registration-v1")))))) {}
};

core::StagePositionState referencedPosition(
    const core::DualSlideCarrierSpecification& specification) {
    auto state = take(core::StagePositionState::create(specification));
    const auto x = take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>("x"), 25000,
            core::AxisCoordinateUnit::Micrometers)),
        0.5,
        core::StagePositionEvidence::EncoderMeasured));
    const auto y = take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>("y"), 30000,
            core::AxisCoordinateUnit::Micrometers)),
        0.5,
        core::StagePositionEvidence::EncoderMeasured));
    state = take(state.afterVerifiedHomingObservation(
        x, core::ObservationSource::HardwareReadback));
    state = take(state.afterVerifiedHomingObservation(
        y, core::ObservationSource::HardwareReadback));
    return state;
}

core::RuntimeDeviceCorrelation makeDevice() {
    return core::RuntimeDeviceCorrelation{
        id<core::MicroscopeId>("scope"),
        id<core::RuntimeId>("runtime"),
        take(core::RuntimeGeneration::create(1)),
        take(core::DeviceSessionGeneration::create(1)),
        id<core::DeviceId>("stage")};
}

struct RuntimeFixture final {
    application::runtime::RuntimeWakeSignal wake;
    core::RuntimeDeviceCorrelation device;
    std::unique_ptr<core::SerializedOperationRuntime<
        core::StageMotionFeedbackPayload>> runtime;

    RuntimeFixture() : device(makeDevice()) {
        runtime = take(core::SerializedOperationRuntime<
            core::StageMotionFeedbackPayload>::create(
                device.microscopeId,
                device.runtimeId,
                device.runtimeGeneration,
                8,
                wake));
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

class RecordingStagePort final : public core::IStageMotionPort {
public:
    core::StageMotionSubmissionResult submission =
        core::StageMotionSubmissionResult::accepted();
    unsigned moveToCalls{0};
    unsigned moveByCalls{0};
    unsigned homeCalls{0};
    unsigned stopCalls{0};
    std::optional<core::MoveAxesTo> moveTo;
    std::optional<core::HomeAxes> home;

    core::StageMotionSubmissionResult moveAxesTo(
        const core::StageMotionCommandContext&,
        const core::MoveAxesTo& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++moveToCalls;
        moveTo = command;
        return submission;
    }

    core::StageMotionSubmissionResult moveAxesBy(
        const core::StageMotionCommandContext&,
        const core::MoveAxesBy&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++moveByCalls;
        return submission;
    }

    core::StageMotionSubmissionResult homeAxes(
        const core::StageMotionCommandContext&,
        const core::HomeAxes& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++homeCalls;
        home = command;
        return submission;
    }

    core::StageMotionSubmissionResult stopAxes(
        const core::StageMotionCommandContext&,
        const core::StopAxes&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++stopCalls;
        return submission;
    }
};
} // namespace

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\\n';
        }
    };

    CarrierFixture carrier;
    const auto policy = take(core::StageHomingPolicy::create(5.0, 5.0));
    const auto slideId = id<core::SlideId>("left-slide");
    const core::SlideLocalPoint destination{20000, 10000};

    const auto directPlan = take(core::StageNavigationPlanner::planToSlidePoint(
        carrier.carrier,
        slideId,
        destination,
        referencedPosition(carrier.specification),
        policy));
    check(std::holds_alternative<core::StageDirectMovePlan>(directPlan),
          "referenced stage with bounded error produces a direct move plan");

    RuntimeFixture directRuntime;
    auto directContext = directRuntime.prepare(
        "direct-operation", "direct-step", "direct-command");
    RecordingStagePort directPort;
    auto directReceipt = take(application::motion::StageNavigationPlanDispatcher::dispatch(
        directPlan, directContext, directPort, *directRuntime.runtime));
    check(directPort.moveToCalls == 1 && directPort.homeCalls == 0 &&
          directPort.moveByCalls == 0 && directPort.stopCalls == 0,
          "direct navigation plan invokes only the coordinated MoveAxesTo port operation");
    check(std::holds_alternative<core::MoveAxesTo>(directReceipt.command) &&
          directReceipt.dispatch.action == core::StageMotionAction::MoveAxesTo &&
          directReceipt.dispatch.stepStatus == core::StepStatus::AdapterAccepted,
          "direct plan receipt preserves the command type and adapter-acceptance state");
    check(directReceipt.target.slideId() == slideId &&
          directReceipt.target.point().xMicrometers == destination.xMicrometers &&
          directReceipt.target.point().yMicrometers == destination.yMicrometers &&
          directReceipt.target.registrationRevision() ==
              carrier.carrier.registrationFor(slideId)->revision(),
          "direct plan receipt preserves physical SlideId, local destination and registration revision");
    check(directReceipt.context.operation().commandId ==
              directContext.operation().commandId &&
          directReceipt.context.runtimeDevice().deviceSessionGeneration ==
              directRuntime.device.deviceSessionGeneration,
          "dispatch receipt retains the exact command and device-session context");
    check(directPort.moveTo &&
          directPort.moveTo->targets().size() == 2 &&
          std::abs(directPort.moveTo->targets()[0].value() - 25000.0) < 1e-9 &&
          std::abs(directPort.moveTo->targets()[1].value() - 30000.0) < 1e-9,
          "direct plan sends the calibrated common-plane X/Y coordinates");

    const auto unknownPosition = take(core::StagePositionState::create(
        carrier.specification));
    const auto homePlan = take(core::StageNavigationPlanner::planToSlidePoint(
        carrier.carrier, slideId, destination, unknownPosition, policy));
    check(std::holds_alternative<core::StageHomeThenMovePlan>(homePlan),
          "unreferenced stage produces a home-then-move plan");

    RuntimeFixture homeRuntime;
    auto homeContext = homeRuntime.prepare(
        "home-operation", "home-step", "home-command");
    RecordingStagePort homePort;
    auto homeReceipt = take(application::motion::StageNavigationPlanDispatcher::dispatch(
        homePlan, homeContext, homePort, *homeRuntime.runtime));
    check(homePort.homeCalls == 1 && homePort.moveToCalls == 0 &&
          homePort.moveByCalls == 0 && homePort.stopCalls == 0,
          "home-first plan invokes only HomeAxes and does not dispatch the destination prematurely");
    check(std::holds_alternative<core::HomeAxes>(homeReceipt.command) &&
          homeReceipt.dispatch.action == core::StageMotionAction::HomeAxes &&
          homeReceipt.dispatch.stepStatus == core::StepStatus::AdapterAccepted,
          "home-first receipt records the exact HomeAxes command and adapter status");
    check(homeReceipt.target.slideId() == slideId &&
          homeReceipt.target.point().xMicrometers == destination.xMicrometers &&
          homeReceipt.target.point().yMicrometers == destination.yMicrometers &&
          homeReceipt.target.registrationRevision() ==
              carrier.carrier.registrationFor(slideId)->revision(),
          "home receipt preserves the original slide-local target and calibration revision");
    check(homePort.home &&
          homePort.home->axes().axes().size() == 2 &&
          homePort.home->axes().axes()[0] == id<core::StageAxisId>("x") &&
          homePort.home->axes().axes()[1] == id<core::StageAxisId>("y"),
          "home-first plan homes both physical stage-plane axes");

    if (failures != 0) {
        std::cerr << failures << " navigation plan dispatcher test(s) failed\\n";
        return 1;
    }
    std::cout << "All navigation plan dispatcher tests passed\\n";
    return 0;
}
