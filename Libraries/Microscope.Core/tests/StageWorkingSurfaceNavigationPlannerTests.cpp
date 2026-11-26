#include "microscope/core/navigation/StageWorkingSurfaceNavigationPlanner.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace microscope::core;

template<class T, class E>
T take(Result<T, E> result) {
    if (!result) throw std::runtime_error("valid test fixture was rejected");
    return std::move(result).value();
}
template<class T>
T id(const char* value) { return take(T::create(value)); }

AxisSpecification axis(const char* name, AxisCoordinateUnit unit,
    double minimum, double maximum) {
    return take(AxisSpecification::create(
        id<StageAxisId>(name), AxisMotionType::Linear, unit,
        take(AxisOperatingRange::create(minimum, maximum, unit)),
        take(PositiveAxisResolution::create(
            unit == AxisCoordinateUnit::Millimeters ? 0.001 : 0.1)),
        std::nullopt, std::nullopt, std::nullopt,
        AxisPositionFeedbackCapability::DeviceReported));
}

struct Fixture final {
    StageSpecification stage;
    StageWorkingSurface2D surface;
};

Fixture fixture(bool millimeters = false) {
    const auto unit = millimeters ? AxisCoordinateUnit::Millimeters : AxisCoordinateUnit::Micrometers;
    const auto minimum = millimeters ? -100.0 : -100'000.0;
    const auto maximum = millimeters ? 150.0 : 150'000.0;
    const auto stage = take(StageSpecification::create(id<EquipmentComponentId>("stage"), {
        axis("x", unit, minimum, maximum),
        axis("y", unit, minimum, maximum),
        axis("focus-z", unit, minimum, maximum)}));
    auto surface = take(StageWorkingSurface2D::createEmpty(
        stage, id<StageAxisId>("x"), id<StageAxisId>("y"), {0.0, 0.0},
        take(StageTravelRange2D::create(120'000.0, 60'000.0))));
    surface = take(surface.withSlideRectangle(take(SlideRectangleDefinition2D::create(
        id<SlideId>("slide-a"), id<CalibrationRevisionId>("slide-a-v1"),
        {10'000.0, 20'000.0}, {10'001.0, 20'001.0},
        SlideRectangleLengthAxis::StageX, 75'000.0, 25'000.0))));
    surface = take(surface.withSlideRectangle(take(SlideRectangleDefinition2D::create(
        id<SlideId>("slide-b"), id<CalibrationRevisionId>("slide-b-v1"),
        {0.0, 0.0}, {1.0, 1.0},
        SlideRectangleLengthAxis::StageY, 20'000.0, 15'000.0))));
    surface = take(surface.withSlideRectangle(take(SlideRectangleDefinition2D::create(
        id<SlideId>("slide-c"), id<CalibrationRevisionId>("slide-c-v1"),
        {90'000.0, 0.0}, {90'001.0, 1.0},
        SlideRectangleLengthAxis::StageX, 20'000.0, 15'000.0))));
    return {stage, std::move(surface)};
}

StagePosition3D positionWithXYReference(
    const StageWorkingSurface2D& surface, const StageSpecification& stage,
    double xMicrometers, double yMicrometers, double errorMicrometers = 0.5) {
    auto position = take(StagePosition3D::unknown(
        surface, stage, id<StageAxisId>("focus-z")));
    const auto makeObservation = [](const StageAxisId& axisId, AxisCoordinateUnit unit,
        double valueMicrometers, double errorMicrometers) {
        const double value = unit == AxisCoordinateUnit::Millimeters
            ? valueMicrometers / 1000.0 : valueMicrometers;
        const double error = unit == AxisCoordinateUnit::Millimeters
            ? errorMicrometers / 1000.0 : errorMicrometers;
        return take(AxisPositionObservation::create(
            take(AxisPosition::create(axisId, value, unit)),
            error, StagePositionEvidence::EncoderMeasured));
    };
    position = take(position.afterVerifiedHomingObservation(
        makeObservation(surface.xAxisId(), surface.xAxisUnit(), xMicrometers, errorMicrometers),
        ObservationSource::HardwareReadback));
    position = take(position.afterVerifiedHomingObservation(
        makeObservation(surface.yAxisId(), surface.yAxisUnit(), yMicrometers, errorMicrometers),
        ObservationSource::HardwareReadback));
    return position;
}
}

int main() {
    using namespace microscope::core;
    int failures = 0;
    const auto check = [&](bool value, const char* message) {
        if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };

    const auto f = fixture();
    const auto policy = take(StageXYHomingPolicy::create(5.0, 5.0));
    const auto position = positionWithXYReference(f.surface, f.stage, 25'000.0, 30'000.0);

    const auto direct = StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
        f.surface, id<SlideId>("slide-a"), {20'000.0, 10'000.0}, position, policy);
    check(direct && std::holds_alternative<StageWorkingSurfaceDirectMovePlan>(direct.value()),
        "a referenced XY position plans direct movement through the selected slide rectangle");
    if (direct) {
        const auto& plan = std::get<StageWorkingSurfaceDirectMovePlan>(direct.value());
        check(plan.command.targets().size() == 2 &&
            plan.target.registrationRevision() == id<CalibrationRevisionId>("slide-a-v1"),
            "plan preserves the slide registration revision and commands exactly X/Y");
        check(std::abs(plan.command.targets()[0].value() - 30'000.0) < 1e-9 &&
            std::abs(plan.command.targets()[1].value() - 30'000.0) < 1e-9,
            "slide-local coordinate resolves into stage coordinates");
    }

    const auto thirdSlide = StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
        f.surface, id<SlideId>("slide-c"), {5'000.0, 5'000.0}, position, policy);
    check(thirdSlide && std::holds_alternative<StageWorkingSurfaceDirectMovePlan>(thirdSlide.value()),
        "navigation supports a third drawn slide without a fixed carrier slot");

    const auto outside = StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
        f.surface, id<SlideId>("slide-a"), {75'001.0, 1'000.0}, position, policy);
    check(!outside && outside.error() ==
        StageWorkingSurfaceNavigationPlanningError::SlidePointOutsideGeometry,
        "rejects a local point outside the constrained slide dimensions");

    const auto missing = StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
        f.surface, id<SlideId>("absent"), {1.0, 1.0}, position, policy);
    check(!missing && missing.error() ==
        StageWorkingSurfaceNavigationPlanningError::SlideNotFound,
        "rejects a slide identity that does not exist in the canvas");

    const auto unknown = take(StagePosition3D::unknown(
        f.surface, f.stage, id<StageAxisId>("focus-z")));
    const auto homePlan = StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
        f.surface, id<SlideId>("slide-a"), {20'000.0, 10'000.0}, unknown, policy);
    check(homePlan && std::holds_alternative<StageWorkingSurfaceHomeThenMovePlan>(homePlan.value()),
        "unknown XY reference creates a home-then-move plan");
    if (homePlan) {
        const auto& home = std::get<StageWorkingSurfaceHomeThenMovePlan>(homePlan.value()).home;
        check(home.axes().axes().size() == 2 &&
            home.axes().axes()[0] == f.surface.xAxisId() &&
            home.axes().axes()[1] == f.surface.yAxisId(),
            "navigation homes exactly X and Y; it does not home focus Z");
    }

    const auto independentZ = StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
        f.surface, id<SlideId>("slide-a"), {20'000.0, 10'000.0}, position, policy);
    check(independentZ &&
        std::holds_alternative<StageWorkingSurfaceDirectMovePlan>(independentZ.value()),
        "unknown focus Z does not invalidate a fully referenced XY move");

    const auto target = take(f.surface.createNavigationTarget(
        id<SlideId>("slide-a"), {20'000.0, 10'000.0}));
    const auto restored = StageWorkingSurfaceNavigationPlanner::resolveAfterHoming(
        f.surface, target,
        positionWithXYReference(f.surface, f.stage, 0.0, 0.0), policy);
    check(restored && restored.value().targets().size() == 2 &&
        std::abs(restored.value().targets()[0].value() - 30'000.0) < 1e-9 &&
        std::abs(restored.value().targets()[1].value() - 30'000.0) < 1e-9,
        "verified XY homing restores the same local target against the current registration");

    const auto unreferencedRestore = StageWorkingSurfaceNavigationPlanner::resolveAfterHoming(
        f.surface, target, unknown, policy);
    check(!unreferencedRestore && unreferencedRestore.error() ==
        StageWorkingSurfaceNavigationPlanningError::HomingNotVerified,
        "restore is blocked until both X and Y reference states are usable");

    const auto staleTarget = target;
    const auto revised = take(f.surface.replacingSlideRectangle(take(
        SlideRectangleDefinition2D::create(
            id<SlideId>("slide-a"), id<CalibrationRevisionId>("slide-a-v2"),
            {11'000.0, 20'000.0}, {11'001.0, 20'001.0},
            SlideRectangleLengthAxis::StageX, 75'000.0, 25'000.0))));
    const auto stale = StageWorkingSurfaceNavigationPlanner::resolveAfterHoming(
        revised, staleTarget,
        positionWithXYReference(revised, f.stage, 0.0, 0.0), policy);
    check(!stale && stale.error() ==
        StageWorkingSurfaceNavigationPlanningError::RegistrationChanged,
        "a redraw invalidates an already captured target from the prior layout revision");

    const auto otherStage = take(StageSpecification::create(
        id<EquipmentComponentId>("other-stage"), {
            axis("x", AxisCoordinateUnit::Micrometers, -100'000.0, 150'000.0),
            axis("y", AxisCoordinateUnit::Micrometers, -100'000.0, 150'000.0),
            axis("focus-z", AxisCoordinateUnit::Micrometers, -100'000.0, 150'000.0)}));
    const auto otherSurface = take(StageWorkingSurface2D::createEmpty(
        otherStage, id<StageAxisId>("x"), id<StageAxisId>("y"), {0,0},
        take(StageTravelRange2D::create(120'000, 60'000))));
    const auto foreignPosition = take(StagePosition3D::unknown(
        otherSurface, otherStage, id<StageAxisId>("focus-z")));
    const auto crossStage = StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
        f.surface, id<SlideId>("slide-a"), {20'000,10'000}, foreignPosition, policy);
    check(!crossStage && crossStage.error() ==
        StageWorkingSurfaceNavigationPlanningError::StageComponentMismatch,
        "position from another installed stage cannot be used with this canvas");

    const auto mm = fixture(true);
    const auto mmPosition = positionWithXYReference(mm.surface, mm.stage, 25'000, 30'000, 1.0);
    const auto mmDirect = StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
        mm.surface, id<SlideId>("slide-a"), {20'000,10'000}, mmPosition, policy);
    check(mmDirect && std::holds_alternative<StageWorkingSurfaceDirectMovePlan>(mmDirect.value()),
        "millimeter axes accept micrometer-defined canvas coordinates");
    if (mmDirect) {
        const auto& command = std::get<StageWorkingSurfaceDirectMovePlan>(mmDirect.value()).command;
        check(command.targets()[0].unit() == AxisCoordinateUnit::Millimeters &&
            std::abs(command.targets()[0].value() - 30.0) < 1e-9 &&
            std::abs(command.targets()[1].value() - 30.0) < 1e-9,
            "physical coordinate units are converted only at the motion boundary");
    }

    if (failures != 0) {
        std::cerr << failures << " working-surface navigation planner test(s) failed\n";
        return 1;
    }
    std::cout << "All working-surface navigation planner tests passed\n";
    return 0;
}
