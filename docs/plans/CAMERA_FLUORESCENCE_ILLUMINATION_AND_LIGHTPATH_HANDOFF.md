# Camera Fluorescence, Illumination and LightPath Calibration — Continuation Handoff

**Target branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Branch head inspected before this handoff was added:** `da4add474940732f817a3ba424e6f5acd1e40e82`  
**Scope:** C++ Camera/Core/Application model and persistence design. Do not migrate this work into Swift in this phase.

## 1. User requirements that are now authoritative

1. A camera used for acquisition must be physically connected to the imaging route of the selected `LightPath`. Camera identity is derived from that route endpoint, not selected from an unrelated global camera list.
2. If the user manually assigns a camera that is physically connected and driver-operable but scientifically less suitable for the requested task, **warn only**. Keep the user's camera selection. Do not reject it or silently substitute another camera.
3. Camera-model capabilities and sensor characteristics must be discoverable through the existing Camera project in this repository and stored in persistence. Reusable model identity is manufacturer + model + optional hardware variant; a serial number or transient SDK enumeration ID identifies a connected unit, not a model profile.
4. For automatic recommendations, compare cameras using task-relevant evidence. Recommend global shutter when otherwise equivalent cameras satisfy the requirements, or when the acquisition explicitly requires the temporal behavior. Global shutter is not a universal hard requirement and must not automatically outrank a materially more sensitive scientific camera.
5. Model illumination source technologies, their channel-specific spectral output, and physically installed filter options. Fluorescence requires separate excitation and detection spectral reasoning.
6. Every microscope-specific operation mode has its own saved LightPath configuration and calibrated illumination target. A 4× Brightfield mode may deliberately use lower specimen-plane irradiance than 40× Brightfield or 4× Darkfield. There is no universal intensity-versus-magnification formula.
7. The user's equipment constitution and operation modes are editable and persisted. Loaded objects remain resident in their owner scope; Core resolution does not query persistence.

## 2. Hard constraints versus advisory suitability

The system must not conflate physical execution validity with image-quality preference.

| Condition | Required behavior |
|---|---|
| Camera is not a terminal endpoint on the selected LightPath imaging route | Not an eligible camera for that LightPath. Offer another valid LightPath where possible. |
| Camera has no supported output mode or the active adapter cannot operate it | Not executable with the current device/session. |
| Camera is connected and operable but has low QE, unsuitable shutter behavior, higher noise, inadequate frame rate or another task-performance shortfall | Keep the camera selectable; return explicit Warning diagnostics and rationale. |
| Camera performance data are incomplete or unknown | Keep it selectable; return an unknown-evidence warning. Never infer a pass or a failure from missing data. |
| Global-shutter camera and rolling-shutter camera are otherwise equivalently suitable | Prefer global shutter if that policy is enabled. |
| Rolling-shutter camera has materially better QE/noise performance for the actual fluorescence band | Do not rank a global-shutter camera above it merely because of shutter type. Explain the trade-off. |

If a future acquisition contract explicitly requires global shutter (for example, a temporal synchronization requirement), that requirement may be hard. That is a task requirement, not a universal camera suitability rule.

## 3. Current source groundwork

### Camera project

- `Camera/Common/include/camera/CameraModels.h`
  - Standard camera capability values now include global shutter, monochrome sensor and sensor CCD mode flags.
  - `CameraSensorCharacteristics` carries optional sensor technology, shutter type, color mode, active sensor dimensions, X/Y pixel pitch, QE samples, read noise, dark current, full well, frame rate and evidence provenance.
  - `CameraDeviceInfo` can carry an enriched optional sensor profile.
- `Camera/Common/include/camera/CameraModelCatalog.h`
  - Adds stable manufacturer/model/variant identity, model profiles, catalog revision, exact match and ambiguous candidate discovery.
  - Incomplete identity returns candidates rather than silently taking the first profile.
- `Camera/Amscope/src/AmcamCamera.cpp`
  - The discovery/model-information block maps vendor model flags for global shutter, monochrome, CMOS and CCD into common capability/sensor fields.
  - It deliberately does not invent QE, read noise, dark current, pixel pitch or full-well values.
  - No acquisition, frame-buffer, stream lifecycle or preview implementation was intended to change in this work.
- `Camera/Common/tests/CameraModelsTests.cpp`
  - Catalog matching, variant ambiguity, QE validation, provenance and capability tests have been authored.

### Microscope Core

- `Libraries/Microscope.Core/include/microscope/core/equipment/OpticalSpecifications.h` and `src/OpticalSpecifications.cpp`
  - Core-side camera sensor characteristics attach to the installed camera's `CameraSpecification`.
  - `OpticalFilterSpecification` models excitation, emission, dichroic, neutral-density and other filter roles with ordered wavelength-dependent response samples.
- `Libraries/Microscope.Core/include/microscope/core/equipment/IlluminationSpecifications.h` and `src/IlluminationSpecifications.cpp`
  - Source technologies include LED, halogen, xenon arc, mercury arc, metal-halide arc and laser.
  - An illumination channel can carry a spectral power distribution. This describes relative spectral shape, not absolute specimen irradiance.
- `Libraries/Microscope.Core/include/microscope/core/equipment/OpticalTopology.h` and `src/OpticalTopology.cpp`
  - Installed filter specifications are associated with the actual equipment component and validated against its declared optical-function role and compatibility identity.
  - The earlier LightPath topology remains the owner of route membership; filters still need to be validated against the selected LightPath during resolver integration.
- `Libraries/Microscope.Core/include/microscope/core/configuration/CameraSuitability.h` and `src/CameraSuitability.cpp`
  - `CameraSuitabilityAssessor` returns warning diagnostics and advisory ranking for fluorescence requests.
  - The current first-pass QE calculation is an unweighted mean over the requested emission band. It is explicitly incomplete for full fluorescence-system prediction.
- `Libraries/Microscope.Core/src/Configuration.cpp`
  - A legacy profile camera allow-list mismatch is now a warning, not a resolution error. Route endpoint and driver-operability constraints remain separate.
- `Libraries/Microscope.Core/include/microscope/core/configuration/LightPathCalibration.h` and `src/LightPathCalibration.cpp`
  - `LightPathCalibration` stores measured source-drive-to-specimen-irradiance points, uncertainty, microscope/equipment/mode revisions, LightPath, source channel and configured optical component identities.
  - `LightPathModeSetting` stores a per-mode target specimen irradiance and references the exact calibration revision.
  - Drive resolution checks microscope, equipment revision, mode revision, LightPath, source/channel, calibration revision and configured component identities. It uses measured-interval interpolation and does not extrapolate.
- Dedicated test source files were added and registered in CMake for camera suitability and LightPath calibration. Spectral source/filter test coverage was also added to existing Core contract test targets.

### Documentation updated

- `docs/architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md`
- `docs/architecture/OPTIKA_B293_APPLICATION_SYSTEM_ARCHITECTURE_V1.md`
- `docs/plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md`

The implementation plan now identifies Phase 3I (camera catalog/suitability), 3J (spectral sources/filters) and 3K (per-mode LightPath calibration contracts), and records which pieces are source groundwork versus outstanding integration.

## 4. Scientific model rules

### Camera suitability for fluorescence

The detector-side evaluation should use the detected emission spectrum after optical transmission, not the nominal lamp color or only the nominal fluorophore peak.

The intended weighting is conceptually:

`QE_weighted = integral[emission(λ) × dichroic_transmission(λ) × emission_filter_transmission(λ) × camera_QE(λ)] / integral[emission(λ) × dichroic_transmission(λ) × emission_filter_transmission(λ)]`

The source-side excitation calculation is separate:

`excitation_at_sample(λ) = source_SPD(λ) × excitation_filter_transmission(λ) × dichroic_reflection_toward_sample(λ) × route_throughput(λ)`

The two sides must not be collapsed into one filter-band label. The fluorophore emission model is task/fluorophore data; camera QE is model-profile data; the installed filter response is equipment-constitution data; the actual illumination SPD is source/channel data.

Use QE, read noise, dark current, full-well capacity, pixel pitch, output mode, frame rate, exposure and readout behavior only where the requested acquisition makes them relevant. Monochrome sensors are generally preferable for maximum sensitivity/resolution in many fluorescence workflows, but this remains an advisory preference because the user's application may require color information or a particular acquisition protocol.

Do not claim that a global-shutter camera is inherently more sensitive. Shutter architecture and quantum efficiency/noise performance are separate properties. Preserve provenance and validity for every stored sensor fact.

### Illumination and filters

Keep these separate:
- Source technology and each independently controlled illumination channel.
- Relative spectral power distribution of that channel.
- Actual installed optical filter components and their response curves.
- Route direction and optical position of each filter.
- Measured absolute irradiance delivered at the specimen plane.
- Device drive setting (percent/current/power/device-defined units).

For a fluorescence cube, excitation filter, dichroic and emission filter must share the correct compatibility identity and occupy the correct routes. An excitation filter is evaluated in the illumination route; an emission filter is evaluated in the imaging/detection route; the dichroic has distinct reflection and transmission behavior. Neutral-density filters attenuate the applicable path and therefore affect calibration.

### Per-mode light intensity

A mode target should be a physical irradiance at a declared measurement plane, not merely a source percentage. The source drive value is derived through the matching calibration.

Illustrative editable modes:
- **4× Brightfield:** user-defined lower specimen-plane irradiance.
- **40× Brightfield:** separately calibrated irradiance; it may be higher than the 4× mode.
- **4× Darkfield:** separately calibrated irradiance; it may be higher than the 4× Brightfield mode because the darkfield stop/condenser geometry and useful scattered-light signal differ.

These are user-configured calibration outcomes, not universal rules. Do not fabricate example irradiance values or fixed drive percentages.

## 5. Critical gaps — do not treat the work as finished

1. **Persistence is not implemented.** The C++ value types and architecture define persistable data, but there is no complete Application persistence adapter/store/schema/migration for the camera model catalog, equipment constitution, operation modes, filters/source data or LightPath calibration. Implement the storage owner and transaction boundaries.
2. **OperationMode aggregate is not complete.** `OperationModeId` and `LightPathModeSetting` exist, but the editable/revisioned OperationMode aggregate that composes techniques, camera preference policy and one or more LightPath mode settings still needs to be integrated.
3. **Resolver integration is not complete.** The current `ConfigurationRequest`/resolver still has legacy explicit camera/path selection. Make LightPath the configuration input and derive camera endpoint/output-mode choices from its imaging routes. Keep manual camera selection as an optional assertion/choice among cameras connected to that LightPath only.
4. **Camera catalog to Core bridge is not complete.** Application must merge `CameraModelCatalog` data into live camera discovery and project the enriched data into the selected camera's Core `CameraSpecification`. Do not add a Core dependency on `Camera::Common` or vendor SDKs.
5. **Spectral calculation is incomplete.** The current mean-QE band calculation must be replaced by emission-spectrum/filter/dichroic weighted integration. Implement interpolation with defined behavior for incomplete spectral coverage and preserve unknown evidence as warning.
6. **Filter route validation is incomplete.** Equipment validation establishes installed-component/filter-role consistency. The resolver must additionally prove each required filter belongs to the selected LightPath and the correct directional route.
7. **Calibration key is incomplete.** A component ID set does not encode optical state. Add a canonical fingerprint/snapshot for all irradiation-affecting adjustable settings: route revision, objective, condenser, annulus/stop, source/channel, filters, aperture/iris/selector states, splitters/relay and geometry. Include meter identity/calibration, measurement plane, conditions, date and uncertainty.
8. **Calibration workflow is not implemented.** No measurement procedure, UI, adapter command/feedback or runtime verification currently applies these values to physical illumination. Do not claim a mode's target has been achieved until supported evidence is observed.
9. **Multi-camera execution remains separate.** A physical splitter branch can define multiple route-connected cameras, but synchronized capture requires explicit trigger/clock compatibility and runtime coordination. The current topology splitter validation is not proof of simultaneous acquisition support.
10. **Optical resolution policy remains modality-specific.** Do not reuse brightfield Rayleigh equations for fluorescence, phase contrast, DIC or confocal without their appropriate model and assumptions.

## 6. Recommended continuation sequence

Continue in this order; do not skip directly to UI or physical testing.

### Step 1 — Stabilize public C++ contracts
- Reconcile headers, source definitions, call sites, test fixtures and CMake registrations for the files added in this work.
- Check whether the AmScope SDK constants used by discovery mapping are present in the repository's pinned vendor header and verify their documented meaning.
- Ensure the model catalog and Core sensor characteristic value models have equivalent validation rules and explicit unknown/provenance semantics.
- Add/update contract tests but do not run local builds/tests/debugging in this work session.

### Step 2 — Implement Application persistence ports and owner-retained stores
- Add focused persistence interfaces for camera-model catalog, microscope equipment constitution, operation-mode revisions and LightPath calibration records. Avoid a generic repository framework or service locator.
- Keep Core persistence-free. Application owns serialization, schema/versioning, transactions, conflict handling, and the active in-memory objects.
- Persist the shared camera model catalog by stable manufacturer/model/variant key and profile revision.
- Persist each microscope's equipment constitution with installed camera-to-model-profile binding, optical topology, source/channel and filter records.
- Persist editable operation modes as revisions, with per-LightPath irradiance targets and exact calibration references.
- Persist calibration points, uncertainty and the complete physical configuration fingerprint. Preserve old revisions for historical acquisition snapshots.

### Step 3 — Complete Camera discovery enrichment
- When a device is enumerated, match its manufacturer/model/variant to the persisted catalog.
- If the variant is absent, return candidates. Do not choose a model profile if multiple variants remain possible.
- Merge known model facts into `CameraDeviceInfo`; preserve live feature descriptors separately from model specifications.
- Map the resulting data into Core `CameraSpecification` only for the camera endpoint derived from the selected LightPath.

### Step 4 — Integrate LightPath, camera and filter resolution
- Make selected `LightPathId` part of the configuration request.
- Derive the camera endpoint from imaging routes; camera is not an independent equipment candidate.
- Filter candidate routes by source, objective, condenser and technique requirements.
- Verify excitation filters on excitation routes, emission filters on detection routes, dichroic direction, compatibility IDs and filter spectral data.
- When more than one connected camera is explicitly exposed by a physical splitter arrangement, require the acquisition request to distinguish single-camera from multi-camera execution. Do not assume synchronization.

### Step 5 — Finish fluorescence spectral suitability
- Introduce a fluorophore/emission-spectrum requirement (or task profile reference) separate from source spectrum.
- Integrate emission spectrum × dichroic transmission × emission-filter transmission × camera QE over the detected band.
- Evaluate exposure/readout and sensor metrics with the acquisition mode's relevant requirements.
- Keep all performance mismatches and unknown sensor data warning-only when the selected camera is physically connected and operable.
- Rank global shutter after task-relevant sensitivity/noise evidence, unless a specific timing requirement makes shutter type hard.

### Step 6 — Integrate operation modes and LightPath calibration
- Implement the editable, revisioned OperationMode aggregate.
- Attach per-LightPath targets and calibrations to the mode revision.
- Replace the component-ID-only calibration key with a canonical optical-state fingerprint.
- Calibrate source drive against irradiance at the declared specimen-plane measurement location.
- Use a calibration curve only for a matching fingerprint and within its measured range. Support separate curves when source behavior exhibits hysteresis/non-monotonic branches.
- Expose source target → derived drive → observed/effective irradiance → residual/uncertainty as distinct values.
- Preserve historical calibration/mode revisions for acquisitions.

### Step 7 — Acceptance tests and later hardware validation
- Add tests for camera model ambiguity, manual camera override warnings, global-shutter tiebreaking, weighted QE, unknown spectral coverage, filter routing, ND attenuation, separate 4×/40×/darkfield mode targets, changed-state calibration invalidation, out-of-range targets and persistence revision conflicts.
- Keep physical tests postponed until the software model, persistence and runtime path are complete.
- Do not call a feature implemented merely because a value type or test source exists.

## 7. Protected implementation constraints

- Continue on `OPTIKA-B293-DUALSLIDE-APP-v.0.9`; do not create a new branch for this work.
- C++ Core/Application first. Do not apply this architecture to Swift now.
- Preserve the established AmScope connection, streaming, frame pool, buffer lifetime, acquisition and preview behavior. Only the narrow discovery metadata mapping is in scope for camera model capability work.
- Do not invent physical devices, adapter operations, SDK support or calibration measurements.
- Do not run local builds, tests or debugging runs in this work session. Authored tests and CMake registration must be reported as unverified until the user runs them in the intended environment.
- Maintain the separation: Camera project discovers facts; Core evaluates typed facts; Application owns persistence and long-lived object integration; adapters perform device I/O and report observations.
