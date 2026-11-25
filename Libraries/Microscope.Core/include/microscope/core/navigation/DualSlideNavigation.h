#pragma once
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/runtime/Feedback.h"
#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/motion/StageAxisPositionState.h"
#include <array>
#include <optional>

namespace microscope::core {

// OPTIKA dual-slide carrier: two side-by-side slides on one horizontal X/Y
// stage plane. The transverse in-plane axis is not a vertical slide-stack axis.
enum class DualSlideSlot { Left, Right };
enum class SlideNavigationCommandError { InvalidStageCoordinate, OutsideStageTravel, SlideNotInCarrier, SlideNotRegistered, RegistrationChanged };
struct StagePlanePoint;
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
    AxisCoordinateUnit horizontalUnit() const noexcept;
    AxisCoordinateUnit transverseUnit() const noexcept;
    CarrierHorizontalDirection horizontalDirection() const noexcept;
    Result<MoveAxesTo, SlideNavigationCommandError> moveTo(const StagePlanePoint&) const;
private:
    DualSlideCarrierSpecification(EquipmentComponentId,StageAxisId,StageAxisId,CarrierHorizontalDirection,
        AxisCoordinateUnit,AxisCoordinateUnit,AxisOperatingRange,AxisOperatingRange);
    EquipmentComponentId stageComponentId_;
    StageAxisId horizontalAxisId_;
    StageAxisId transverseAxisId_;
    CarrierHorizontalDirection direction_;
    AxisCoordinateUnit horizontalUnit_;
    AxisCoordinateUnit transverseUnit_;
    AxisOperatingRange horizontalRange_;
    AxisOperatingRange transverseRange_;
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
class DualSlideCarrierState;
class SlideNavigationTarget final {
public:
    const SlideId& slideId() const noexcept;
    const SlideLocalPoint& point() const noexcept;
    const CalibrationRevisionId& registrationRevision() const noexcept;
private:
    SlideNavigationTarget(SlideId,SlideLocalPoint,CalibrationRevisionId);
    SlideId slideId_;
    SlideLocalPoint point_;
    CalibrationRevisionId registrationRevision_;
    friend class DualSlideCarrierState;
};
enum class SlideNavigationTargetError { SlideNotInCarrier,SlideNotRegistered,PointOutsideSlide };
struct StagePlanePoint final { double horizontalMicrometers; double transverseMicrometers; };
struct SlidePositionEstimate final {
    SlideLocalPoint position;
    double xErrorBoundMicrometers;
    double yErrorBoundMicrometers;
    StagePositionEvidence horizontalEvidence;
    StagePositionEvidence transverseEvidence;
    ObservationSource horizontalSource;
    ObservationSource transverseSource;
};
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
    InvalidResidualLimit,InvalidSlot,DuplicateCorner,MissingCorner,DegenerateTransform,
    FitResidualExceeded,NonFiniteLocalPoint,LocalPointOutsideSlide,NonFiniteStagePoint
};
class SlideRegistration final {
public:
    static Result<SlideRegistration,SlideRegistrationError> create(
        SlideId,CalibrationRevisionId,DualSlideSlot,SlideGeometry,std::array<SlideCornerMeasurement,4>,
        double maximumFitResidualMicrometers);
    const SlideId& slideId() const noexcept;
    const CalibrationRevisionId& revision() const noexcept;
    DualSlideSlot slot() const noexcept;
    const SlideGeometry& geometry() const noexcept;
    const std::array<SlideCornerMeasurement,4>& cornerMeasurements() const noexcept;
    double maximumFitResidualMicrometers() const noexcept;
    double acceptedMaximumFitResidualMicrometers() const noexcept;
    double rmsFitResidualMicrometers() const noexcept;
    const std::array<double,4>& cornerFitResidualsMicrometers() const noexcept;
    Result<StagePlanePoint,SlideRegistrationError> toStage(SlideLocalPoint) const;
    Result<SlideLocalPoint,SlideRegistrationError> toSlide(StagePlanePoint) const;
    Result<SlidePositionEstimate,SlideRegistrationError> estimateSlidePosition(
        StagePlanePoint,double horizontalErrorBoundMicrometers,
        double transverseErrorBoundMicrometers,
        StagePositionEvidence horizontalEvidence,StagePositionEvidence transverseEvidence,
        ObservationSource horizontalSource,ObservationSource transverseSource) const;
private:
    SlideRegistration(SlideId,CalibrationRevisionId,DualSlideSlot,SlideGeometry,
        std::array<SlideCornerMeasurement,4>,std::array<double,6>,std::array<double,4>,double,double,double,double);
    SlideId slideId_;
    CalibrationRevisionId revision_;
    DualSlideSlot slot_;
    SlideGeometry geometry_;
    std::array<SlideCornerMeasurement,4> corners_;
    // X=a0+a1*x+a2*y; Y=b0+b1*x+b2*y.
    std::array<double,6> transform_;
    std::array<double,4> cornerResiduals_;
    double maximumCornerUncertainty_;
    double acceptedMaximumFitResidualMicrometers_;
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
enum class DualSlideCarrierStateError { WrongLeftSlot,WrongRightSlot,DuplicateSlideId,SlideSlotOrderMismatch };
enum class CurrentSlidePositionError {
    SlideNotInCarrier,SlideNotRegistered,StageComponentMismatch,HorizontalAxisMismatch,TransverseAxisMismatch,
    PositionUnknown,PositionUncertaintyUnknown,PositionOutsideSlide,AxisUnitMismatch,InvalidEstimate
};
class DualSlideCarrierState final {
public:
    static Result<DualSlideCarrierState,DualSlideCarrierStateError> create(
        DualSlideCarrierSpecification,SlideSlotState left,SlideSlotState right);
    const DualSlideCarrierSpecification& specification() const noexcept;
    const SlideSlotState& left() const noexcept;
    const SlideSlotState& right() const noexcept;
    const SlideRegistration* registrationFor(const SlideId&) const noexcept;
    Result<SlideNavigationTarget,SlideNavigationTargetError> createTarget(
        const SlideId&,SlideLocalPoint) const;
    Result<MoveAxesTo,SlideNavigationCommandError> commandFor(const SlideNavigationTarget&) const;
    Result<MoveAxesTo, SlideNavigationCommandError> commandFor(const SlideId&, SlideLocalPoint) const;
    Result<SlidePositionEstimate,CurrentSlidePositionError> currentSlidePosition(
        const SlideId&,const StageAxisPositionState& horizontal,
        const StageAxisPositionState& transverse) const;
private:
    DualSlideCarrierState(DualSlideCarrierSpecification,SlideSlotState,SlideSlotState);
    DualSlideCarrierSpecification specification_;
    SlideSlotState left_;
    SlideSlotState right_;
};
} // namespace microscope::core
