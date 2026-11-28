#include "microscope/application/navigation/StageSlotRegistration.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace microscope::core;
using namespace microscope::application::navigation;
int failures=0;
void check(bool value,const char* message) {
    if(!value) { ++failures; std::cerr<<"FAIL: "<<message<<'\n'; }
}
template<class T,class E>
T take(Result<T,E> result) {
    if(!result) throw std::runtime_error("StageSlot registration fixture failed.");
    return std::move(result).value();
}
template<class T>T makeId(const char* value){return take(T::create(value));}

StageSlotCornerMeasurement corner(
    SlideCorner c,double x,double y,double uncertainty=10.0) {
    return take(StageSlotCornerMeasurement::create(c,{x,y},
        ObservationSource::HardwareReadback,uncertainty));
}

void testGenericSlotAffineRegistrationInNanometres() {
    const auto slot=makeId<StageSlotId>("stage-slot-17");
    const auto slide=makeId<SlideId>("slide-17");
    const auto calibration=makeId<CalibrationRevisionId>("calibration-4");
    const auto geometry=take(SlideGeometry::create(75'000.0,25'000.0));
    // 90-degree in-plane rotation plus a fixed global translation:
    // X = 100 mm + local Y; Y = 200 mm + local X.
    const std::array<StageSlotCornerMeasurement,4> corners{
        corner(SlideCorner::TopLeft,100'000'000.0,200'000'000.0),
        corner(SlideCorner::TopRight,100'000'000.0,275'000'000.0),
        corner(SlideCorner::BottomRight,125'000'000.0,275'000'000.0),
        corner(SlideCorner::BottomLeft,125'000'000.0,200'000'000.0)};
    const auto registration=take(StageSlotRegistration::create(
        slot,slide,3,calibration,geometry,corners,1.0));
    check(registration.stageSlotId()==slot&&registration.slideId()==slide&&
          registration.slideRevision()==3&&registration.calibrationRevision()==calibration,
        "registration is keyed by a generic StageSlot and exact Slide/calibration revisions");
    check(registration.maximumFitResidualNanometres()<1e-5&&
          registration.rmsFitResidualNanometres()<1e-5,
        "fits the four measured corners with near-zero residual");
    const auto stage=take(registration.toStage({10'000'000.0,5'000'000.0}));
    check(std::abs(stage.x-105'000'000.0)<1e-4&&
          std::abs(stage.y-210'000'000.0)<1e-4,
        "maps local Slide nanometres into global Stage nanometres");
    const auto local=take(registration.toSlide(stage));
    check(std::abs(local.x-10'000'000.0)<1e-4&&
          std::abs(local.y-5'000'000.0)<1e-4,
        "inverse affine mapping returns the original Slide-local coordinates");
    check(!registration.toStage({75'000'001.0,0.0}),
        "rejects local coordinates outside the physical Slide geometry");

    const auto estimate=take(registration.estimateSlidePosition(
        stage,100.0,200.0,
        StagePositionEvidence::EncoderMeasured,StagePositionEvidence::ExternalMeasured,
        ObservationSource::HardwareReadback,ObservationSource::ExternalSensor));
    check(std::abs(estimate.position.x-10'000'000.0)<1e-4&&
          estimate.xErrorBoundNanometres>0.0&&estimate.yErrorBoundNanometres>0.0&&
          estimate.horizontalEvidence==StagePositionEvidence::EncoderMeasured,
        "propagates Stage position uncertainty and preserves evidence/provenance");
}

void testRegistrationValidation() {
    const auto geometry=take(SlideGeometry::create(75'000.0,25'000.0));
    const auto makeCorners=[] {
        return std::array<StageSlotCornerMeasurement,4>{
            corner(SlideCorner::TopLeft,0,0),
            corner(SlideCorner::TopRight,75'000'000,0),
            corner(SlideCorner::BottomRight,75'000'000,25'000'000),
            corner(SlideCorner::BottomLeft,0,25'000'000)};
    };
    const auto slot=makeId<StageSlotId>("stage-slot-validation");
    const auto slide=makeId<SlideId>("slide-validation");
    const auto calibration=makeId<CalibrationRevisionId>("calibration-validation");

    auto invalidRevision=StageSlotRegistration::create(
        slot,slide,0,calibration,geometry,makeCorners(),1);
    check(!invalidRevision&&invalidRevision.error()==StageSlotRegistrationError::InvalidSlideRevision,
        "requires a positive immutable Slide revision");
    auto invalidResidual=StageSlotRegistration::create(
        slot,slide,1,calibration,geometry,makeCorners(),-1);
    check(!invalidResidual&&invalidResidual.error()==StageSlotRegistrationError::InvalidResidualLimit,
        "rejects a negative accepted fit residual");
    auto duplicate=makeCorners();
    duplicate[3]=corner(SlideCorner::TopLeft,0,25'000'000);
    auto duplicateResult=StageSlotRegistration::create(
        slot,slide,1,calibration,geometry,duplicate,1);
    check(!duplicateResult&&duplicateResult.error()==StageSlotRegistrationError::DuplicateCorner,
        "rejects duplicate corner identity");
    auto degenerate=std::array<StageSlotCornerMeasurement,4>{
        corner(SlideCorner::TopLeft,10,10),
        corner(SlideCorner::TopRight,20,20),
        corner(SlideCorner::BottomRight,30,30),
        corner(SlideCorner::BottomLeft,40,40)};
    auto degenerateResult=StageSlotRegistration::create(
        slot,slide,1,calibration,geometry,degenerate,100);
    check(!degenerateResult&&degenerateResult.error()==StageSlotRegistrationError::DegenerateTransform,
        "rejects a singular four-corner affine transform");
}
}

int main() {
    testGenericSlotAffineRegistrationInNanometres();
    testRegistrationValidation();
    if(failures) {
        std::cerr<<failures<<" StageSlot registration test(s) failed.\n";
        return 1;
    }
    std::cout<<"All generic StageSlot registration tests passed.\n";
    return 0;
}
