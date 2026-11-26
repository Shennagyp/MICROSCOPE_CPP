#include "microscope/core/configuration/CameraSuitability.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
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
        throw std::runtime_error("A camera suitability test fixture factory rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

bool hasDiagnosticCode(
    const CameraSuitabilityReport& report,
    std::string_view code) {
    for (const auto& diagnostic : report.warnings) {
        if (diagnostic.code() == std::string(code)) {
            return true;
        }
    }
    return false;
}

CameraSpecification makeCamera(
    const char* componentId,
    CameraShutterType shutter,
    double qe450,
    double qe550,
    double qe650,
    double readNoise,
    double darkCurrent,
    double fullWell) {
    const auto outputMode = take(CameraOutputMode::create(
        makeId<CameraModeId>((std::string(componentId) + "-mode").c_str()),
        take(SensorRegionOfInterest::create(0, 0, 1024, 1024)),
        take(PixelBinning::create(1, 1)),
        take(PixelDimensions::create(1024, 1024)),
        CameraPixelLayout::Monochrome,
        "Mono16"));

    CameraSensorCharacteristics sensor;
    sensor.technology = CameraSensorTechnology::ScientificCMOS;
    sensor.shutterType = shutter;
    sensor.colorMode = CameraSensorColorMode::Monochrome;
    sensor.readNoiseElectronsRms = readNoise;
    sensor.darkCurrentElectronsPerPixelPerSecond = darkCurrent;
    sensor.fullWellCapacityElectrons = fullWell;
    sensor.maximumFrameRateFramesPerSecond = 100.0;
    sensor.quantumEfficiency = {
        {400.0, qe450 * 0.8},
        {450.0, qe450},
        {550.0, qe550},
        {650.0, qe650},
        {700.0, qe650 * 0.9}};
    sensor.provenance = CameraSpecificationProvenance::ManufacturerDatasheet;

    return take(CameraSpecification::create(
        makeId<EquipmentComponentId>(componentId),
        take(PixelDimensions::create(1024, 1024)),
        take(PositiveMicrometers::create(6.5)),
        take(PositiveMicrometers::create(6.5)),
        {outputMode},
        std::nullopt,
        sensor));
}

FluorescenceCameraRequirements requirements() {
    return FluorescenceCameraRequirements{
        take(SpectralBand::create(
            take(PositiveNanometers::create(500.0)),
            take(PositiveNanometers::create(600.0)))),
        0.75,
        2.0,
        0.1,
        20000.0,
        30.0,
        true,
        true};
}

void testFluorescenceSuitabilityReturnsWarningsWithoutRejectingCamera() {
    const auto camera = makeCamera(
        "camera-low-qe",
        CameraShutterType::Rolling,
        0.35, 0.40, 0.30,
        5.0, 0.5, 10000.0);
    const auto report = CameraSuitabilityAssessor::assessFluorescence(
        camera, requirements());

    check(report.hasWarnings(),
          "returns diagnostics for a camera below fluorescence preferences");
    check(report.knownPreferenceFailures >= 4,
          "reports weak QE, read noise, dark current and full-well shortfalls");
    check(report.meanQuantumEfficiencyInEmissionBand.has_value(),
          "computes QE within the requested emission band");
    check(std::any_of(
              report.warnings.begin(), report.warnings.end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "camera.fluorescence.qe_below_preference";
              }),
          "uses a stable diagnostic code for low QE");

    // The assessor returns a report for the selected camera. No Result failure
    // or automatic camera substitution occurs because preferences were missed.
    check(camera.componentId().value() == "camera-low-qe",
          "manual selection remains the caller's camera choice despite warnings");
}

void testUnknownQuantumEfficiencyProducesWarningNotFailure() {
    const auto camera = makeCamera(
        "camera-no-qe",
        CameraShutterType::Global,
        0.0, 0.0, 0.0,
        1.0, 0.01, 30000.0);

    auto noQe = camera.sensorCharacteristics().value();
    noQe.quantumEfficiency.clear();
    const auto cameraWithoutQe = take(CameraSpecification::create(
        camera.componentId(),
        camera.activeSensorDimensions(),
        take(PositiveMicrometers::create(camera.pixelPitchXMicrometers())),
        take(PositiveMicrometers::create(camera.pixelPitchYMicrometers())),
        camera.outputModes(),
        camera.exposureTimeRange(),
        noQe));

    const auto report = CameraSuitabilityAssessor::assessFluorescence(
        cameraWithoutQe, requirements());
    check(report.meanQuantumEfficiencyInEmissionBand == std::nullopt,
          "reports QE as unknown when the persisted model does not contain a curve");
    check(std::any_of(
              report.warnings.begin(), report.warnings.end(),
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code() ==
                      "camera.fluorescence.qe_unknown_in_emission_band";
              }),
          "warns when the emission band is not characterized");
}

void testFluorescenceQeIsWeightedByEmissionAndDetectionFilters() {
    const auto camera = makeCamera(
        "camera-weighted-qe",
        CameraShutterType::Rolling,
        0.85, 0.90, 0.78,
        1.2, 0.02, 30000.0);

    auto request = requirements();
    request.emissionSpectrum = take(FluorescenceEmissionSpectrum::create({
        {take(PositiveNanometers::create(500.0)), 0.1},
        {take(PositiveNanometers::create(550.0)), 1.0},
        {take(PositiveNanometers::create(600.0)), 0.2}}));
    const auto matchingFilterSet =
        makeId<OpticalCompatibilityId>("fluorescence-cube-matched");

    request.dichroic = take(OpticalFilterSpecification::create(
        makeId<EquipmentComponentId>("weighted-qe-dichroic"),
        OpticalFilterRole::Dichroic,
        {{take(PositiveNanometers::create(500.0)), 0.90, 0.05},
         {take(PositiveNanometers::create(550.0)), 0.95, 0.03},
         {take(PositiveNanometers::create(600.0)), 0.80, 0.15}},
        matchingFilterSet));

    request.emissionFilter = take(OpticalFilterSpecification::create(
        makeId<EquipmentComponentId>("weighted-qe-emission-filter"),
        OpticalFilterRole::Emission,
        {{take(PositiveNanometers::create(500.0)), 0.05, std::nullopt},
         {take(PositiveNanometers::create(550.0)), 0.80, std::nullopt},
         {take(PositiveNanometers::create(600.0)), 0.90, std::nullopt}},
        matchingFilterSet));

    const auto report =
        CameraSuitabilityAssessor::assessFluorescence(camera, request);

    check(report.completeSpectralWeighting,
          "marks QE complete only when emission, dichroic, emission-filter and sensor curves cover the requested band");
    check(report.meanQuantumEfficiencyInEmissionBand.has_value(),
          "calculates the detected-spectrum weighted QE");
    check(report.meanQuantumEfficiencyInEmissionBand &&
          *report.meanQuantumEfficiencyInEmissionBand > 0.88,
          "weights QE toward wavelengths where the fluorophore emits and the detection path transmits");
    check(!hasDiagnosticCode(
              report,
              "camera.fluorescence.qe_unweighted_band_fallback"),
          "does not report the coarse unweighted fallback when full spectral inputs are supplied");

    auto mismatchedSetRequest = request;
    mismatchedSetRequest.emissionFilter = take(OpticalFilterSpecification::create(
        makeId<EquipmentComponentId>("wrong-cube-emission-filter"),
        OpticalFilterRole::Emission,
        {{take(PositiveNanometers::create(500.0)), 0.05, std::nullopt},
         {take(PositiveNanometers::create(550.0)), 0.80, std::nullopt},
         {take(PositiveNanometers::create(600.0)), 0.90, std::nullopt}},
        makeId<OpticalCompatibilityId>("different-fluorescence-cube")));
    const auto mismatchedSetReport =
        CameraSuitabilityAssessor::assessFluorescence(camera, mismatchedSetRequest);
    check(!mismatchedSetReport.completeSpectralWeighting &&
          hasDiagnosticCode(
              mismatchedSetReport,
              "camera.fluorescence.detection_filter_set_mismatch"),
          "does not combine dichroic and emission filters from different compatibility sets");
}

void testIncompleteDetectionSpectraRemainUnknown() {
    const auto camera = makeCamera(
        "camera-incomplete-spectral-coverage",
        CameraShutterType::Rolling,
        0.85, 0.90, 0.78,
        1.2, 0.02, 30000.0);

    auto request = requirements();
    request.emissionSpectrum = take(FluorescenceEmissionSpectrum::create({
        {take(PositiveNanometers::create(520.0)), 0.2},
        {take(PositiveNanometers::create(580.0)), 1.0}}));
    request.dichroic = take(OpticalFilterSpecification::create(
        makeId<EquipmentComponentId>("partial-dichroic"),
        OpticalFilterRole::Dichroic,
        {{take(PositiveNanometers::create(520.0)), 0.9, 0.05},
         {take(PositiveNanometers::create(580.0)), 0.9, 0.05}}));
    request.emissionFilter = take(OpticalFilterSpecification::create(
        makeId<EquipmentComponentId>("partial-emission-filter"),
        OpticalFilterRole::Emission,
        {{take(PositiveNanometers::create(520.0)), 0.8, std::nullopt},
         {take(PositiveNanometers::create(580.0)), 0.8, std::nullopt}}));

    const auto report =
        CameraSuitabilityAssessor::assessFluorescence(camera, request);
    check(!report.completeSpectralWeighting,
          "does not claim a complete weighted QE estimate when the curves miss band edges");
    check(report.unknownPreferenceData > 0,
          "records incomplete spectral coverage as unknown evidence");
    check(hasDiagnosticCode(
              report,
              "camera.fluorescence.weighted_qe_coverage_incomplete"),
          "explains which spectral coverage evidence is missing");
}

void testGlobalShutterIsOnlyTheFinalTieBreaker() {
    const auto rolling = makeCamera(
        "camera-rolling",
        CameraShutterType::Rolling,
        0.85, 0.90, 0.78,
        1.2, 0.02, 30000.0);
    const auto global = makeCamera(
        "camera-global",
        CameraShutterType::Global,
        0.85, 0.90, 0.78,
        1.2, 0.02, 30000.0);

    const auto request = requirements();
    const auto rollingReport =
        CameraSuitabilityAssessor::assessFluorescence(rolling, request);
    const auto globalReport =
        CameraSuitabilityAssessor::assessFluorescence(global, request);

    check(CameraSuitabilityAssessor::shouldRecommendBefore(
              global, globalReport, rolling, rollingReport, request),
          "recommends global shutter when the other measured suitability metrics are equal");

    const auto betterSensitivity = makeCamera(
        "camera-better-qe-rolling",
        CameraShutterType::Rolling,
        0.90, 0.95, 0.85,
        0.8, 0.01, 35000.0);
    const auto betterReport =
        CameraSuitabilityAssessor::assessFluorescence(betterSensitivity, request);

    check(CameraSuitabilityAssessor::shouldRecommendBefore(
              betterSensitivity, betterReport, global, globalReport, request),
          "prefers better fluorescence detector performance over global shutter");
}

} // namespace

int main() {
    testFluorescenceSuitabilityReturnsWarningsWithoutRejectingCamera();
    testUnknownQuantumEfficiencyProducesWarningNotFailure();
    testFluorescenceQeIsWeightedByEmissionAndDetectionFilters();
    testIncompleteDetectionSpectraRemainUnknown();
    testGlobalShutterIsOnlyTheFinalTieBreaker();

    if (failures != 0) {
        std::cerr << failures << " camera suitability contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All camera suitability contract tests passed.\n";
    return 0;
}
