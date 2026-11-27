# Microscope Equipment Definition JSON — Schema v1

**Owner:** `Microscope.Application` configuration boundary  
**Domain model:** `Microscope.Core::MicroscopeEquipmentDefinition`  
**Schema version:** `1`

## Purpose and ownership

An equipment-definition document is the authoritative, versioned description of the microscope's **installed static hardware model**. The loader reconstructs a `MicroscopeEquipmentDefinition` through Core value factories and Core graph validation.

It contains physical component identities, ports, connections, ordered optical paths, selectable LightPaths, optical and camera capabilities, illumination characteristics, stage axes, and the dual-slide carrier's stage/axis binding.

It does not contain current device observations, controller sessions, encoder positions, homing state, calibration measurements, camera frames, acquisition operations, slide-folder paths, image/tile persistence, or gallery state. Those have different owners and lifecycles.

The equipment document is not the configuration-profile catalog. A `ConfigurationProfileCatalog` is a separate revisioned compatibility declaration scoped to a particular equipment revision.

## Document envelope

The root is an object with the following fields:

| Field | Type | Requirement | Meaning |
|---|---|---|---|
| `schemaVersion` | integer | required | Must be exactly `1` |
| `microscopeId` | string | required | Stable Core `MicroscopeId` |
| `revision` | positive integer | required | Immutable equipment-definition revision |
| `components` | array | required | Installed physical components |
| `connections` | array | required | Physical port-to-port connections |
| `opticalPaths` | array | required | Ordered optical routes |
| `lightPaths` | array | optional | Named selectable combinations of optical routes |
| `objectiveSpecifications` | array | optional | Objective capability/geometry records |
| `cameraSpecifications` | array | optional | Sensor, pixel mapping, output mode and capability records |
| `condenserSpecifications` | array | optional | Condenser optical capability |
| `illuminationSpecifications` | array | optional | Physical source and channel capabilities |
| `stageSpecifications` | array | optional | Stage component and motion-axis specifications |
| `dualSlideCarrierSpecifications` | array | optional | Binding of a two-slide carrier to a stage and two axes |
| `filterSpecifications` | array | optional | Installed filter spectral response |
| `opticalRelaySpecifications` | array | optional | Tube-lens, relay, adapter or reducer factors |

Optional arrays may be omitted or set to `null`; both mean an empty collection. Required arrays must be present, but may be empty where the Core graph permits it.

Unknown fields are rejected. Duplicate object keys are rejected. Enum values are exact strings, not numeric ordinals. Numbers must be finite JSON numbers. Identifiers are validated by the Core strong-ID factory.

## Physical topology

### Components

Each `components` item has:

| Field | Type | Requirement |
|---|---|---|
| `id` | string | required, unique equipment component identity |
| `kind` | string | required |
| `ports` | array | optional |
| `opticalFunctions` | array | optional |

Component `kind` values:

- `optical-element`
- `imaging-device`
- `illumination-device`
- `mechanical-assembly`
- `other`

Each port has `id` and `direction`. Directions are `input`, `output`, and `bidirectional`.

Core owns port-cardinality rules. For example, an optical element requires input and output ports; an imaging device requires an input; an illumination device requires an output; a mechanical assembly has no optical ports. The loader does not weaken these contracts.

Each optical-function capability has `function` and optional `compatibilityId`. Function strings are:

- `beam-splitter`, `phase-plate`, `phase-annulus`
- `polarizer`, `analyzer`, `differential-interference-prism`
- `excitation-filter`, `emission-filter`, `dichroic-mirror`
- `neutral-density-filter`, `darkfield-stop`, `other`

The optional compatibility identity binds mating optical elements where required; it is not inferred from component names.

### Connections

A connection contains `id`, `firstPortId`, and `secondPortId`. It represents physical adjacency, not optical propagation direction. Endpoint ownership, port direction, duplicate connection, and port reuse validation belong to Core.

### Ordered optical paths

Each path contains `id`, `role`, and `steps`. Role values are `imaging`, `illumination`, and `other`.

Each step contains:

- `componentId` — required
- `entryPortId` — optional/null at the source boundary
- `exitPortId` — optional/null at the terminal boundary

A path's sequence defines propagation order. Connection endpoint ordering does not.

### Selectable LightPaths

A LightPath contains `id`, `name`, and `opticalPathIds`. Core verifies that referenced paths exist and that each selectable LightPath has a valid imaging route and camera resolution. Camera identity is derived from the imaging route; it is not redundantly copied into the LightPath.

## Optical specifications

### Objective

Fields:

- `componentId`
- `nominalMagnification`
- `numericalAperture`
- `workingDistanceMicrometers`
- `chromaticCorrection`
- `fieldCorrection`
- `immersionMedium`
- optional `designCoverglassThicknessMicrometers`

Enum strings:

- chromatic correction: `achromatic`, `semi-apochromatic`, `apochromatic`, `other`, `unknown`
- field correction: `plan`, `non-plan`, `unknown`
- immersion medium: `air`, `water`, `oil`, `glycerol`, `silicone`, `other`, `unknown`

Magnification is dimensionless. NA is dimensionless. Working distance and coverglass thickness are in micrometers.

### Condenser

Fields:

- `componentId`
- `minimumApertureNA`
- `maximumApertureNA`
- `design`
- `immersionMedium`
- optional `workingDistanceMicrometers`

Design values: `abbe`, `achromatic`, `achromat-aplanatic`, `other`, `unknown`.

Immersion values: `air`, `water`, `oil`, `other`, `unknown`.

Core verifies the aperture interval and enum validity.

### Optical relays

Each record contains `componentId`, `role`, and optional `magnificationFactor`.

Roles: `tube-lens`, `intermediate-relay`, `camera-adapter`, `reduction-adapter`, `magnification-changer`, `other`.

The factor is dimensionless. It is not an inferred value from a component label.

### Spectral filters

Each record contains `componentId`, `role`, `spectralResponse`, and optional `compatibilityId`.

Roles: `excitation`, `emission`, `dichroic`, `neutral-density`, `other`.

Each spectral sample contains:

- `wavelengthNanometers`
- `transmissionFraction`
- optional `reflectionFraction`

Wavelengths must be positive and strictly increasing. Transmission/reflection are fractions, not percentages. Core enforces the valid range and dichroic requirements.

## Camera specification

A camera record contains:

- `componentId`
- `activeSensorDimensions` — `width`, `height` in physical sensor pixels
- `pixelPitchXMicrometers`, `pixelPitchYMicrometers`
- `outputModes` — required, non-empty
- optional `exposureTimeRange`
- optional `sensorCharacteristics`

Each output mode contains:

- `id`
- `sensorRegion` — `x`, `y`, `width`, `height` in physical sensor pixels
- `binning` — `horizontalFactor`, `verticalFactor`
- `deliveredDimensions` — `width`, `height` in delivered image pixels
- `pixelLayout` — `monochrome`, `bayer`, `interleaved-color`, `planar-color`, `other`
- `pixelFormatCode` — explicit format string

The distinction between sensor ROI and delivered dimensions is intentional. The loader does not assume one image pixel equals one sensor pixel.

Exposure range fields are `minimumMicroseconds` and `maximumMicroseconds`.

Optional sensor characteristics may contain:

- `technology`: `ccd`, `cmos`, `scientific-cmos`, `unknown`
- `shutterType`: `global`, `rolling`, `frame-transfer`, `global-reset`, `unknown`
- `colorMode`: `monochrome`, `color`, `unknown`
- `readNoiseElectronsRms`
- `darkCurrentElectronsPerPixelPerSecond`
- `fullWellCapacityElectrons`
- `maximumFrameRateFramesPerSecond`
- `quantumEfficiency`: array of `{ wavelengthNanometers, quantumEfficiency }`
- `provenance`: `manufacturer-datasheet`, `vendor-model-descriptor`, `user-verified`, `measured`, `unknown`
- optional `provenanceReference`

Quantum-efficiency samples describe a spectral curve; they are not replaced by a single assumed wavelength.

## Illumination specification

Each source record contains `componentId`, `technology`, and `channels`.

Technology values: `led`, `halogen`, `xenon-arc`, `mercury-arc`, `metal-halide-arc`, `laser`, `other`, `unknown`.

Each channel contains:

- `id`
- optional `spectralBand` — minimum/maximum wavelength in nanometers
- `controlKind` — `fixed`, `continuous`, `device-defined`, `unknown`
- optional `intensityRange` — minimum, maximum and explicit unit
- optional `spectralPowerDistribution` — increasing wavelength samples with relative power

Intensity units: `percent`, `milliamperes`, `milliwatts`, `relative`, `device-defined`.

Relative spectral power is a normalized shape; it does not claim absolute irradiance. Absolute irradiance requires calibration owned by the calibration/runtime layer.

## Stage and carrier

A stage contains `componentId` and `axes`.

Each axis contains:

- `id`
- `motionType`: `linear`, `rotary`, `other`, `unknown`
- `coordinateUnit`
- `operatingRange`: `minimum`, `maximum`, `unit`
- `nominalResolution`
- optional `accuracy`: `maximumAbsoluteError`, `testConditions`
- optional `repeatability`: `value`, `metric`, `testConditions`
- optional `measurementUncertainty`: `value`, `testConditions`
- `positionFeedback`
- optional `measurementLocation`

Coordinate units: `micrometers`, `millimeters`, `degrees`, `radians`, `device-defined`, `unknown`.

Repeatability metric: `maximum-deviation`, `standard-deviation`, `root-mean-square`, `unknown`.

Feedback capability: `none`, `encoder`, `linear-scale`, `device-reported`, `external-sensor`, `other`, `unknown`.

Measurement location: `motor`, `axis`, `specimen`, `external`, `other`, `unknown`.

All axis performance values use the coordinate unit declared by that axis. Test conditions are retained as provenance; the loader does not collapse accuracy, repeatability and uncertainty into one number.

A dual-slide carrier contains `stageComponentId`, `horizontalAxisId`, `transverseAxisId`, and `direction`. Direction values are `positive-stage-x-left-to-right` or `negative-stage-x-left-to-right`. The referenced StageSpecification must exist, and Core validates both axis identities and units.

## Validation, bounds and persistence

The loader enforces:

- schema version 1; no unknown fields or duplicate object keys;
- valid JSON number and string syntax, including UTF-8 and Unicode surrogate pairs;
- maximum document size 16 MiB, nesting depth 64, 500,000 JSON nodes, 50,000 array elements, 128 members per object, and 1 MiB per string;
- exact integer parsing for revision and pixel/count fields;
- finite numeric values and explicit enum strings;
- all nested value factories and the final `MicroscopeEquipmentDefinition::create` graph validation.

The loader returns a validated Core value; it does not write a database or change runtime state.

## Equipment-definition revision persistence

`EquipmentDefinitionSqliteStore` stores the validated source JSON document as the immutable equipment-definition aggregate. It intentionally does not decompose the physical graph into a second relational representation or serialize native C++ object memory.

- Revision streams are keyed by `MicroscopeId`.
- A save accepts only a document whose embedded microscope identity matches the requested repository scope.
- The document's `revision` must equal `expectedCurrentRevision + 1`; a transaction and optimistic current-revision check prevent stale writers.
- Published source-document rows cannot be updated or deleted. The current pointer advances sequentially and cannot be deleted.
- The exact source text is retained, including whitespace and ordering. On load, the document is parsed again through `EquipmentDefinitionJsonLoader` and Core factories.
- The loaded Core model's `MicroscopeId` and revision are checked against the SQL key. Corrupt, unsupported or mismatched documents are rejected rather than partially reconstructed.

The configuration-profile catalog remains a separate revisioned aggregate scoped to a specific equipment revision. Calibration, physical observations and runtime state are also separate.

A schema change must increment `schemaVersion` and add an explicit backward-compatibility/migration policy. Do not silently reinterpret historical source documents.
