#include "microscope/core/equipment/StageSpecifications.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool hasMeaningfulText(const std::string& value) noexcept {
    return value.find('\0') == std::string::npos &&
           value.find_first_not_of(" \t\n\r\f\v") != std::string::npos;
}

[[nodiscard]] bool isKnownAxisMotionType(AxisMotionType value) noexcept {
    switch (value) {
    case AxisMotionType::Linear:
    case AxisMotionType::Rotary:
    case AxisMotionType::Other:
    case AxisMotionType::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownAxisCoordinateUnit(AxisCoordinateUnit value) noexcept {
    switch (value) {
    case AxisCoordinateUnit::Micrometers:
    case AxisCoordinateUnit::Millimeters:
    case AxisCoordinateUnit::Degrees:
    case AxisCoordinateUnit::Radians:
    case AxisCoordinateUnit::DeviceDefined:
    case AxisCoordinateUnit::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isCoordinateUnitCompatible(
    AxisMotionType motion,
    AxisCoordinateUnit unit) noexcept {
    if (unit == AxisCoordinateUnit::DeviceDefined ||
        unit == AxisCoordinateUnit::Unknown ||
        motion == AxisMotionType::Unknown) {
        return true;
    }

    switch (motion) {
    case AxisMotionType::Linear:
        return unit == AxisCoordinateUnit::Micrometers ||
               unit == AxisCoordinateUnit::Millimeters;
    case AxisMotionType::Rotary:
        return unit == AxisCoordinateUnit::Degrees ||
               unit == AxisCoordinateUnit::Radians;
    case AxisMotionType::Other:
        return false;
    case AxisMotionType::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownPositionFeedback(
    AxisPositionFeedbackCapability value) noexcept {
    switch (value) {
    case AxisPositionFeedbackCapability::None:
    case AxisPositionFeedbackCapability::Encoder:
    case AxisPositionFeedbackCapability::LinearScale:
    case AxisPositionFeedbackCapability::DeviceReported:
    case AxisPositionFeedbackCapability::ExternalSensor:
    case AxisPositionFeedbackCapability::Other:
    case AxisPositionFeedbackCapability::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownMeasurementLocation(
    AxisMeasurementLocation value) noexcept {
    switch (value) {
    case AxisMeasurementLocation::Motor:
    case AxisMeasurementLocation::Axis:
    case AxisMeasurementLocation::Specimen:
    case AxisMeasurementLocation::External:
    case AxisMeasurementLocation::Other:
    case AxisMeasurementLocation::Unknown:
        return true;
    }
    return false;
}

} // namespace

AxisOperatingRange::AxisOperatingRange(
    double minimum,
    double maximum,
    AxisCoordinateUnit unit)
    : minimum_(minimum),
      maximum_(maximum),
      unit_(unit) {}

Result<AxisOperatingRange, AxisOperatingRangeError> AxisOperatingRange::create(
    double minimum,
    double maximum,
    AxisCoordinateUnit unit) {
    if (!isKnownAxisCoordinateUnit(unit)) {
        return Result<AxisOperatingRange, AxisOperatingRangeError>::failure(
            AxisOperatingRangeError::InvalidUnit);
    }

    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        return Result<AxisOperatingRange, AxisOperatingRangeError>::failure(
            AxisOperatingRangeError::NonFiniteBound);
    }

    if (minimum >= maximum) {
        return Result<AxisOperatingRange, AxisOperatingRangeError>::failure(
            AxisOperatingRangeError::NonIncreasingRange);
    }

    return Result<AxisOperatingRange, AxisOperatingRangeError>::success(
        AxisOperatingRange(minimum, maximum, unit));
}

double AxisOperatingRange::minimum() const noexcept {
    return minimum_;
}

double AxisOperatingRange::maximum() const noexcept {
    return maximum_;
}

AxisCoordinateUnit AxisOperatingRange::unit() const noexcept {
    return unit_;
}

AxisAccuracySpecification::AxisAccuracySpecification(
    NonNegativeAxisAccuracy maximumAbsoluteError,
    std::string testConditions)
    : maximumAbsoluteError_(maximumAbsoluteError),
      testConditions_(std::move(testConditions)) {}

Result<
    AxisAccuracySpecification,
    AxisAccuracyError> AxisAccuracySpecification::create(
        NonNegativeAxisAccuracy maximumAbsoluteError,
        std::string testConditions) {
    if (!hasMeaningfulText(testConditions)) {
        return Result<
            AxisAccuracySpecification,
            AxisAccuracyError>::failure(
                AxisAccuracyError::EmptyTestConditions);
    }

    return Result<
        AxisAccuracySpecification,
        AxisAccuracyError>::success(
            AxisAccuracySpecification(
                maximumAbsoluteError,
                std::move(testConditions)));
}

double AxisAccuracySpecification::maximumAbsoluteError() const noexcept {
    return maximumAbsoluteError_.value();
}

const std::string& AxisAccuracySpecification::testConditions() const noexcept {
    return testConditions_;
}

AxisRepeatabilitySpecification::AxisRepeatabilitySpecification(
    NonNegativeAxisRepeatability value,
    AxisRepeatabilityMetric metric,
    std::string testConditions)
    : value_(value),
      metric_(metric),
      testConditions_(std::move(testConditions)) {}

Result<
    AxisRepeatabilitySpecification,
    AxisRepeatabilityError> AxisRepeatabilitySpecification::create(
        NonNegativeAxisRepeatability value,
        AxisRepeatabilityMetric metric,
        std::string testConditions) {
    switch (metric) {
    case AxisRepeatabilityMetric::MaximumDeviation:
    case AxisRepeatabilityMetric::StandardDeviation:
    case AxisRepeatabilityMetric::RootMeanSquare:
    case AxisRepeatabilityMetric::Unknown:
        break;
    default:
        return Result<
            AxisRepeatabilitySpecification,
            AxisRepeatabilityError>::failure(
                AxisRepeatabilityError::InvalidMetric);
    }

    if (!hasMeaningfulText(testConditions)) {
        return Result<
            AxisRepeatabilitySpecification,
            AxisRepeatabilityError>::failure(
                AxisRepeatabilityError::EmptyTestConditions);
    }

    return Result<
        AxisRepeatabilitySpecification,
        AxisRepeatabilityError>::success(
            AxisRepeatabilitySpecification(
                value,
                metric,
                std::move(testConditions)));
}

double AxisRepeatabilitySpecification::value() const noexcept {
    return value_.value();
}

AxisRepeatabilityMetric AxisRepeatabilitySpecification::metric() const noexcept {
    return metric_;
}

const std::string& AxisRepeatabilitySpecification::testConditions() const noexcept {
    return testConditions_;
}

AxisMeasurementUncertaintySpecification::AxisMeasurementUncertaintySpecification(
    NonNegativeAxisUncertainty value,
    std::string testConditions)
    : value_(value),
      testConditions_(std::move(testConditions)) {}

Result<
    AxisMeasurementUncertaintySpecification,
    AxisUncertaintyError> AxisMeasurementUncertaintySpecification::create(
        NonNegativeAxisUncertainty value,
        std::string testConditions) {
    if (!hasMeaningfulText(testConditions)) {
        return Result<
            AxisMeasurementUncertaintySpecification,
            AxisUncertaintyError>::failure(
                AxisUncertaintyError::EmptyTestConditions);
    }

    return Result<
        AxisMeasurementUncertaintySpecification,
        AxisUncertaintyError>::success(
            AxisMeasurementUncertaintySpecification(
                value,
                std::move(testConditions)));
}

double AxisMeasurementUncertaintySpecification::value() const noexcept {
    return value_.value();
}

const std::string&
AxisMeasurementUncertaintySpecification::testConditions() const noexcept {
    return testConditions_;
}

AxisSpecification::AxisSpecification(
    StageAxisId id,AxisMotionType motionType,AxisCoordinateUnit coordinateUnit,
    AxisOperatingRange operatingRange,PositiveAxisResolution nominalResolution,
    std::optional<AxisAccuracySpecification> accuracy,
    std::optional<AxisRepeatabilitySpecification> repeatability,
    std::optional<AxisMeasurementUncertaintySpecification> measurementUncertainty,
    AxisPositionFeedbackCapability positionFeedback,
    std::optional<AxisMeasurementLocation> measurementLocation,
    std::optional<StageLinearAxisRole> linearRole,
    std::optional<StageRotationAxisGeometry> rotationGeometry)
    : id_(std::move(id)),motionType_(motionType),coordinateUnit_(coordinateUnit),
      operatingRange_(operatingRange),nominalResolution_(nominalResolution),
      accuracy_(std::move(accuracy)),repeatability_(std::move(repeatability)),
      measurementUncertainty_(std::move(measurementUncertainty)),
      positionFeedback_(positionFeedback),measurementLocation_(measurementLocation),
      linearRole_(linearRole),rotationGeometry_(rotationGeometry) {}

Result<AxisSpecification, AxisSpecificationError> AxisSpecification::create(
    StageAxisId id,
    AxisMotionType motionType,
    AxisCoordinateUnit coordinateUnit,
    AxisOperatingRange operatingRange,
    PositiveAxisResolution nominalResolution,
    std::optional<AxisAccuracySpecification> accuracy,
    std::optional<AxisRepeatabilitySpecification> repeatability,
    std::optional<AxisMeasurementUncertaintySpecification> measurementUncertainty,
    AxisPositionFeedbackCapability positionFeedback,
    std::optional<AxisMeasurementLocation> measurementLocation,
    std::optional<StageLinearAxisRole> linearRole,
    std::optional<StageRotationAxisGeometry> rotationGeometry) {
    if (!isKnownAxisMotionType(motionType)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidMotionType);
    }

    if (!isKnownAxisCoordinateUnit(coordinateUnit)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidCoordinateUnit);
    }

    if (operatingRange.unit() != coordinateUnit) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::OperatingRangeUnitMismatch);
    }

    if (!isCoordinateUnitCompatible(motionType, coordinateUnit)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::UnitNotCompatibleWithMotion);
    }

    if (!isKnownPositionFeedback(positionFeedback)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidFeedbackCapability);
    }

    if (measurementLocation &&
        !isKnownMeasurementLocation(*measurementLocation)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidMeasurementLocation);
    }

    if (positionFeedback == AxisPositionFeedbackCapability::None &&
        measurementLocation) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::NoFeedbackHasMeasurementLocation);
    }

    if ((linearRole && motionType != AxisMotionType::Linear) ||
        (rotationGeometry && motionType != AxisMotionType::Rotary)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidMotionType);
    }
    if (linearRole && *linearRole != StageLinearAxisRole::X &&
        *linearRole != StageLinearAxisRole::Y &&
        *linearRole != StageLinearAxisRole::Z) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidMotionType);
    }
    if (rotationGeometry &&
        rotationGeometry->direction != StageRotationAxisDirection::X &&
        rotationGeometry->direction != StageRotationAxisDirection::Y &&
        rotationGeometry->direction != StageRotationAxisDirection::Z) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidMotionType);
    }

    return Result<AxisSpecification, AxisSpecificationError>::success(
        AxisSpecification(
            std::move(id),
            motionType,
            coordinateUnit,
            operatingRange,
            nominalResolution,
            std::move(accuracy),
            std::move(repeatability),
            std::move(measurementUncertainty),
            positionFeedback,
            measurementLocation,
            linearRole,
            rotationGeometry));
}

const StageAxisId& AxisSpecification::id() const noexcept {
    return id_;
}

AxisMotionType AxisSpecification::motionType() const noexcept {
    return motionType_;
}

AxisCoordinateUnit AxisSpecification::coordinateUnit() const noexcept {
    return coordinateUnit_;
}

const AxisOperatingRange& AxisSpecification::operatingRange() const noexcept {
    return operatingRange_;
}

double AxisSpecification::nominalResolution() const noexcept {
    return nominalResolution_.value();
}

const std::optional<AxisAccuracySpecification>&
AxisSpecification::accuracy() const noexcept {
    return accuracy_;
}

const std::optional<AxisRepeatabilitySpecification>&
AxisSpecification::repeatability() const noexcept {
    return repeatability_;
}

const std::optional<AxisMeasurementUncertaintySpecification>&
AxisSpecification::measurementUncertainty() const noexcept {
    return measurementUncertainty_;
}

AxisPositionFeedbackCapability
AxisSpecification::positionFeedback() const noexcept {
    return positionFeedback_;
}

const std::optional<AxisMeasurementLocation>&
AxisSpecification::measurementLocation() const noexcept {
    return measurementLocation_;
}

const std::optional<StageLinearAxisRole>&
AxisSpecification::linearRole() const noexcept { return linearRole_; }

const std::optional<StageRotationAxisGeometry>&
AxisSpecification::rotationGeometry() const noexcept { return rotationGeometry_; }

namespace {

bool addNanometres(StageNanometres a,StageNanometres b,StageNanometres& out) noexcept {
    if((b>0&&a>std::numeric_limits<StageNanometres>::max()-b)||
       (b<0&&a<std::numeric_limits<StageNanometres>::min()-b)) return false;
    out=a+b;
    return true;
}
bool slotBounds(const StageSlotSpecification& slot,
    StageNanometres& x0,StageNanometres& x1,
    StageNanometres& y0,StageNanometres& y1) noexcept {
    return addNanometres(slot.originInStageCoordinates().x,slot.xRange().minimum(),x0)&&
        addNanometres(slot.originInStageCoordinates().x,slot.xRange().maximum(),x1)&&
        addNanometres(slot.originInStageCoordinates().y,slot.yRange().minimum(),y0)&&
        addNanometres(slot.originInStageCoordinates().y,slot.yRange().maximum(),y1);
}
bool slotOverlaps(const StageSlotSpecification& a,const StageSlotSpecification& b) noexcept {
    StageNanometres ax0=0,ax1=0,ay0=0,ay1=0,bx0=0,bx1=0,by0=0,by1=0;
    if(!slotBounds(a,ax0,ax1,ay0,ay1)||!slotBounds(b,bx0,bx1,by0,by1)) return true;
    return ax0<=bx1&&bx0<=ax1&&ay0<=by1&&by0<=ay1;
}
std::optional<long double> linearValueNm(double value,AxisCoordinateUnit unit) noexcept {
    if(unit==AxisCoordinateUnit::Micrometers)
        return static_cast<long double>(value)*1'000.0L;
    if(unit==AxisCoordinateUnit::Millimeters)
        return static_cast<long double>(value)*1'000'000.0L;
    return std::nullopt;
}
std::optional<long double> angularValueRadians(double value,AxisCoordinateUnit unit) noexcept {
    constexpr long double kDegreesToRadians=0.017453292519943295769236907684886L;
    if(unit==AxisCoordinateUnit::Radians)return static_cast<long double>(value);
    if(unit==AxisCoordinateUnit::Degrees)return static_cast<long double>(value)*kDegreesToRadians;
    return std::nullopt;
}
bool effectiveRangeFits(const AxisSpecification& axis,const StageNanometreRange& range) noexcept {
    const auto minimum=linearValueNm(axis.operatingRange().minimum(),axis.coordinateUnit());
    const auto maximum=linearValueNm(axis.operatingRange().maximum(),axis.coordinateUnit());
    return minimum&&maximum&&
        static_cast<long double>(range.minimum())>=*minimum&&
        static_cast<long double>(range.maximum())<=*maximum;
}
bool effectiveRangeFits(const AxisSpecification& axis,const StageRadianRange& range) noexcept {
    const auto minimum=angularValueRadians(axis.operatingRange().minimum(),axis.coordinateUnit());
    const auto maximum=angularValueRadians(axis.operatingRange().maximum(),axis.coordinateUnit());
    return minimum&&maximum&&
        static_cast<long double>(range.minimum())>=*minimum&&
        static_cast<long double>(range.maximum())<=*maximum;
}
} // namespace

StageNanometreRange::StageNanometreRange(StageNanometres minimum,StageNanometres maximum) noexcept
    :minimum_(minimum),maximum_(maximum) {}
Result<StageNanometreRange,StageNanometreRangeError>
StageNanometreRange::create(StageNanometres minimum,StageNanometres maximum) {
    if(minimum>=maximum)
        return Result<StageNanometreRange,StageNanometreRangeError>::failure(
            StageNanometreRangeError::NonIncreasingRange);
    return Result<StageNanometreRange,StageNanometreRangeError>::success(
        StageNanometreRange(minimum,maximum));
}
StageNanometres StageNanometreRange::minimum()const noexcept{return minimum_;}
StageNanometres StageNanometreRange::maximum()const noexcept{return maximum_;}
bool StageNanometreRange::contains(StageNanometres value)const noexcept {
    return value>=minimum_&&value<=maximum_;
}

StageRadianRange::StageRadianRange(double minimum,double maximum)noexcept
    :minimum_(minimum),maximum_(maximum){}
Result<StageRadianRange,StageRadianRangeError>
StageRadianRange::create(double minimum,double maximum) {
    if(!std::isfinite(minimum)||!std::isfinite(maximum)||minimum>=maximum)
        return Result<StageRadianRange,StageRadianRangeError>::failure(
            StageRadianRangeError::InvalidRange);
    return Result<StageRadianRange,StageRadianRangeError>::success(
        StageRadianRange(minimum,maximum));
}
double StageRadianRange::minimum()const noexcept{return minimum_;}
double StageRadianRange::maximum()const noexcept{return maximum_;}
bool StageRadianRange::contains(double value)const noexcept {
    return std::isfinite(value)&&value>=minimum_&&value<=maximum_;
}

StageSlotSpecification::StageSlotSpecification(StageSlotId id,
    StagePosition2DNanometres origin,StageNanometreRange x,StageNanometreRange y)
    :id_(std::move(id)),origin_(origin),xRange_(x),yRange_(y){}
Result<StageSlotSpecification,StageSlotSpecificationError>
StageSlotSpecification::create(StageSlotId id,StagePosition2DNanometres origin,
    StageNanometreRange x,StageNanometreRange y) {
    StageNanometres ignored=0;
    if(x.minimum()<0||y.minimum()<0)
        return Result<StageSlotSpecification,StageSlotSpecificationError>::failure(
            StageSlotSpecificationError::InvalidLocalRange);
    if(!addNanometres(origin.x,x.maximum(),ignored)||
       !addNanometres(origin.y,y.maximum(),ignored))
        return Result<StageSlotSpecification,StageSlotSpecificationError>::failure(
            StageSlotSpecificationError::CoordinateOverflow);
    return Result<StageSlotSpecification,StageSlotSpecificationError>::success(
        StageSlotSpecification(std::move(id),origin,x,y));
}
const StageSlotId& StageSlotSpecification::id()const noexcept{return id_;}
const StagePosition2DNanometres&
StageSlotSpecification::originInStageCoordinates()const noexcept{return origin_;}
const StageNanometreRange& StageSlotSpecification::xRange()const noexcept{return xRange_;}
const StageNanometreRange& StageSlotSpecification::yRange()const noexcept{return yRange_;}

StageConfigurationSpecification::StageConfigurationSpecification(StageConfigurationId id,
    std::vector<StageWorkingLinearAxisSpecification> linear,
    std::vector<StageWorkingRotationAxisSpecification> rotary,
    std::vector<StageSlotSpecification> slots)
    :id_(std::move(id)),workingLinearAxes_(std::move(linear)),
     workingRotationAxes_(std::move(rotary)),slots_(std::move(slots)){}
Result<StageConfigurationSpecification,StageConfigurationSpecificationError>
StageConfigurationSpecification::create(StageConfigurationId id,
    std::vector<StageWorkingLinearAxisSpecification> linear,
    std::vector<StageWorkingRotationAxisSpecification> rotary,
    std::vector<StageSlotSpecification> slots) {
    if(slots.empty())
        return Result<StageConfigurationSpecification,StageConfigurationSpecificationError>::failure(
            StageConfigurationSpecificationError::NoSlots);
    std::set<StageAxisId> axes;
    for(const auto& axis:linear) if(!axes.insert(axis.axisId).second)
        return Result<StageConfigurationSpecification,StageConfigurationSpecificationError>::failure(
            StageConfigurationSpecificationError::DuplicateWorkingAxis);
    for(const auto& axis:rotary) if(!axes.insert(axis.axisId).second)
        return Result<StageConfigurationSpecification,StageConfigurationSpecificationError>::failure(
            StageConfigurationSpecificationError::DuplicateWorkingAxis);
    std::set<StageSlotId> slotIds;
    for(std::size_t i=0;i<slots.size();++i) {
        if(!slotIds.insert(slots[i].id()).second)
            return Result<StageConfigurationSpecification,StageConfigurationSpecificationError>::failure(
                StageConfigurationSpecificationError::DuplicateSlotId);
        for(std::size_t j=i+1;j<slots.size();++j) if(slotOverlaps(slots[i],slots[j]))
            return Result<StageConfigurationSpecification,StageConfigurationSpecificationError>::failure(
                StageConfigurationSpecificationError::OverlappingSlots);
    }
    return Result<StageConfigurationSpecification,StageConfigurationSpecificationError>::success(
        StageConfigurationSpecification(std::move(id),std::move(linear),std::move(rotary),std::move(slots)));
}
const StageConfigurationId& StageConfigurationSpecification::id()const noexcept{return id_;}
const std::vector<StageWorkingLinearAxisSpecification>&
StageConfigurationSpecification::workingLinearAxes()const noexcept{return workingLinearAxes_;}
const std::vector<StageWorkingRotationAxisSpecification>&
StageConfigurationSpecification::workingRotationAxes()const noexcept{return workingRotationAxes_;}
const std::vector<StageSlotSpecification>& StageConfigurationSpecification::slots()const noexcept{return slots_;}

StageSpecification::StageSpecification(
    EquipmentComponentId componentId,
    std::vector<AxisSpecification> axes,
    std::vector<StageConfigurationSpecification> configurations)
    :componentId_(std::move(componentId)),axes_(std::move(axes)),
     configurations_(std::move(configurations)){}

Result<StageSpecification,StageSpecificationError> StageSpecification::create(
    EquipmentComponentId componentId,std::vector<AxisSpecification> axes,
    std::vector<StageConfigurationSpecification> configurations) {
    if(axes.empty())
        return Result<StageSpecification,StageSpecificationError>::failure(StageSpecificationError::NoAxes);
    std::set<StageAxisId> axisIds;
    std::set<StageLinearAxisRole> physicalLinearRoles;
    for(const auto& axis:axes) {
        if(!axisIds.insert(axis.id()).second)
            return Result<StageSpecification,StageSpecificationError>::failure(StageSpecificationError::DuplicateAxisId);
        if(axis.linearRole()&&
           (axis.motionType()!=AxisMotionType::Linear||
            !physicalLinearRoles.insert(*axis.linearRole()).second))
            return Result<StageSpecification,StageSpecificationError>::failure(
                StageSpecificationError::DuplicateLinearAxisRole);
        if(axis.rotationGeometry()&&axis.motionType()!=AxisMotionType::Rotary)
            return Result<StageSpecification,StageSpecificationError>::failure(
                StageSpecificationError::InvalidAxisRoleBinding);
    }

    std::set<StageConfigurationId> configurationIds;
    for(const auto& configuration:configurations) {
        if(!configurationIds.insert(configuration.id()).second)
            return Result<StageSpecification,StageSpecificationError>::failure(
                StageSpecificationError::DuplicateConfigurationId);
        const StageWorkingLinearAxisSpecification *x=nullptr,*y=nullptr,*z=nullptr;
        std::set<StageLinearAxisRole> roles;
        for(const auto& working:configuration.workingLinearAxes()) {
            const auto axis=std::find_if(axes.begin(),axes.end(),
                [&working](const AxisSpecification& candidate){return candidate.id()==working.axisId;});
            if(axis==axes.end()||axis->motionType()!=AxisMotionType::Linear||!axis->linearRole())
                return Result<StageSpecification,StageSpecificationError>::failure(
                    StageSpecificationError::UnknownWorkingAxis);
            if(!effectiveRangeFits(*axis,working.effectiveRange))
                return Result<StageSpecification,StageSpecificationError>::failure(
                    StageSpecificationError::InvalidEffectiveAxisRange);
            if(!roles.insert(*axis->linearRole()).second)
                return Result<StageSpecification,StageSpecificationError>::failure(
                    StageSpecificationError::InvalidAxisRoleBinding);
            if(*axis->linearRole()==StageLinearAxisRole::X)x=&working;
            if(*axis->linearRole()==StageLinearAxisRole::Y)y=&working;
            if(*axis->linearRole()==StageLinearAxisRole::Z)z=&working;
        }
        if(!x)return Result<StageSpecification,StageSpecificationError>::failure(StageSpecificationError::MissingXWorkingAxis);
        if(!y)return Result<StageSpecification,StageSpecificationError>::failure(StageSpecificationError::MissingYWorkingAxis);
        if(!z)return Result<StageSpecification,StageSpecificationError>::failure(StageSpecificationError::MissingZWorkingAxis);

        for(const auto& working:configuration.workingRotationAxes()) {
            const auto axis=std::find_if(axes.begin(),axes.end(),
                [&working](const AxisSpecification& candidate){return candidate.id()==working.axisId;});
            if(axis==axes.end()||axis->motionType()!=AxisMotionType::Rotary||!axis->rotationGeometry())
                return Result<StageSpecification,StageSpecificationError>::failure(
                    StageSpecificationError::UnknownWorkingAxis);
            if(!effectiveRangeFits(*axis,working.effectiveRange))
                return Result<StageSpecification,StageSpecificationError>::failure(
                    StageSpecificationError::InvalidEffectiveAxisRange);
        }
        for(const auto& slot:configuration.slots()) {
            StageNanometres x0=0,x1=0,y0=0,y1=0;
            if(!slotBounds(slot,x0,x1,y0,y1)||
               !x->effectiveRange.contains(x0)||!x->effectiveRange.contains(x1)||
               !y->effectiveRange.contains(y0)||!y->effectiveRange.contains(y1))
                return Result<StageSpecification,StageSpecificationError>::failure(
                    StageSpecificationError::SlotOutsideWorkingRange);
        }
    }

    return Result<StageSpecification,StageSpecificationError>::success(
        StageSpecification(std::move(componentId),std::move(axes),std::move(configurations)));
}

const EquipmentComponentId& StageSpecification::componentId() const noexcept {
    return componentId_;
}

const std::vector<AxisSpecification>& StageSpecification::axes() const noexcept {
    return axes_;
}
const std::vector<StageConfigurationSpecification>&
StageSpecification::configurations() const noexcept { return configurations_; }

const AxisSpecification* StageSpecification::axis(const StageAxisId& id) const noexcept {
    const auto it=std::find_if(axes_.begin(),axes_.end(),
        [&id](const AxisSpecification& value){return value.id()==id;});
    return it==axes_.end()?nullptr:&*it;
}

const AxisSpecification* StageSpecification::linearAxis(StageLinearAxisRole role) const noexcept {
    const auto it=std::find_if(axes_.begin(),axes_.end(),
        [role](const AxisSpecification& value){
            return value.linearRole()&&*value.linearRole()==role;
        });
    return it==axes_.end()?nullptr:&*it;
}

} // namespace microscope::core
