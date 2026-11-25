#pragma once
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/runtime/Feedback.h"
#include <array>
#include <optional>

namespace microscope::core {

// OPTIKA dual-slide carrier: two side-by-side slides on one horizontal X/Y
// stage plane. The transverse in-plane axis is not a vertical slide-stack axis.
enum class DualSlideSlot { Left, Right };
enum class CarrierHorizontalDirection {
    PositiveStageXFromLeftToRight,
    NegativeStageXFromLeftToRight
};

enum class DualSlideCarrierSpecificationError {
    SamePlaneAxis, MissingHorizontalAxis, MissingTransverseAxis,
    NonLinearPlaneAxis, NonPhysicalPlaneAxisUnit, InvalidHorizontalDirection
};
class DualSlideCarrierSpecification final {
public:
    static Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError> create(
        const StageSpecification&, StageAxisId horizontalAxisId,
        StageAxisId transverseAxisId, CarrierHorizontalDirection);
    const EquipmentComponentId& stageComponentId() const noexcept;
    const StageAxisId& horizontalAxisId() const noexcept;
    const StageAxisId& transverseAxisId() const noexcept;
    CarrierHorizontalDirection horizontalDirection() const noexcept;
private:
    DualSlideCarrierSpecification(EquipmentComponentId,StageAxisId,StageAxisId,CarrierHorizontalDirection);
    EquipmentComponentId stageComponentId_;
    StageAxisId horizontalAxisId_;
    StageAxisId transverseAxisId_;
    CarrierHorizontalDirection direction_;
};

enum class SlideGeometryError { NonFiniteDimension, NonPositiveDimension };
class SlideGeometry final {
public:
    static Result<SlideGeometry,SlideGeometryError> create(double widthMicrometers,double heightMicrometers);
    double widthMicrometers() const noexcept;
    double heightMicrometers() const noexcept;
    bool contains(double xMicrometers,double yMicrometers) const noexcept;
private:
    SlideGeometry(double,double);
    double width_;
    double height_;
};

struct SlideLocalPoint final { double xMicrometers; double yMicrometers; };
struct StagePlanePoint final { double horizontalMicrometers; double transverseMicrometers; };
enum class SlideCorner { TopLeft, TopRight, BottomRight, BottomLeft };

enum class SlideCornerMeasurementError {
    InvalidCorner, NonFiniteCoordinate, InvalidUncertainty,
    UnknownProvenance, InvalidProvenance
};
class SlideCornerMeasurement final {
public:
    static Result<SlideCornerMeasurement,SlideCornerMeasurementError> create(
        SlideCorner,StagePlanePoint,ObservationSource,double uncertaintyMicrometers);
    SlideCorner corner() const noexcept;
    const StagePlanePoint& stagePosition() const noexcept;
    ObservationSource source() const noexcept;
    double uncertaintyMicrometers() const noexcept;
private:
    SlideCornerMeasurement(SlideCorner,StagePlanePoint,ObservationSource,double);
    SlideCorner corner_;
    StagePlanePoint stage_;
    ObservationSource source_;
    double uncertainty_;
};

enum class SlideRegistrationError {
    InvalidResidualLimit,DuplicateCorner,MissingCorner,DegenerateTransform,
    FitResidualExceeded,NonFiniteLocalPoint,LocalPointOutsideSlide,NonFiniteStagePoint
};
class SlideRegistration final {
public:
    static Result<SlideRegistration,SlideRegistrationError> create(
        SlideId,DualSlideSlot,SlideGeometry,std::array<SlideCornerMeasurement,4>,
        double maximumFitResidualMicrometers);
    const SlideId& slideId() const noexcept;
    DualSlideSlot slot() const noexcept;
    const SlideGeometry& geometry() const noexcept;
    const std::array<SlideCornerMeasurement,4>& cornerMeasurements() const noexcept;
    double maximumFitResidualMicrometers() const noexcept;
    double rmsFitResidualMicrometers() const noexcept;
    Result<StagePlanePoint,SlideRegistrationError> toStage(SlideLocalPoint) const;
    Result<SlideLocalPoint,SlideRegistrationError> toSlide(StagePlanePoint) const;
private:
    SlideRegistration(SlideId,DualSlideSlot,SlideGeometry,
        std::array<SlideCornerMeasurement,4>,std::array<double,6>,double,double);
    SlideId slideId_;
    DualSlideSlot slot_;
    SlideGeometry geometry_;
    std::array<SlideCornerMeasurement,4> corners_;
    // X=a0+a1*x+a2*y; Y=b0+b1*x+b2*y.
    std::array<double,6> transform_;
    double maxResidual_;
    double rmsResidual_;
};

enum class SlideSlotStateError {
    InvalidSlot,RegistrationWithoutSlide,RegistrationSlideMismatch,RegistrationSlotMismatch
};
class SlideSlotState final {
public:
    static Result<SlideSlotState,SlideSlotStateError> create(
        DualSlideSlot,std::optional<SlideId>,std::optional<SlideRegistration> = std::nullopt);
    DualSlideSlot slot() const noexcept;
    const std::optional<SlideId>& slideId() const noexcept;
    const std::optional<SlideRegistration>& registration() const noexcept;
private:
    SlideSlotState(DualSlideSlot,std::optional<SlideId>,std::optional<SlideRegistration>);
    DualSlideSlot slot_;
    std::optional<SlideId> slideId_;
    std::optional<SlideRegistration> registration_;
};
enum class DualSlideCarrierStateError { WrongLeftSlot,WrongRightSlot,DuplicateSlideId };
class DualSlideCarrierState final {
public:
    static Result<DualSlideCarrierState,DualSlideCarrierStateError> create(
        DualSlideCarrierSpecification,SlideSlotState left,SlideSlotState right);
    const DualSlideCarrierSpecification& specification() const noexcept;
    const SlideSlotState& left() const noexcept;
    const SlideSlotState& right() const noexcept;
    const SlideRegistration* registrationFor(const SlideId&) const noexcept;
private:
    DualSlideCarrierState(DualSlideCarrierSpecification,SlideSlotState,SlideSlotState);
    DualSlideCarrierSpecification specification_;
    SlideSlotState left_;
    SlideSlotState right_;
};
} // namespace microscope::core
