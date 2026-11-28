# Microscope Equipment Definition JSON — Stage Schema v2

**Owner:** Microscope.Application configuration boundary  
**Domain model:** Microscope.Core installed equipment specification → Controller runtime Stage  
**Schema version:** 2  
**Status:** Canonical Stage configuration format.

The non-Stage equipment records retain the topology/optical structures documented in EQUIPMENT_DEFINITION_JSON_SCHEMA_V1.md. Schema v2 removes the fixed dual-slide carrier field and adds the generic installed Stage configuration model described below.

## Root Stage field

stageSpecifications is an optional array of installed Stage specifications. Schema v2 does not accept dualSlideCarrierSpecifications.

Each Stage contains:
- componentId
- axes
- configurations

## Installed axes

Each linear axis contains the common physical-axis fields:
- id
- motionType: linear
- coordinateUnit: nanometres
- operatingRange: minimum, maximum and unit nanometres
- nominalResolution in nanometres
- optional accuracy, repeatability and measurementUncertainty, each in nanometres with its own testConditions
- positionFeedback
- optional measurementLocation
- linearRole: x, y or z

Each rotary axis contains:
- id
- motionType: rotary
- coordinateUnit: degrees or radians
- operatingRange and nominalResolution in the declared angular unit
- optional angular accuracy, repeatability and measurementUncertainty in the declared angular unit
- positionFeedback
- optional measurementLocation
- rotationGeometry: pointOnAxisNanometres {x,y,z} and direction x, y or z

Linear axes must use nanometres in schema v2. Rotary axis geometry's point is expressed in global Stage nanometres; its direction is a cardinal unit axis. The Controller runtime normalizes rotary angles to radians.

## StageConfiguration

Each Stage configuration contains:
- id
- workingLinearAxes
- workingRotationAxes
- slots

Every runnable configuration must define working X, Y and Z linear axes. A working-linear-axis item has:
- axisId: reference to an installed linear axis
- effectiveRangeNanometres: minimum and maximum

A working-rotation-axis item has:
- axisId: reference to an installed rotary axis
- effectiveRangeRadians: minimum and maximum

Rotation is optional per configuration. Effective ranges must remain inside the referenced installed axis mechanical travel.

## StageSlot

Each StageConfiguration owns a variable N-element slots array. A StageSlot item contains:
- id
- originInStageNanometres: global {x,y}
- xRangeNanometres: local minimum/maximum
- yRangeNanometres: local minimum/maximum

Slot local ranges are non-negative. The origin is global machine XY. Slot-relative coordinates are translated by adding the slot origin. Each slot rectangle must fit within the active configuration's effective XY envelope. Slot IDs must be unique within the configuration and rectangles may not overlap.

A StageSlot is a physical working region in a holder. It is not a Slide and contains no SlideId, SampleType, patient/sample metadata or acquisition data. The application associates existing Slide records with StageSlot IDs through its separate StageSlotAssignments model.

## Example

~~~json
{
  "schemaVersion": 2,
  "microscopeId": "optika-b293",
  "revision": 2,
  "stageSpecifications": [
    {
      "componentId": "stage-main",
      "axes": [
        {
          "id": "stage-x",
          "motionType": "linear",
          "coordinateUnit": "nanometres",
          "operatingRange": {
            "minimum": 0,
            "maximum": 160000000,
            "unit": "nanometres"
          },
          "nominalResolution": 100,
          "positionFeedback": "linear-scale",
          "linearRole": "x"
        },
        {
          "id": "stage-y",
          "motionType": "linear",
          "coordinateUnit": "nanometres",
          "operatingRange": {
            "minimum": 0,
            "maximum": 90000000,
            "unit": "nanometres"
          },
          "nominalResolution": 100,
          "positionFeedback": "encoder",
          "linearRole": "y"
        },
        {
          "id": "stage-z",
          "motionType": "linear",
          "coordinateUnit": "nanometres",
          "operatingRange": {
            "minimum": 0,
            "maximum": 100000000,
            "unit": "nanometres"
          },
          "nominalResolution": 50,
          "positionFeedback": "encoder",
          "linearRole": "z"
        }
      ],
      "configurations": [
        {
          "id": "slides",
          "workingLinearAxes": [
            {
              "axisId": "stage-x",
              "effectiveRangeNanometres": {
                "minimum": 0,
                "maximum": 160000000
              }
            },
            {
              "axisId": "stage-y",
              "effectiveRangeNanometres": {
                "minimum": 0,
                "maximum": 90000000
              }
            },
            {
              "axisId": "stage-z",
              "effectiveRangeNanometres": {
                "minimum": 8000000,
                "maximum": 80000000
              }
            }
          ],
          "workingRotationAxes": [],
          "slots": [
            {
              "id": "slide-slot-1",
              "originInStageNanometres": {
                "x": 10000000,
                "y": 10000000
              },
              "xRangeNanometres": {
                "minimum": 0,
                "maximum": 75000000
              },
              "yRangeNanometres": {
                "minimum": 0,
                "maximum": 25000000
              }
            }
          ]
        }
      ]
    }
  ]
}
~~~

This example is illustrative equipment data; it is not an assertion about the measured travel or resolution of a particular physical controller.

## Validation rules

The Core equipment factories and JSON loader reject:
- Non-nanometre v2 linear axis units or mismatched operating-range units.
- Axis IDs that are duplicated or missing.
- Working axis references to unknown or wrong-motion-type axes.
- Effective ranges outside installed mechanical travel.
- Missing X, Y or Z working axes in a runnable configuration.
- Invalid or duplicate configuration/slot identities.
- Negative slot-local ranges, arithmetic overflow, out-of-envelope slots or overlapping slots.
- A rotary working-axis declaration without installed rotation geometry and a valid angular range.
- Unknown root/object fields, duplicate JSON keys, malformed values and invalid graph references.

## Runtime projection

Controller::Stage::fromEquipmentSpecification constructs the runtime Stage from one validated Core Stage specification and selected StageConfigurationId. The resulting Stage owns its runtime axis/configuration objects and current position/motion lifecycle. The Core equipment definition remains the immutable installed-hardware specification and is rechecked by StageMotionCommandTranslator before command translation.

Device-native units, motor steps, encoder counts, direction polarity, controller channels, homing procedure and trajectories belong to the physical IStageMotionPort adapter, not the equipment Stage coordinate model.
