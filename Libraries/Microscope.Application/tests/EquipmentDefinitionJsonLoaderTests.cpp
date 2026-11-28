#include "microscope/application/configuration/EquipmentDefinitionJsonLoader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace microscope::core;
using namespace microscope::application::configuration;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const std::string& validDocument() {
    static const std::string document = R"JSON(
{
  "schemaVersion": 1,
  "microscopeId": "optika-b293",
  "revision": 12,
  "components": [
    {
      "id": "led-main",
      "kind": "illumination-device",
      "ports": [
        { "id": "led-output", "direction": "output" }
      ]
    },
    {
      "id": "condenser-abbe",
      "kind": "optical-element",
      "ports": [
        { "id": "condenser-input", "direction": "input" },
        { "id": "condenser-output", "direction": "output" }
      ]
    },
    {
      "id": "objective-4x",
      "kind": "optical-element",
      "ports": [
        { "id": "objective-input", "direction": "input" },
        { "id": "objective-output", "direction": "output" }
      ]
    },
    {
      "id": "camera-main",
      "kind": "imaging-device",
      "ports": [
        { "id": "camera-input", "direction": "input" }
      ]
    },
    {
      "id": "filter-excitation",
      "kind": "optical-element",
      "ports": [
        { "id": "filter-input", "direction": "input" },
        { "id": "filter-output", "direction": "output" }
      ],
      "opticalFunctions": [
        {
          "function": "excitation-filter",
          "compatibilityId": "filter-family-dapi"
        }
      ]
    },
    {
      "id": "tube-lens",
      "kind": "optical-element",
      "ports": [
        { "id": "tube-lens-input", "direction": "input" },
        { "id": "tube-lens-output", "direction": "output" }
      ]
    },
    {
      "id": "stage-main",
      "kind": "mechanical-assembly"
    }
  ],
  "connections": [
    {
      "id": "led-to-condenser",
      "firstPortId": "led-output",
      "secondPortId": "condenser-input"
    },
    {
      "id": "objective-to-camera",
      "firstPortId": "objective-output",
      "secondPortId": "camera-input"
    }
  ],
  "opticalPaths": [
    {
      "id": "transmitted-illumination",
      "role": "illumination",
      "steps": [
        {
          "componentId": "led-main",
          "entryPortId": null,
          "exitPortId": "led-output"
        },
        {
          "componentId": "condenser-abbe",
          "entryPortId": "condenser-input",
          "exitPortId": null
        }
      ]
    },
    {
      "id": "transmitted-imaging",
      "role": "imaging",
      "steps": [
        {
          "componentId": "objective-4x",
          "entryPortId": null,
          "exitPortId": "objective-output"
        },
        {
          "componentId": "camera-main",
          "entryPortId": "camera-input",
          "exitPortId": null
        }
      ]
    }
  ],
  "lightPaths": [
    {
      "id": "brightfield-transmitted",
      "name": "Transmitted brightfield",
      "opticalPathIds": [
        "transmitted-imaging",
        "transmitted-illumination"
      ]
    }
  ],
  "objectiveSpecifications": [
    {
      "componentId": "objective-4x",
      "nominalMagnification": 4.0,
      "numericalAperture": 0.10,
      "workingDistanceMicrometers": 18000.0,
      "chromaticCorrection": "achromatic",
      "fieldCorrection": "plan",
      "immersionMedium": "air",
      "designCoverglassThicknessMicrometers": 170.0
    }
  ],
  "cameraSpecifications": [
    {
      "componentId": "camera-main",
      "activeSensorDimensions": { "width": 1280, "height": 960 },
      "pixelPitchXMicrometers": 2.2,
      "pixelPitchYMicrometers": 2.2,
      "outputModes": [
        {
          "id": "bayer-640x480",
          "sensorRegion": { "x": 0, "y": 0, "width": 1280, "height": 960 },
          "binning": { "horizontalFactor": 2, "verticalFactor": 2 },
          "deliveredDimensions": { "width": 640, "height": 480 },
          "pixelLayout": "bayer",
          "pixelFormatCode": "BayerRG8"
        }
      ],
      "exposureTimeRange": {
        "minimumMicroseconds": 50.0,
        "maximumMicroseconds": 1000000.0
      },
      "sensorCharacteristics": {
        "technology": "cmos",
        "shutterType": "rolling",
        "colorMode": "color",
        "readNoiseElectronsRms": 2.1,
        "darkCurrentElectronsPerPixelPerSecond": 0.02,
        "fullWellCapacityElectrons": 12000.0,
        "maximumFrameRateFramesPerSecond": 30.0,
        "quantumEfficiency": [
          { "wavelengthNanometers": 450.0, "quantumEfficiency": 0.62 },
          { "wavelengthNanometers": 550.0, "quantumEfficiency": 0.81 },
          { "wavelengthNanometers": 650.0, "quantumEfficiency": 0.67 }
        ],
        "provenance": "user-verified",
        "provenanceReference": "lab-camera-characterization-2026"
      }
    }
  ],
  "condenserSpecifications": [
    {
      "componentId": "condenser-abbe",
      "minimumApertureNA": 0.10,
      "maximumApertureNA": 1.25,
      "design": "abbe",
      "immersionMedium": "air",
      "workingDistanceMicrometers": 30000.0
    }
  ],
  "illuminationSpecifications": [
    {
      "componentId": "led-main",
      "technology": "led",
      "channels": [
        {
          "id": "white-led",
          "spectralBand": {
            "minimumWavelengthNanometers": 420.0,
            "maximumWavelengthNanometers": 700.0
          },
          "controlKind": "continuous",
          "intensityRange": {
            "minimum": 0.0,
            "maximum": 100.0,
            "unit": "percent"
          },
          "spectralPowerDistribution": [
            { "wavelengthNanometers": 420.0, "relativePower": 0.2 },
            { "wavelengthNanometers": 550.0, "relativePower": 1.0 },
            { "wavelengthNanometers": 700.0, "relativePower": 0.1 }
          ]
        }
      ]
    }
  ],
  "stageSpecifications": [
    {
      "componentId": "stage-main",
      "axes": [
        {
          "id": "stage-x",
          "motionType": "linear",
          "coordinateUnit": "micrometers",
          "operatingRange": {
            "minimum": 0.0,
            "maximum": 160000.0,
            "unit": "micrometers"
          },
          "nominalResolution": 0.1,
          "accuracy": {
            "maximumAbsoluteError": 2.0,
            "testConditions": "calibrated over 100 mm travel"
          },
          "repeatability": {
            "value": 0.5,
            "metric": "maximum-deviation",
            "testConditions": "bidirectional approach"
          },
          "measurementUncertainty": {
            "value": 0.2,
            "testConditions": "encoder readback"
          },
          "positionFeedback": "linear-scale",
          "measurementLocation": "axis"
        },
        {
          "id": "stage-y",
          "motionType": "linear",
          "coordinateUnit": "micrometers",
          "operatingRange": {
            "minimum": 0.0,
            "maximum": 90000.0,
            "unit": "micrometers"
          },
          "nominalResolution": 0.1,
          "accuracy": null,
          "repeatability": null,
          "measurementUncertainty": null,
          "positionFeedback": "device-reported",
          "measurementLocation": "axis"
        }
      ]
    }
  ],
  "dualSlideCarrierSpecifications": [
    {
      "stageComponentId": "stage-main",
      "horizontalAxisId": "stage-x",
      "transverseAxisId": "stage-y",
      "direction": "positive-stage-x-left-to-right"
    }
  ],
  "filterSpecifications": [
    {
      "componentId": "filter-excitation",
      "role": "excitation",
      "compatibilityId": "filter-family-dapi",
      "spectralResponse": [
        {
          "wavelengthNanometers": 430.0,
          "transmissionFraction": 0.82
        },
        {
          "wavelengthNanometers": 450.0,
          "transmissionFraction": 0.91
        },
        {
          "wavelengthNanometers": 480.0,
          "transmissionFraction": 0.05
        }
      ]
    }
  ],
  "opticalRelaySpecifications": [
    {
      "componentId": "tube-lens",
      "role": "tube-lens",
      "magnificationFactor": 1.0
    }
  ]
}
)JSON";
    return document;
}

std::string replaceOnce(
    std::string source,
    const std::string& before,
    const std::string& after) {
    const auto position = source.find(before);
    if (position == std::string::npos) {
        throw std::runtime_error("Equipment JSON test fixture replacement anchor was not found.");
    }
    source.replace(position, before.size(), after);
    return source;
}

std::string validV2Document() {
    auto document=validDocument();
    document=replaceOnce(document,"\"schemaVersion\": 1,","\"schemaVersion\": 2,");
    const auto stageStart=document.find("  \"stageSpecifications\": [");
    const auto carrierStart=document.find("  \"dualSlideCarrierSpecifications\": [",stageStart);
    if(stageStart==std::string::npos||carrierStart==std::string::npos)
        throw std::runtime_error("Schema v2 stage/carrier fixture anchors were not found.");

    std::string stageBlock=R"JSON(  "stageSpecifications": [
    {
      "componentId": "stage-main",
      "axes": [
        {
          "id": "stage-x", "motionType": "linear", "coordinateUnit": "micrometers",
          "operatingRange": {"minimum": 0.0, "maximum": 160000.0, "unit": "micrometers"},
          "nominalResolution": 0.1, "positionFeedback": "linear-scale",
          "measurementLocation": "axis", "linearRole": "x"
        },
        {
          "id": "stage-y", "motionType": "linear", "coordinateUnit": "micrometers",
          "operatingRange": {"minimum": 0.0, "maximum": 90000.0, "unit": "micrometers"},
          "nominalResolution": 0.1, "positionFeedback": "device-reported",
          "measurementLocation": "axis", "linearRole": "y"
        },
        {
          "id": "stage-z", "motionType": "linear", "coordinateUnit": "micrometers",
          "operatingRange": {"minimum": 0.0, "maximum": 100000.0, "unit": "micrometers"},
          "nominalResolution": 0.05, "positionFeedback": "encoder",
          "measurementLocation": "axis", "linearRole": "z"
        },
        {
          "id": "stage-theta", "motionType": "rotary", "coordinateUnit": "degrees",
          "operatingRange": {"minimum": -180.0, "maximum": 180.0, "unit": "degrees"},
          "nominalResolution": 0.01, "positionFeedback": "encoder",
          "measurementLocation": "axis",
          "rotationGeometry": {
            "pointOnAxisNanometres": {"x": 0, "y": 0, "z": 15000000},
            "direction": "z"
          }
        }
      ],
      "configurations": [
        {
          "id": "slides",
          "workingLinearAxes": [
            {"axisId": "stage-x", "effectiveRangeNanometres": {"minimum": 0, "maximum": 160000000}},
            {"axisId": "stage-y", "effectiveRangeNanometres": {"minimum": 0, "maximum": 90000000}},
            {"axisId": "stage-z", "effectiveRangeNanometres": {"minimum": 8000000, "maximum": 80000000}}
          ],
          "workingRotationAxes": [],
          "slots": [
)JSON";
    for(int index=0;index<10;++index) {
        if(index!=0) stageBlock+=",\n";
        const auto x=10'000'000+static_cast<std::int64_t>(index)*9'000'000;
        stageBlock+="            {\"id\": \"slide-slot-"+std::to_string(index+1)+
            "\", \"originInStageNanometres\": {\"x\": "+
            std::to_string(x)+", \"y\": 10000000}, \"xRangeNanometres\": "+
            "{\"minimum\": 0, \"maximum\": 8000000}, \"yRangeNanometres\": "+
            "{\"minimum\": 0, \"maximum\": 25000000}}";
    }
    stageBlock+=R"JSON(
          ]
        },
        {
          "id": "petri",
          "workingLinearAxes": [
            {"axisId": "stage-x", "effectiveRangeNanometres": {"minimum": 0, "maximum": 160000000}},
            {"axisId": "stage-y", "effectiveRangeNanometres": {"minimum": 0, "maximum": 90000000}},
            {"axisId": "stage-z", "effectiveRangeNanometres": {"minimum": 2000000, "maximum": 60000000}}
          ],
          "workingRotationAxes": [
            {"axisId": "stage-theta", "effectiveRangeRadians": {"minimum": -1.5707963267948966, "maximum": 1.5707963267948966}}
          ],
          "slots": [
            {
              "id": "petri-dish-slot",
              "originInStageNanometres": {"x": 0, "y": 0},
              "xRangeNanometres": {"minimum": 0, "maximum": 30000000},
              "yRangeNanometres": {"minimum": 0, "maximum": 30000000}
            }
          ]
        }
      ]
    }
  ],
)JSON";
    document.replace(stageStart,carrierStart-stageStart,stageBlock);
    const auto obsoleteCarrierStart=document.find(
        "  \"dualSlideCarrierSpecifications\": [",stageStart);
    const auto filterStart=document.find("  \"filterSpecifications\": [",obsoleteCarrierStart);
    if(obsoleteCarrierStart==std::string::npos||filterStart==std::string::npos)
        throw std::runtime_error("Schema v2 legacy carrier block anchors were not found.");
    document.erase(obsoleteCarrierStart,filterStart-obsoleteCarrierStart);
    return document;
}

void parsesCompleteStaticEquipmentDefinition() {
    const auto parsed = EquipmentDefinitionJsonLoader::parse(validDocument());
    check(static_cast<bool>(parsed),
        "parses the complete versioned equipment-definition document");
    if (!parsed) {
        std::cerr << "Loader error at " << parsed.error().path << ": "
                  << parsed.error().detail << '\n';
        return;
    }

    const auto& equipment = parsed.value();
    check(equipment.microscopeId().value() == "optika-b293" &&
        equipment.revision() == 12,
        "restores microscope identity and equipment-definition revision");

    check(equipment.components().size() == 7 &&
        equipment.connections().size() == 2 &&
        equipment.opticalPaths().size() == 2 &&
        equipment.lightPaths().size() == 1,
        "restores the physical graph, undirected connections, ordered optical routes and selectable LightPath");

    check(equipment.objectiveSpecifications().size() == 1 &&
        equipment.objectiveSpecifications()[0].nominalMagnification() == 4.0 &&
        equipment.objectiveSpecifications()[0].numericalAperture() == 0.10 &&
        equipment.objectiveSpecifications()[0].workingDistanceMicrometers() == 18000.0 &&
        equipment.objectiveSpecifications()[0].designCoverglassThickness() &&
        equipment.objectiveSpecifications()[0].designCoverglassThickness()->value() == 170.0,
        "restores objective magnification, NA, working distance and coverglass design");

    check(equipment.cameraSpecifications().size() == 1,
        "restores one camera sensor specification");
    if (!equipment.cameraSpecifications().empty()) {
        const auto& camera = equipment.cameraSpecifications()[0];
        check(camera.activeSensorDimensions().width() == 1280 &&
            camera.activeSensorDimensions().height() == 960 &&
            camera.pixelPitchXMicrometers() == 2.2 &&
            camera.outputModes().size() == 1,
            "restores active sensor geometry and pixel pitch");
        const auto& mode = camera.outputModes()[0];
        check(mode.sensorRegion().width() == 1280 &&
            mode.sensorRegion().height() == 960 &&
            mode.binning().horizontalFactor() == 2 &&
            mode.deliveredDimensions().width() == 640 &&
            mode.pixelLayout() == CameraPixelLayout::Bayer &&
            mode.pixelFormatCode() == "BayerRG8",
            "restores sensor ROI, binning and delivered image mode without conflating sensor pixels and image pixels");
        check(camera.exposureTimeRange() &&
            camera.exposureTimeRange()->maximumMicroseconds() == 1000000.0,
            "restores the camera exposure-time capability range");
        check(camera.sensorCharacteristics() &&
            camera.sensorCharacteristics()->technology == CameraSensorTechnology::CMOS &&
            camera.sensorCharacteristics()->shutterType == CameraShutterType::Rolling &&
            camera.sensorCharacteristics()->quantumEfficiency.size() == 3 &&
            camera.sensorCharacteristics()->provenance == CameraSpecificationProvenance::UserVerified &&
            camera.sensorCharacteristics()->provenanceReference ==
                std::optional<std::string>("lab-camera-characterization-2026"),
            "restores camera physical characteristics, QE samples and their provenance");
    }

    check(equipment.condenserSpecifications().size() == 1 &&
        equipment.condenserSpecifications()[0].minimumApertureNA() == 0.10 &&
        equipment.condenserSpecifications()[0].maximumApertureNA() == 1.25 &&
        equipment.condenserSpecifications()[0].design() == CondenserDesign::Abbe,
        "restores condenser design, aperture range and immersion model");

    check(equipment.illuminationSpecifications().size() == 1,
        "restores one illumination-source specification");
    if (!equipment.illuminationSpecifications().empty()) {
        const auto& illumination = equipment.illuminationSpecifications()[0];
        check(illumination.technology() == IlluminationSourceTechnology::Led &&
            illumination.channels().size() == 1,
            "restores illumination technology and channel identity");
        const auto& channel = illumination.channels()[0];
        check(channel.id().value() == "white-led" &&
            channel.spectralBand() &&
            channel.spectralBand()->minimumWavelengthNanometers() == 420.0 &&
            channel.intensityRange() &&
            channel.intensityRange()->unit() == IlluminationIntensityUnit::Percent &&
            channel.spectralPowerDistribution() &&
            channel.spectralPowerDistribution()->samples().size() == 3,
            "restores spectral band, channel intensity unit and spectral-power distribution");
    }

    check(equipment.stageSpecifications().size() == 1 &&
        equipment.stageSpecifications()[0].axes().size() == 2,
        "restores the mechanical stage and its separately modelled axes");
    if (!equipment.stageSpecifications().empty()) {
        const auto& axes = equipment.stageSpecifications()[0].axes();
        check(axes[0].id().value() == "stage-x" &&
            axes[0].coordinateUnit() == AxisCoordinateUnit::Micrometers &&
            axes[0].operatingRange().maximum() == 160000.0 &&
            axes[0].accuracy() && axes[0].repeatability() &&
            axes[0].measurementUncertainty() &&
            axes[0].positionFeedback() == AxisPositionFeedbackCapability::LinearScale &&
            axes[0].measurementLocation() == AxisMeasurementLocation::Axis,
            "restores independent axis range, resolution, accuracy, repeatability, uncertainty and feedback");
        check(!axes[1].accuracy() && !axes[1].repeatability() &&
            !axes[1].measurementUncertainty() &&
            axes[1].operatingRange().maximum() == 90000.0,
            "preserves absent optional axis-performance facts instead of inferring them");
    }

    check(equipment.dualSlideCarrierSpecifications().size() == 1 &&
        equipment.dualSlideCarrierSpecifications()[0].stageComponentId().value() == "stage-main" &&
        equipment.dualSlideCarrierSpecifications()[0].horizontalAxisId().value() == "stage-x" &&
        equipment.dualSlideCarrierSpecifications()[0].transverseAxisId().value() == "stage-y" &&
        equipment.dualSlideCarrierSpecifications()[0].horizontalDirection() ==
            CarrierHorizontalDirection::PositiveStageXFromLeftToRight,
        "restores the two-horizontal-slide carrier's physical stage/axis binding");

    check(equipment.filterSpecifications().size() == 1 &&
        equipment.filterSpecifications()[0].spectralResponse().size() == 3 &&
        equipment.filterSpecifications()[0].compatibilityId() &&
        equipment.filterSpecifications()[0].compatibilityId()->value() == "filter-family-dapi",
        "restores filter spectral response and compatibility family");

    check(equipment.opticalRelaySpecifications().size() == 1 &&
        equipment.opticalRelaySpecifications()[0].role() == OpticalRelayRole::TubeLens &&
        equipment.opticalRelaySpecifications()[0].magnificationFactor() &&
        equipment.opticalRelaySpecifications()[0].magnificationFactor()->value() == 1.0,
        "restores relay role and calibrated magnification factor");
}

void parsesSchemaV2GenericStageConfigurations() {
    const auto parsed=EquipmentDefinitionJsonLoader::parse(validV2Document());
    check(static_cast<bool>(parsed),
        "parses schema v2 equipment definitions with generic Stage configurations");
    if(!parsed) {
        std::cerr<<"Schema v2 loader error at "<<parsed.error().path<<": "
                 <<parsed.error().detail<<'\n';
        return;
    }
    const auto& stages=parsed.value().stageSpecifications();
    check(stages.size()==1&&stages[0].configurations().size()==2,
        "retains both equipment-defined working configurations in Core");
    if(stages.size()==1&&stages[0].configurations().size()==2) {
        check(stages[0].configurations()[0].slots().size()==10,
            "persists a generic ten-slot slide holder through equipment JSON");
        check(stages[0].configurations()[1].slots().size()==1,
            "persists a one-slot Petri holder through equipment JSON");
        check(stages[0].configurations()[0].workingRotationAxes().empty()&&
              stages[0].configurations()[1].workingRotationAxes().size()==1,
            "persists configuration-specific optional rotary axes");
        const auto* x=stages[0].linearAxis(StageLinearAxisRole::X);
        check(x&&x->linearRole()==StageLinearAxisRole::X,
            "retains the physical X role on the installed axis");
    }

    auto legacyCarrierField=replaceOnce(validV2Document(),
        "  \"filterSpecifications\": [",
        "  \"dualSlideCarrierSpecifications\": [],\\n  \"filterSpecifications\": [");
    const auto obsolete=EquipmentDefinitionJsonLoader::parse(legacyCarrierField);
    check(!obsolete&&obsolete.error().code==EquipmentDefinitionJsonErrorCode::UnknownField,
        "schema v2 rejects the retired fixed dual-slide carrier specification field");
}

void rejectsMalformedOrAmbiguousJsonDocuments() {
    auto malformed = EquipmentDefinitionJsonLoader::parse("{ \"schemaVersion\": 1,");
    check(!malformed &&
        malformed.error().code == EquipmentDefinitionJsonErrorCode::JsonSyntaxError,
        "rejects malformed JSON syntax");

    auto duplicateKeyDocument = replaceOnce(
        validDocument(),
        "\"schemaVersion\": 1,",
        "\"schemaVersion\": 1, \"schemaVersion\": 1,");
    auto duplicateKey = EquipmentDefinitionJsonLoader::parse(duplicateKeyDocument);
    check(!duplicateKey &&
        duplicateKey.error().code == EquipmentDefinitionJsonErrorCode::JsonSyntaxError,
        "rejects duplicate object keys rather than silently accepting a parser-dependent value");

    auto unknownFieldDocument = replaceOnce(
        validDocument(),
        "\"revision\": 12,",
        "\"revision\": 12, \"runtimePosition\": 123.0,");
    auto unknownField = EquipmentDefinitionJsonLoader::parse(unknownFieldDocument);
    check(!unknownField &&
        unknownField.error().code == EquipmentDefinitionJsonErrorCode::UnknownField,
        "rejects unknown fields so misspellings and accidental runtime-state leakage are not ignored");

    auto unsupportedVersionDocument = replaceOnce(
        validDocument(), "\"schemaVersion\": 1,", "\"schemaVersion\": 3,");
    auto unsupportedVersion = EquipmentDefinitionJsonLoader::parse(unsupportedVersionDocument);
    check(!unsupportedVersion &&
        unsupportedVersion.error().code ==
            EquipmentDefinitionJsonErrorCode::UnsupportedSchemaVersion,
        "rejects an unsupported document schema version");

    std::string invalidUtf8 = validDocument();
    const auto marker = invalidUtf8.find("optika-b293");
    invalidUtf8.insert(marker + 2, 1, static_cast<char>(0xff));
    auto utf8 = EquipmentDefinitionJsonLoader::parse(invalidUtf8);
    check(!utf8 &&
        utf8.error().code == EquipmentDefinitionJsonErrorCode::JsonSyntaxError,
        "rejects invalid UTF-8 in raw JSON string data");

    auto missingCarrierStageDocument = replaceOnce(
        validDocument(),
        "\"stageComponentId\": \"stage-main\"",
        "\"stageComponentId\": \"missing-stage\"");
    auto missingCarrierStage = EquipmentDefinitionJsonLoader::parse(
        missingCarrierStageDocument);
    check(!missingCarrierStage &&
        missingCarrierStage.error().code == EquipmentDefinitionJsonErrorCode::InvalidValue,
        "rejects a dual-slide carrier that references no decoded stage specification");

    auto wrongAxisUnitDocument = replaceOnce(
        validDocument(),
        "\"coordinateUnit\": \"micrometers\"",
        "\"coordinateUnit\": \"degrees\"");
    auto wrongAxisUnit = EquipmentDefinitionJsonLoader::parse(wrongAxisUnitDocument);
    check(!wrongAxisUnit &&
        wrongAxisUnit.error().code == EquipmentDefinitionJsonErrorCode::DomainValidationFailed,
        "delegates motion-type/unit compatibility validation to Core");
}

void handlesEmptyFilesAndFileLoadingErrors() {
    auto empty = EquipmentDefinitionJsonLoader::parse("");
    check(!empty &&
        empty.error().code == EquipmentDefinitionJsonErrorCode::EmptyDocument,
        "rejects an empty in-memory JSON document");

    auto missingFile = EquipmentDefinitionJsonLoader::loadFile(
        "/path/that/does/not/exist/equipment.json");
    check(!missingFile &&
        missingFile.error().code == EquipmentDefinitionJsonErrorCode::FileOpenFailed,
        "reports a missing equipment-definition file as a file-open error");

    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto path = (std::filesystem::temp_directory_path() /
        ("microscope-equipment-definition-" + std::to_string(nonce) + ".json")).string();
    {
        std::ofstream output(path, std::ios::binary);
        output << validDocument();
    }
    auto loaded = EquipmentDefinitionJsonLoader::loadFile(path);
    check(static_cast<bool>(loaded) &&
        loaded.value().microscopeId().value() == "optika-b293",
        "loads and validates an equipment-definition JSON document from a file");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

} // namespace

int main() {
    try {
        parsesCompleteStaticEquipmentDefinition();
        parsesSchemaV2GenericStageConfigurations();
        rejectsMalformedOrAmbiguousJsonDocuments();
        handlesEmptyFilesAndFileLoadingErrors();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) return 1;
    std::cout << "Equipment-definition JSON loader tests passed\n";
    return 0;
}
