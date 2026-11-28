#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/equipment/StageSpecifications.h"

#include <iostream>
#include <optional>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace microscope::core;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename TValue, typename TError>
TValue take(Result<TValue, TError> result) {
    if (!result) {
        throw std::runtime_error("A test fixture factory rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

PositiveAxisResolution resolution(double value) {
    return take(PositiveAxisResolution::create(value));
}

NonNegativeAxisAccuracy accuracy(double value) {
    return take(NonNegativeAxisAccuracy::create(value));
}

NonNegativeAxisRepeatability repeatability(double value) {
    return take(NonNegativeAxisRepeatability::create(value));
}

NonNegativeAxisUncertainty uncertainty(double value) {
    return take(NonNegativeAxisUncertainty::create(value));
}

AxisOperatingRange range(
    double minimum,
    double maximum,
    AxisCoordinateUnit unit) {
    return take(AxisOperatingRange::create(minimum, maximum, unit));
}

AxisAccuracySpecification accuracySpec(
    double value,
    std::string conditions = "calibrated load, bidirectional approach") {
    return take(AxisAccuracySpecification::create(
        accuracy(value), std::move(conditions)));
}

AxisRepeatabilitySpecification repeatabilitySpec(
    double value,
    AxisRepeatabilityMetric metric = AxisRepeatabilityMetric::MaximumDeviation,
    std::string conditions = "10 repeated cycles, nominal load") {
    return take(AxisRepeatabilitySpecification::create(
        repeatability(value), metric, std::move(conditions)));
}

AxisMeasurementUncertaintySpecification uncertaintySpec(
    double value,
    std::string conditions = "reference instrument uncertainty budget") {
    return take(AxisMeasurementUncertaintySpecification::create(
        uncertainty(value), std::move(conditions)));
}

AxisSpecification configuredLinearAxis(
    const char* id,StageLinearAxisRole role,double minimumUm,double maximumUm) {
    return take(AxisSpecification::create(
        makeId<StageAxisId>(id),AxisMotionType::Linear,AxisCoordinateUnit::Micrometers,
        range(minimumUm,maximumUm,AxisCoordinateUnit::Micrometers),resolution(0.1),
        std::nullopt,std::nullopt,std::nullopt,
        AxisPositionFeedbackCapability::None,std::nullopt,role));
}
AxisSpecification configuredRotationAxis(
    const char* id,StageRotationAxisDirection direction) {
    return take(AxisSpecification::create(
        makeId<StageAxisId>(id),AxisMotionType::Rotary,AxisCoordinateUnit::Degrees,
        range(-180.0,180.0,AxisCoordinateUnit::Degrees),resolution(0.01),
        std::nullopt,std::nullopt,std::nullopt,
        AxisPositionFeedbackCapability::None,std::nullopt,std::nullopt,
        StageRotationAxisGeometry{{0,0,15'000'000},direction}));
}
StageNanometreRange nmRange(StageNanometres minimum,StageNanometres maximum) {
    return take(StageNanometreRange::create(minimum,maximum));
}
StageRadianRange radRange(double minimum,double maximum) {
    return take(StageRadianRange::create(minimum,maximum));
}

AxisSpecification linearAxis(
    const char* id,
    std::optional<AxisAccuracySpecification> accuracyValue,
    std::optional<AxisRepeatabilitySpecification> repeatabilityValue,
    std::optional<AxisMeasurementUncertaintySpecification> uncertaintyValue,
    AxisPositionFeedbackCapability feedback =
        AxisPositionFeedbackCapability::Encoder,
    std::optional<AxisMeasurementLocation> location =
        AxisMeasurementLocation::Axis) {
    return take(AxisSpecification::create(
        makeId<StageAxisId>(id),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,
        range(-100'000.0, 100'000.0, AxisCoordinateUnit::Micrometers),
        resolution(0.1),
        std::move(accuracyValue),
        std::move(repeatabilityValue),
        std::move(uncertaintyValue),
        feedback,
        location));
}

EquipmentComponent makeMechanicalAssembly(const EquipmentComponentId& id) {
    return take(EquipmentComponent::create(
        id, EquipmentComponentKind::MechanicalAssembly, {}));
}

EquipmentComponent makeOpticalElement(const EquipmentComponentId& id) {
    auto input = take(OpticalPort::create(
        makeId<OpticalPortId>("stage-test-optical-input"),
        OpticalPortDirection::Input));
    auto output = take(OpticalPort::create(
        makeId<OpticalPortId>("stage-test-optical-output"),
        OpticalPortDirection::Output));
    return take(EquipmentComponent::create(
        id, EquipmentComponentKind::OpticalElement,
        {std::move(input), std::move(output)}));
}

void testTypedAxisUnitsRemainDistinct() {
    static_assert(!std::is_convertible<
        PositiveAxisResolution, PositiveMicrometers>::value,
        "axis resolution and physical length values must not convert implicitly");
    static_assert(!std::is_convertible<
        NonNegativeAxisAccuracy, NonNegativeAxisRepeatability>::value,
        "accuracy and repeatability values must remain distinct");

    auto linearDegrees = AxisSpecification::create(
        makeId<StageAxisId>("linear-with-degrees"),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Degrees,
        range(-90.0, 90.0, AxisCoordinateUnit::Degrees),
        resolution(0.1),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AxisPositionFeedbackCapability::Unknown,
        std::nullopt);
    check(!linearDegrees &&
          linearDegrees.error() ==
              AxisSpecificationError::UnitNotCompatibleWithMotion,
          "rejects angular units for a declared linear axis");

    auto rotaryMicrometers = AxisSpecification::create(
        makeId<StageAxisId>("rotary-with-micrometers"),
        AxisMotionType::Rotary,
        AxisCoordinateUnit::Micrometers,
        range(0.0, 100.0, AxisCoordinateUnit::Micrometers),
        resolution(0.1),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AxisPositionFeedbackCapability::Unknown,
        std::nullopt);
    check(!rotaryMicrometers &&
          rotaryMicrometers.error() ==
              AxisSpecificationError::UnitNotCompatibleWithMotion,
          "rejects linear units for a declared rotary axis");

    auto rangeUnitMismatch = AxisSpecification::create(
        makeId<StageAxisId>("range-unit-mismatch"),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,
        range(-10.0, 10.0, AxisCoordinateUnit::Millimeters),
        resolution(0.1),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AxisPositionFeedbackCapability::Unknown,
        std::nullopt);
    check(!rangeUnitMismatch &&
          rangeUnitMismatch.error() ==
              AxisSpecificationError::OperatingRangeUnitMismatch,
          "requires operating range units to match the axis coordinate unit");
}

void testResolutionAccuracyRepeatabilityAndUncertaintyAreIndependent() {
    const auto axis = linearAxis(
        "x-axis",
        accuracySpec(1.2),
        repeatabilitySpec(0.3),
        uncertaintySpec(0.05));

    check(axis.nominalResolution() == 0.1,
          "preserves nominal command/readout resolution");
    check(axis.accuracy().has_value() &&
          axis.accuracy()->maximumAbsoluteError() == 1.2,
          "preserves independently specified maximum absolute accuracy error");
    check(axis.repeatability().has_value() &&
          axis.repeatability()->value() == 0.3 &&
          axis.repeatability()->metric() ==
              AxisRepeatabilityMetric::MaximumDeviation,
          "preserves precision/repeatability value and metric");
    check(axis.measurementUncertainty().has_value() &&
          axis.measurementUncertainty()->value() == 0.05,
          "preserves independent measurement uncertainty");
    check(axis.accuracy()->testConditions().find("bidirectional") !=
              std::string::npos &&
          axis.repeatability()->testConditions().find("10 repeated cycles") !=
              std::string::npos,
          "retains measurement conditions for accuracy and repeatability");

    const auto rotary = take(AxisSpecification::create(
        makeId<StageAxisId>("theta-axis"),
        AxisMotionType::Rotary,
        AxisCoordinateUnit::Degrees,
        range(-180.0, 180.0, AxisCoordinateUnit::Degrees),
        resolution(0.01),
        std::nullopt,
        repeatabilitySpec(0.02, AxisRepeatabilityMetric::StandardDeviation),
        std::nullopt,
        AxisPositionFeedbackCapability::Encoder,
        AxisMeasurementLocation::Axis));
    check(rotary.motionType() == AxisMotionType::Rotary &&
          rotary.coordinateUnit() == AxisCoordinateUnit::Degrees &&
          rotary.operatingRange().minimum() == -180.0,
          "supports rotary axes and signed operating ranges");
    check(!rotary.accuracy() && rotary.repeatability().has_value(),
          "allows unavailable performance specifications to remain absent");
}

void testFeedbackCapabilityAndLocationAreExplicit() {
    auto noFeedbackWithLocation = AxisSpecification::create(
        makeId<StageAxisId>("axis-no-feedback-invalid-location"),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,
        range(0.0, 1'000.0, AxisCoordinateUnit::Micrometers),
        resolution(1.0),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AxisPositionFeedbackCapability::None,
        AxisMeasurementLocation::Motor);
    check(!noFeedbackWithLocation &&
          noFeedbackWithLocation.error() ==
              AxisSpecificationError::NoFeedbackHasMeasurementLocation,
          "does not assign a measurement location when no position feedback exists");

    auto unknownFeedback = AxisSpecification::create(
        makeId<StageAxisId>("axis-unknown-feedback"),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Millimeters,
        range(-5.0, 5.0, AxisCoordinateUnit::Millimeters),
        resolution(0.001),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AxisPositionFeedbackCapability::Unknown,
        std::nullopt);
    check(static_cast<bool>(unknownFeedback),
          "allows unknown feedback capability without inventing a measurement source");

    auto invalidFeedback = AxisSpecification::create(
        makeId<StageAxisId>("axis-invalid-feedback"),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,
        range(0.0, 100.0, AxisCoordinateUnit::Micrometers),
        resolution(0.1),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        static_cast<AxisPositionFeedbackCapability>(255),
        std::nullopt);
    check(!invalidFeedback &&
          invalidFeedback.error() ==
              AxisSpecificationError::InvalidFeedbackCapability,
          "rejects unknown position feedback capability values");

    auto invalidLocation = AxisSpecification::create(
        makeId<StageAxisId>("axis-invalid-location"),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,
        range(0.0, 100.0, AxisCoordinateUnit::Micrometers),
        resolution(0.1),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AxisPositionFeedbackCapability::Encoder,
        static_cast<AxisMeasurementLocation>(255));
    check(!invalidLocation &&
          invalidLocation.error() ==
              AxisSpecificationError::InvalidMeasurementLocation,
          "rejects unknown feedback measurement locations");
}

void testStageRequiresUniqueAxisIdentities() {
    auto noAxes = StageSpecification::create(
        makeId<EquipmentComponentId>("empty-stage"),
        {});
    check(!noAxes && noAxes.error() == StageSpecificationError::NoAxes,
          "requires a stage to declare at least one axis");

    const auto firstAxis = linearAxis(
        "x-axis-duplicate",
        std::nullopt,
        std::nullopt,
        std::nullopt);
    const auto secondAxis = linearAxis(
        "x-axis-duplicate",
        std::nullopt,
        std::nullopt,
        std::nullopt);
    auto duplicateAxes = StageSpecification::create(
        makeId<EquipmentComponentId>("duplicate-axis-stage"),
        {firstAxis, secondAxis});
    check(!duplicateAxes &&
          duplicateAxes.error() == StageSpecificationError::DuplicateAxisId,
          "rejects duplicate axis identity within one stage");

    auto validStage = StageSpecification::create(
        makeId<EquipmentComponentId>("multi-axis-stage"),
        {linearAxis("x-axis", std::nullopt, std::nullopt, std::nullopt),
         linearAxis("y-axis", std::nullopt, std::nullopt, std::nullopt)});
    check(static_cast<bool>(validStage) && validStage.value().axes().size() == 2,
          "supports multiple independently specified axes in one stage");
}

void testGenericStageConfigurationsAndNPhysicalSlots() {
    const auto xId=makeId<StageAxisId>("stage-x");
    const auto yId=makeId<StageAxisId>("stage-y");
    const auto zId=makeId<StageAxisId>("stage-z");
    const auto thetaId=makeId<StageAxisId>("stage-theta");
    auto x=configuredLinearAxis("stage-x",StageLinearAxisRole::X,0.0,900'000.0);
    auto y=configuredLinearAxis("stage-y",StageLinearAxisRole::Y,0.0,300'000.0);
    auto z=configuredLinearAxis("stage-z",StageLinearAxisRole::Z,0.0,100'000.0);
    auto theta=configuredRotationAxis("stage-theta",StageRotationAxisDirection::Z);

    const auto slidesId=makeId<StageConfigurationId>("slides-holder");
    const auto petriId=makeId<StageConfigurationId>("petri-holder");
    std::vector<StageSlotSpecification> slideSlots;
    for(int index=0;index<10;++index) {
        const auto name="slide-slot-"+std::to_string(index+1);
        slideSlots.push_back(take(StageSlotSpecification::create(
            makeId<StageSlotId>(name.c_str()),
            {20'000'000+static_cast<StageNanometres>(index)*80'000'000,20'000'000},
            nmRange(0,75'000'000),nmRange(0,25'000'000))));
    }

    auto slideConfiguration=take(StageConfigurationSpecification::create(
        slidesId,
        {{xId,nmRange(0,900'000'000)},
         {yId,nmRange(0,300'000'000)},
         {zId,nmRange(8'000'000,80'000'000)}},
        {},std::move(slideSlots)));
    auto petriSlot=take(StageSlotSpecification::create(
        makeId<StageSlotId>("petri-dish-slot"),{0,0},
        nmRange(0,150'000'000),nmRange(0,150'000'000)));
    auto petriConfiguration=take(StageConfigurationSpecification::create(
        petriId,
        {{xId,nmRange(0,900'000'000)},
         {yId,nmRange(0,300'000'000)},
         {zId,nmRange(2'000'000,60'000'000)}},
        {{thetaId,radRange(-1.5707963267948966,1.5707963267948966)}},
        {std::move(petriSlot)}));

    auto stage=take(StageSpecification::create(
        makeId<EquipmentComponentId>("configured-stage"),
        {std::move(x),std::move(y),std::move(z),std::move(theta)},
        {std::move(slideConfiguration),std::move(petriConfiguration)}));
    check(stage.configurations().size()==2,
        "one installed Core Stage contains multiple working configurations");
    check(stage.configurations()[0].slots().size()==10,
        "holder configuration represents ten physical slots without a dual-slide type");
    check(stage.configurations()[1].slots().size()==1,
        "Petri-dish holder is represented as one generic slot");
    check(stage.configurations()[0].workingRotationAxes().empty(),
        "rotation is absent from a configuration that does not use it");
    check(stage.configurations()[1].workingRotationAxes().size()==1,
        "rotation is included only in the configuration that uses it");
    check(stage.configurations()[0].workingLinearAxes()[2].effectiveRange.minimum()==8'000'000&&
          stage.configurations()[1].workingLinearAxes()[2].effectiveRange.minimum()==2'000'000,
        "each configuration owns its own effective Z working range");

    auto tooWideSlot=take(StageSlotSpecification::create(
        makeId<StageSlotId>("outside-slot"),{880'000'000,0},
        nmRange(0,50'000'000),nmRange(0,25'000'000)));
    auto invalidConfig=take(StageConfigurationSpecification::create(
        makeId<StageConfigurationId>("outside-envelope"),
        {{xId,nmRange(0,900'000'000)},
         {yId,nmRange(0,300'000'000)},
         {zId,nmRange(0,100'000'000)}},{},{std::move(tooWideSlot)}));
    auto outside=StageSpecification::create(
        makeId<EquipmentComponentId>("outside-stage"),
        {configuredLinearAxis("stage-x",StageLinearAxisRole::X,0.0,900'000.0),
         configuredLinearAxis("stage-y",StageLinearAxisRole::Y,0.0,300'000.0),
         configuredLinearAxis("stage-z",StageLinearAxisRole::Z,0.0,100'000.0)},
        {std::move(invalidConfig)});
    check(!outside&&outside.error()==StageSpecificationError::SlotOutsideWorkingRange,
        "rejects slots extending beyond the configuration's XY envelope");

    auto invalidZ=take(StageConfigurationSpecification::create(
        makeId<StageConfigurationId>("outside-z"),
        {{xId,nmRange(0,900'000'000)},
         {yId,nmRange(0,300'000'000)},
         {zId,nmRange(0,110'000'000)}},{},
        {take(StageSlotSpecification::create(
            makeId<StageSlotId>("z-slot"),{0,0},
            nmRange(0,50'000'000),nmRange(0,25'000'000)))}));
    auto zOutside=StageSpecification::create(
        makeId<EquipmentComponentId>("z-outside-stage"),
        {configuredLinearAxis("stage-x",StageLinearAxisRole::X,0.0,900'000.0),
         configuredLinearAxis("stage-y",StageLinearAxisRole::Y,0.0,300'000.0),
         configuredLinearAxis("stage-z",StageLinearAxisRole::Z,0.0,100'000.0)},
        {std::move(invalidZ)});
    check(!zOutside&&zOutside.error()==StageSpecificationError::InvalidEffectiveAxisRange,
        "rejects a configuration whose effective Z range exceeds mechanical travel");

    auto missingZ=take(StageConfigurationSpecification::create(
        makeId<StageConfigurationId>("missing-z"),
        {{xId,nmRange(0,900'000'000)},{yId,nmRange(0,300'000'000)}},{},
        {take(StageSlotSpecification::create(
            makeId<StageSlotId>("missing-z-slot"),{0,0},
            nmRange(0,50'000'000),nmRange(0,25'000'000)))}));
    auto noZ=StageSpecification::create(
        makeId<EquipmentComponentId>("missing-z-stage"),
        {configuredLinearAxis("stage-x",StageLinearAxisRole::X,0.0,900'000.0),
         configuredLinearAxis("stage-y",StageLinearAxisRole::Y,0.0,300'000.0),
         configuredLinearAxis("stage-z",StageLinearAxisRole::Z,0.0,100'000.0)},
        {std::move(missingZ)});
    check(!noZ&&noZ.error()==StageSpecificationError::MissingZWorkingAxis,
        "every active configuration must define an effective Z range");

    auto touchingA=take(StageSlotSpecification::create(
        makeId<StageSlotId>("touching-a"),{0,0},
        nmRange(0,10'000),nmRange(0,10'000)));
    auto touchingB=take(StageSlotSpecification::create(
        makeId<StageSlotId>("touching-b"),{10'000,0},
        nmRange(0,10'000),nmRange(0,10'000)));
    auto touchingConfiguration=StageConfigurationSpecification::create(
        makeId<StageConfigurationId>("touching-slots"),
        {{xId,nmRange(0,900'000'000)},
         {yId,nmRange(0,300'000'000)},
         {zId,nmRange(0,100'000'000)}},{},
        {std::move(touchingA),std::move(touchingB)});
    check(!touchingConfiguration&&
          touchingConfiguration.error()==StageConfigurationSpecificationError::OverlappingSlots,
        "rejects slots sharing a boundary because slot ranges are inclusive");
}

void testEquipmentAggregateValidatesStageBinding() {
    const auto microscopeId = makeId<MicroscopeId>("stage-binding-microscope");
    const auto stageId = makeId<EquipmentComponentId>("stage-mechanical-assembly");
    const auto stageComponent = makeMechanicalAssembly(stageId);
    const auto stageSpecification = take(StageSpecification::create(
        stageId,
        {linearAxis("x-axis", std::nullopt, std::nullopt, std::nullopt),
         linearAxis("y-axis", std::nullopt, std::nullopt, std::nullopt)}));

    auto validDefinition = MicroscopeEquipmentDefinition::create(
        microscopeId,
        {stageComponent},
        {},
        {},
        {},
        {},
        {},
        {},
        {stageSpecification});
    check(static_cast<bool>(validDefinition),
          "accepts stage axes attached to an installed mechanical assembly");
    if (validDefinition) {
        check(validDefinition.value().stageSpecifications().size() == 1 &&
              validDefinition.value().stageSpecifications()[0].axes().size() == 2,
              "equipment definition retains the stage and its axes");
    }

    auto stageOnOptic = MicroscopeEquipmentDefinition::create(
        microscopeId,
        {makeOpticalElement(stageId)},
        {},
        {},
        {},
        {},
        {},
        {},
        {stageSpecification});
    check(!stageOnOptic &&
          stageOnOptic.error() == EquipmentDefinitionError::StageComponentWrongKind,
          "requires a stage specification to reference a mechanical assembly");

    auto unknownStage = StageSpecification::create(
        makeId<EquipmentComponentId>("stage-not-installed"),
        {linearAxis("x-axis", std::nullopt, std::nullopt, std::nullopt)});
    auto missingStage = MicroscopeEquipmentDefinition::create(
        microscopeId,
        {stageComponent},
        {},
        {},
        {},
        {},
        {},
        {},
        {take(std::move(unknownStage))});
    check(!missingStage &&
          missingStage.error() ==
              EquipmentDefinitionError::StageReferencesUnknownComponent,
          "rejects a stage specification for an uninstalled component");

    auto duplicateStage = MicroscopeEquipmentDefinition::create(
        microscopeId,
        {stageComponent},
        {},
        {},
        {},
        {},
        {},
        {},
        {stageSpecification, stageSpecification});
    check(!duplicateStage &&
          duplicateStage.error() ==
              EquipmentDefinitionError::DuplicateStageSpecification,
          "rejects duplicate stage specifications for one physical component");
}

} // namespace

int main() {
    testTypedAxisUnitsRemainDistinct();
    testResolutionAccuracyRepeatabilityAndUncertaintyAreIndependent();
    testFeedbackCapabilityAndLocationAreExplicit();
    testStageRequiresUniqueAxisIdentities();
    testEquipmentAggregateValidatesStageBinding();
    testGenericStageConfigurationsAndNPhysicalSlots();

    if (failures != 0) {
        std::cerr << failures << " stage/axis contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All stage/axis specification contract tests passed.\n";
    return 0;
}
