#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/equipment/OpticalSpecifications.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <limits>
#include <stdexcept>
#include <string>
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

PositiveMicrometers micrometers(double value) {
    return take(PositiveMicrometers::create(value));
}

NominalMagnification magnification(double value) {
    return take(NominalMagnification::create(value));
}

NumericalAperture numericalAperture(double value) {
    return take(NumericalAperture::create(value));
}

PixelDimensions dimensions(std::uint32_t width, std::uint32_t height) {
    return take(PixelDimensions::create(width, height));
}

SensorRegionOfInterest roi(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height) {
    return take(SensorRegionOfInterest::create(x, y, width, height));
}

PixelBinning binning(std::uint32_t x, std::uint32_t y) {
    return take(PixelBinning::create(x, y));
}

CameraOutputMode outputMode(
    const char* id,
    SensorRegionOfInterest sensorRegion,
    PixelBinning pixelBinning,
    PixelDimensions delivered,
    CameraPixelLayout layout = CameraPixelLayout::Monochrome,
    std::string formatCode = "Mono16") {
    return take(CameraOutputMode::create(
        makeId<CameraModeId>(id),
        sensorRegion,
        pixelBinning,
        delivered,
        layout,
        std::move(formatCode)));
}

ObjectiveSpecification objectiveSpecification(
    const EquipmentComponentId& componentId) {
    return take(ObjectiveSpecification::create(
        componentId,
        magnification(60.0),
        numericalAperture(1.49),
        micrometers(130.0),
        ObjectiveChromaticCorrection::Apochromatic,
        ObjectiveFieldCorrection::Plan,
        ObjectiveImmersionMedium::Oil,
        micrometers(0.17)));
}

CameraSpecification cameraSpecification(
    const EquipmentComponentId& componentId) {
    return take(CameraSpecification::create(
        componentId,
        dimensions(4096, 3000),
        micrometers(3.45),
        micrometers(3.45),
        {outputMode(
             "full-frame",
             roi(0, 0, 4096, 3000),
             binning(1, 1),
             dimensions(2048, 1500)),
         outputMode(
             "two-by-two-binned",
             roi(256, 100, 2048, 1500),
             binning(2, 2),
             dimensions(1024, 750))}));
}

EquipmentComponent makeObjectiveComponent(const EquipmentComponentId& id) {
    auto input = take(OpticalPort::create(
        makeId<OpticalPortId>("objective-input"),
        OpticalPortDirection::Input));
    auto output = take(OpticalPort::create(
        makeId<OpticalPortId>("objective-output"),
        OpticalPortDirection::Output));
    return take(EquipmentComponent::create(
        id,
        EquipmentComponentKind::OpticalElement,
        {std::move(input), std::move(output)}));
}

EquipmentComponent makeCameraComponent(const EquipmentComponentId& id) {
    auto input = take(OpticalPort::create(
        makeId<OpticalPortId>("camera-input"),
        OpticalPortDirection::Input));
    return take(EquipmentComponent::create(
        id,
        EquipmentComponentKind::ImagingDevice,
        {std::move(input)}));
}

void testPositiveOpticalValuesAreFiniteAndTyped() {
    static_assert(!std::is_convertible<
        PositiveMicrometers, NominalMagnification>::value,
        "length values must not convert to magnification");
    static_assert(!std::is_convertible<
        NominalMagnification, NumericalAperture>::value,
        "magnification must not convert to numerical aperture");

    auto zeroPitch = PositiveMicrometers::create(0.0);
    check(!zeroPitch &&
          zeroPitch.error() == PositiveFiniteValueError::NotPositive,
          "rejects zero sensor pixel pitch");

    auto negativeWorkingDistance = PositiveMicrometers::create(-10.0);
    check(!negativeWorkingDistance &&
          negativeWorkingDistance.error() == PositiveFiniteValueError::NotPositive,
          "rejects negative working distance");

    auto nanMagnification =
        NominalMagnification::create(std::numeric_limits<double>::quiet_NaN());
    check(!nanMagnification &&
          nanMagnification.error() == PositiveFiniteValueError::NotFinite,
          "rejects NaN magnification");

    auto infiniteAperture =
        NumericalAperture::create(std::numeric_limits<double>::infinity());
    check(!infiniteAperture &&
          infiniteAperture.error() == PositiveFiniteValueError::NotFinite,
          "rejects infinite numerical aperture");

    auto validAperture = NumericalAperture::create(1.49);
    check(static_cast<bool>(validAperture),
          "accepts numerical aperture above one without an artificial upper bound");
}

void testObjectiveSpecification() {
    const auto componentId =
        makeId<EquipmentComponentId>("objective-60x-oil");

    auto specification = objectiveSpecification(componentId);
    check(specification.componentId() == componentId,
          "objective specification uses the physical component identity");
    check(specification.nominalMagnification() == 60.0,
          "preserves nominal objective magnification");
    check(specification.numericalAperture() == 1.49,
          "preserves numerical aperture without truncating values above one");
    check(specification.workingDistanceMicrometers() == 130.0,
          "preserves working distance with explicit micrometer units");
    check(specification.chromaticCorrection() ==
              ObjectiveChromaticCorrection::Apochromatic &&
          specification.fieldCorrection() == ObjectiveFieldCorrection::Plan &&
          specification.immersionMedium() == ObjectiveImmersionMedium::Oil,
          "preserves objective optical correction and immersion characteristics");
    check(specification.designCoverglassThickness().has_value() &&
          specification.designCoverglassThickness()->value() == 0.17,
          "preserves optional design coverglass thickness");

    auto invalidCorrection = ObjectiveSpecification::create(
        componentId,
        magnification(10.0),
        numericalAperture(0.25),
        micrometers(6'000.0),
        static_cast<ObjectiveChromaticCorrection>(255),
        ObjectiveFieldCorrection::Plan,
        ObjectiveImmersionMedium::Air);
    check(!invalidCorrection &&
          invalidCorrection.error() ==
              ObjectiveSpecificationError::InvalidChromaticCorrection,
          "rejects unknown objective correction enum values");
}

void testCameraOutputMappingUsesSensorAndDeliveredDimensions() {
    const auto cameraId = makeId<EquipmentComponentId>("camera-main");
    const auto camera = cameraSpecification(cameraId);

    check(camera.componentId() == cameraId,
          "camera specification uses the physical imaging component identity");
    check(camera.activeSensorDimensions().width() == 4096 &&
          camera.activeSensorDimensions().height() == 3000,
          "preserves active sensor dimensions");
    check(camera.pixelPitchXMicrometers() == 3.45 &&
          camera.pixelPitchYMicrometers() == 3.45,
          "preserves independent X/Y sensor pixel pitches");
    check(camera.outputModes().size() == 2,
          "retains multiple readout/output modes");

    const auto& fullFrame = camera.outputModes()[0];
    check(fullFrame.sensorPixelsPerDeliveredPixelX() == 2.0 &&
          fullFrame.sensorPixelsPerDeliveredPixelY() == 2.0,
          "full-frame output records two sensor pixels per delivered pixel");

    const auto& binned = camera.outputModes()[1];
    check(binned.binning().horizontalFactor() == 2 &&
          binned.binning().verticalFactor() == 2,
          "preserves hardware binning independently from output dimensions");
    check(binned.sensorPixelsPerDeliveredPixelX() == 2.0 &&
          binned.sensorPixelsPerDeliveredPixelY() == 2.0,
          "binned mode preserves the actual sensor-to-output mapping");
    check(binned.sensorRegion().x() == 256 &&
          binned.sensorRegion().y() == 100,
          "preserves sensor ROI origin");
}

void testCameraModeAndSensorValidation() {
    auto zeroWidth = PixelDimensions::create(0, 100);
    check(!zeroWidth &&
          zeroWidth.error() == PixelDimensionsError::ZeroWidth,
          "rejects zero sensor/output width");

    auto zeroRoi = SensorRegionOfInterest::create(0, 0, 0, 20);
    check(!zeroRoi && zeroRoi.error() == SensorRegionError::ZeroWidth,
          "rejects zero sensor ROI width");

    auto zeroBinning = PixelBinning::create(1, 0);
    check(!zeroBinning &&
          zeroBinning.error() == PixelBinningError::ZeroVerticalFactor,
          "rejects zero vertical binning");

    auto oversizedBinning = CameraOutputMode::create(
        makeId<CameraModeId>("invalid-binning"),
        roi(0, 0, 4, 4),
        binning(8, 1),
        dimensions(1, 4),
        CameraPixelLayout::Monochrome,
        "Mono8");
    check(!oversizedBinning &&
          oversizedBinning.error() ==
              CameraOutputModeError::BinningExceedsSensorRegion,
          "rejects binning factors larger than the sensor ROI");

    auto invalidPixelLayout = CameraOutputMode::create(
        makeId<CameraModeId>("invalid-layout"),
        roi(0, 0, 4, 4),
        binning(1, 1),
        dimensions(4, 4),
        static_cast<CameraPixelLayout>(255),
        "Mono8");
    check(!invalidPixelLayout &&
          invalidPixelLayout.error() ==
              CameraOutputModeError::InvalidPixelLayout,
          "rejects an unknown pixel layout");

    auto invalidFormatCode = CameraOutputMode::create(
        makeId<CameraModeId>("invalid-format"),
        roi(0, 0, 4, 4),
        binning(1, 1),
        dimensions(4, 4),
        CameraPixelLayout::Monochrome,
        "Mono 8");
    check(!invalidFormatCode &&
          invalidFormatCode.error() ==
              CameraOutputModeError::InvalidPixelFormatCode,
          "requires a stable whitespace-free pixel format code");

    const auto cameraId = makeId<EquipmentComponentId>("camera-invalid-region");
    const auto outsideSensor = CameraSpecification::create(
        cameraId,
        dimensions(100, 100),
        micrometers(3.45),
        micrometers(3.45),
        {outputMode(
            "outside-roi",
            roi(90, 90, 20, 20),
            binning(1, 1),
            dimensions(20, 20))});
    check(!outsideSensor &&
          outsideSensor.error() ==
              CameraSpecificationError::SensorRegionOutsideActiveArea,
          "rejects an ROI outside the active sensor area");

    const auto noModes = CameraSpecification::create(
        cameraId,
        dimensions(100, 100),
        micrometers(3.45),
        micrometers(3.45),
        {});
    check(!noModes &&
          noModes.error() == CameraSpecificationError::NoOutputModes,
          "requires at least one supported camera output mode");
}

void testEquipmentAggregateValidatesSpecializedComponentReferences() {
    const auto microscopeId = makeId<MicroscopeId>("microscope-specs");
    const auto objectiveId =
        makeId<EquipmentComponentId>("objective-specs");
    const auto cameraId =
        makeId<EquipmentComponentId>("camera-specs");

    const std::vector<EquipmentComponent> components{
        makeObjectiveComponent(objectiveId),
        makeCameraComponent(cameraId)};

    const auto objective = objectiveSpecification(objectiveId);
    const auto camera = cameraSpecification(cameraId);

    auto validDefinition = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {objective},
        {camera});
    check(static_cast<bool>(validDefinition),
          "accepts objective and camera specifications linked to matching installed components");
    if (validDefinition) {
        check(validDefinition.value().objectiveSpecifications().size() == 1 &&
              validDefinition.value().cameraSpecifications().size() == 1,
              "equipment definition exposes specialized records without duplicating component identity");
    }

    const auto unknownObjective = ObjectiveSpecification::create(
        makeId<EquipmentComponentId>("objective-not-installed"),
        magnification(4.0),
        numericalAperture(0.10),
        micrometers(30.0),
        ObjectiveChromaticCorrection::Achromatic,
        ObjectiveFieldCorrection::Plan,
        ObjectiveImmersionMedium::Air);
    auto unknownObjectiveDefinition = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {take(std::move(unknownObjective))},
        {camera});
    check(!unknownObjectiveDefinition &&
          unknownObjectiveDefinition.error() ==
              EquipmentDefinitionError::ObjectiveReferencesUnknownComponent,
          "rejects an objective specification for an uninstalled component");

    auto cameraOnObjective = CameraSpecification::create(
        objectiveId,
        dimensions(100, 100),
        micrometers(3.0),
        micrometers(3.0),
        {outputMode(
            "objective-wrong-camera",
            roi(0, 0, 100, 100),
            binning(1, 1),
            dimensions(100, 100))});
    auto wrongCameraKindDefinition = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {objective},
        {take(std::move(cameraOnObjective))});
    check(!wrongCameraKindDefinition &&
          wrongCameraKindDefinition.error() ==
              EquipmentDefinitionError::CameraComponentWrongKind,
          "requires camera specifications to reference an imaging component");

    auto objectiveOnCamera = ObjectiveSpecification::create(
        cameraId,
        magnification(4.0),
        numericalAperture(0.10),
        micrometers(30.0),
        ObjectiveChromaticCorrection::Achromatic,
        ObjectiveFieldCorrection::Plan,
        ObjectiveImmersionMedium::Air);
    auto wrongObjectiveKindDefinition = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {take(std::move(objectiveOnCamera))},
        {camera});
    check(!wrongObjectiveKindDefinition &&
          wrongObjectiveKindDefinition.error() ==
              EquipmentDefinitionError::ObjectiveComponentWrongKind,
          "requires objective specifications to reference an optical element");
}

} // namespace

int main() {
    testPositiveOpticalValuesAreFiniteAndTyped();
    testObjectiveSpecification();
    testCameraOutputMappingUsesSensorAndDeliveredDimensions();
    testCameraModeAndSensorValidation();
    testEquipmentAggregateValidatesSpecializedComponentReferences();

    if (failures != 0) {
        std::cerr << failures << " optical specification contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All optical specification contract tests passed.\n";
    return 0;
}
