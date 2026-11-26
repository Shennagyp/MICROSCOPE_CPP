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
  - Catalog matching, generic/specific variant ambiguity, empty-variant identity, QE validation, provenance and capability tests have been authored.
- `Libraries/Microscope.Application/include/microscope/application/persistence/CameraModelCatalogSqliteStore.h` and `src/CameraModelCatalogSqliteStore.cpp`
  - Application-owned SQLite store persists the shared catalog, model/profile revision, provenance, capability-known/supported masks, sensor characteristics and wavelength-dependent QE samples.
  - Writes replace the shared catalog atomically and require the expected current catalog revision. Loads validate persisted profile values and reconstruct the typed catalog.
- `Libraries/Microscope.Application/include/microscope/application/camera/CameraModelDiscoveryEnricher.h` and `src/CameraModelDiscoveryEnricher.cpp`
  - Enriches a connected device only from an exact manufacturer/model/variant match. Incomplete identity returns candidate profiles for explicit resolution.
  - Known live device capabilities/sensor facts are retained; conflicts with persisted model data are returned as warnings.
- AmScope discovery now identifies manufacturer as `AmScope`, model as the vendor model descriptor name, and leaves hardware variant absent unless the backend reports it. No acquisition/preview code was modified.

### Microscope Core

- `Libraries/Microscope.Core/include/microscope/core/configuration/LightPathCameraResolver.h` and `src/LightPathCameraResolver.cpp`
  - Resolves one camera/output-mode choice against one OperationMode + LightPath.
  - Candidate camera IDs are derived only from Imaging route terminal endpoints returned by `MicroscopeEquipmentDefinition::cameraEndpointIds()`.
  - Manual camera selection outside that endpoint set is Unsupported. A connected camera with low/unknown fluorescence suitability remains Resolved with warnings.
  - Multiple connected camera endpoints return alternatives; a strictly better fluorescence candidate may be recommended, while equal candidates require explicit selection. Global shutter participates only after the task-relevant sensor metrics.
  - Driver availability/operability is intentionally an Application runtime concern; this pure Core resolver validates the declared equipment topology and CameraSpecification.
- `Libraries/Microscope.Core/include/microscope/core/equipment/OpticalSpecifications.h` and `src/OpticalSpecifications.cpp`
  - Core-side camera sensor characteristics attach to the installed camera's `CameraSpecification`.
  - `OpticalFilterSpecification` models excitation, emission, dichroic, neutral-density and other filter roles with ordered wavelength-dependent response samples and bounded linear transmission/reflection interpolation.
  - `FluorescenceEmissionSpectrum` represents relative spectral photon-flux density for a fluorophore/task profile.
- `Libraries/Microscope.Core/include/microscope/core/equipment/IlluminationSpecifications.h` and `src/IlluminationSpecifications.cpp`
  - Source technologies include LED, halogen, xenon arc, mercury arc, metal-halide arc and laser.
  - An illumination channel can carry a spectral power distribution. This describes relative spectral shape, not absolute specimen irradiance.
- `Libraries/Microscope.Core/include/microscope/core/equipment/OpticalTopology.h` and `src/OpticalTopology.cpp`
  - Installed filter specifications are associated with the actual equipment component and validated against its declared optical-function role and compatibility identity.
  - The earlier LightPath topology remains the owner of route membership; filters still need to be validated against the selected LightPath during resolver integration.
- `Libraries/Microscope.Core/include/microscope/core/configuration/CameraSuitability.h` and `src/CameraSuitability.cpp`
  - `CameraSuitabilityAssessor` returns warning diagnostics and advisory ranking for fluorescence requests.
  - When a fluorophore emission spectrum, dichroic and emission-filter specifications are supplied, it integrates emission × dichroic transmission × emission-filter transmission × camera QE. It partitions at all input sample knots and applies three-point Gauss–Legendre quadrature to the product of piecewise-linear curves (degree at most four on each interval).
  - It does not combine dichroic/emission filters with different non-null compatibility identities.
  - It requires all curves to cover the full requested emission band; there is no extrapolation. The previous unweighted band mean is only a warned fallback when the spectral inputs are absent.
- `FluorescenceEmissionSpectrum` is now a Core value type for a relative spectral photon-flux distribution, distinct from source spectral power distribution. `OpticalFilterSpecification` exposes bounded linear transmission/reflection interpolation.
- `Libraries/Microscope.Core/src/Configuration.cpp`
  - A legacy profile camera allow-list mismatch is now a warning, not a resolution error. Route endpoint and driver-operability constraints remain separate.
- `Libraries/Microscope.Core/include/microscope/core/configuration/LightPathCalibration.h` and `src/LightPathCalibration.cpp`
  - `LightPathCalibration` stores measured source-drive-to-specimen-irradiance points, uncertainty, microscope/equipment/mode revisions, LightPath, source channel and configured optical component identities.
  - `LightPathModeSetting` stores a per-mode target specimen irradiance and references the exact calibration revision.
  - Drive resolution checks microscope, equipment revision, mode revision, LightPath, source/channel, calibration revision and configured component identities. It uses measured-interval interpolation and does not extrapolate.
- `Libraries/Microscope.Core/include/microscope/core/configuration/OperationMode.h` and `src/OperationMode.cpp`
  - Adds an immutable revisioned OperationMode aggregate owning one `LightPathModeSetting` per selected LightPath. Camera identity remains derived from topology, not pinned in the mode.
  - The aggregate checks microscope, mode ID/revision, equipment revision and unique LightPath identity across all settings.
- Dedicated test sources exist and are registered in CMake for camera suitability, LightPath calibration and OperationMode. Spectral source/filter test coverage was added to existing Core contract test targets.
- Tests are authored but have not been run.

### Application persistence

- `Libraries/Microscope.Application/include/microscope/application/persistence/MicroscopeConfigurationSqliteStore.h` and `src/MicroscopeConfigurationSqliteStore.cpp`
  - Persists immutable LightPath calibration revisions and revisioned OperationMode records in SQLite.
  - Mode saves validate referenced calibration existence and exact microscope/equipment/mode/path/source/channel/optical-component binding.
  - Uses transactional current-mode updates and optimistic revision checks; calibration revisions are append-only.
- Corresponding test source files:
  - `Libraries/Microscope.Application/tests/CameraModelCatalogSqliteStoreTests.cpp`
  - `Libraries/Microscope.Application/tests/CameraModelDiscoveryEnricherTests.cpp`
  - `Libraries/Microscope.Application/tests/MicroscopeConfigurationSqliteStoreTests.cpp`
- Application CMake links SQLite and Camera::Common; root CMake defines Camera::Common once for both Application and optional AmScope backend. The AmScope CMake target reuses the shared target.

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

1. **Persistence is partially implemented.** SQLite stores now persist the shared camera model catalog and the immutable LightPath calibration / revisioned OperationMode records, with optimistic revision checks and test sources. The microscope equipment constitution and its installed camera-to-model-profile bindings are not yet persisted. Source/filter/equipment revisions and acquisition snapshots also need durable records.
2. **OperationMode aggregate and persistence groundwork are implemented.** Core has a revisioned aggregate owning one LightPath setting per path; Application persists revisions and verifies calibration bindings. The aggregate does not yet include the full declarative technique composition/objective requirement model.
3. **Core LightPath camera selection and ConfigurationResolver propagation are implemented; canonical API migration remains.** `LightPathCameraResolver` derives camera candidates exclusively from imaging-route terminal endpoints, supports manual camera selection only among those endpoints, resolves camera output modes, and ranks fluorescence suitability without blocking a manually selected endpoint. `LightPathConfigurationRequestFactory` builds the established request from that selection and binds it to the exact OperationMode revision and LightPath. The existing `ConfigurationResolver` revalidates the route set and camera endpoint, merges the camera-selection diagnostics into its result, and retains the binding in the resolved request. The public legacy request shape still exposes independent camera/path fields for compatibility; migrate callers to the LightPath-first factory and later make the old constructor a deprecated compatibility path. Tests are authored but have not been run.
4. **Camera discovery enrichment and Core projection are implemented as isolated Application contracts; runtime composition remains.** Application enriches `CameraDeviceInfo` from an exact persisted catalog profile, reports conflicts/ambiguity, and projects the enriched sensor characteristics and provenance into Core `CameraSensorCharacteristics`. It must still invoke this enricher from the active camera-capability composition and attach the projected sensor data to the camera endpoint's `CameraSpecification` in the loaded equipment constitution. Do not add a Core dependency on `Camera::Common` or vendor SDKs.
5. **Full fluorescence spectral model is incomplete.** Detector-side emission-spectrum × dichroic-transmission × emission-filter-transmission × camera-QE integration is now implemented in Core, with bounded interpolation and weighted integration for piecewise-linear curves. Remaining: feed the fluorophore/task emission profile and exact installed detector filters from OperationMode/LightPath resolution; implement the excitation-side source SPD × excitation-filter transmission × dichroic reflection and combine both sides with collection-path throughput, exposure, readout mode, signal/noise and photobleaching constraints.
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

### Step 2 — Complete Application persistence
- **Implemented:** focused SQLite stores exist for the shared camera model catalog and for LightPath calibration / OperationMode revisions. Core remains persistence-free; Application owns serialization, transactions and revision checks.
- **Remaining:** persist each microscope's equipment constitution with installed camera-to-model-profile binding, optical topology, source/channel and filter records.
- **Remaining:** replace the provisional calibration component-ID set with the complete optical-state fingerprint and persist measurement instrument/conditions/provenance and measurement plane.
- **Remaining:** persist acquisition/configuration snapshots including selected camera, warnings/acknowledgements, effective observed illumination and the exact equipment/mode/calibration revisions.
- Preserve old mode/calibration/equipment revisions for historical acquisition snapshots. Avoid a generic repository framework or service locator.

### Step 3 — Complete Camera discovery integration
- **Implemented:** Application matches live discovery by manufacturer/model/variant, enriches missing facts, preserves live facts and returns candidate profiles or conflict warnings when exact matching is not possible.
- **Remaining:** invoke the enricher from the active camera capability composition using the loaded catalog; persist and maintain the per-microscope installed camera-component-to-model-profile binding with the equipment constitution.
- Map enriched data into Core `CameraSpecification` only for the camera endpoint derived from the selected LightPath.

### Step 4 — Migrate the legacy ConfigurationResolver to LightPath
- **Implemented in Core:** `LightPathCameraResolver` derives candidate IDs exclusively from the selected LightPath's Imaging route terminals, verifies the OperationMode is configured for that path, and resolves the available camera output mode.
- **Implemented in Core:** manual camera assignment is accepted only among those route-derived endpoints. A connected camera with weak or unknown fluorescence suitability remains selected with warnings; a camera that is not connected to the selected LightPath is Unsupported.
- **Implemented in Core:** with multiple endpoints, a unique suitability winner may be recommended. Global shutter is considered only after task-relevant QE/noise/dark-current/full-well/frame-rate/monochrome comparisons; an equal tie returns SelectionRequired. Tests are authored but not run.
- **Implemented in Core:** a generated request retains OperationMode ID/revision, selected LightPath, derived camera/output mode and camera-selection diagnostics. The existing ConfigurationResolver validates this metadata against installed routes and endpoints and carries warnings into ConfigurationResolution/ResolvedConfiguration.
- **Remaining:** make LightPath identity the canonical public request input and migrate current callers. Keep the old independent camera/path constructor only as a documented compatibility path during migration.
- **Remaining:** invoke the Application camera catalog enricher and project its sensor profile into the Core CameraSpecification for the selected endpoint. Application separately validates that the connected driver/session can actually operate the endpoint.
- **Remaining:** filter candidate routes by source, objective, condenser and technique requirements. Verify excitation filters on excitation routes, emission filters on detection routes, dichroic direction, compatibility IDs and filter spectral data.
- **Remaining:** distinguish single-camera selection from multi-camera simultaneous acquisition. A physical splitter creates endpoints; it does not prove trigger synchronization or capture support.

### Step 5 — Complete fluorescence spectral suitability
- **Implemented in Core:** `FluorescenceEmissionSpectrum`, filter transmission/reflection interpolation and detector-side weighted QE integration using emission × dichroic transmission × emission-filter transmission × camera QE.
- **Remaining:** source the fluorophore/emission profile and exact detector filter specifications from the selected operation mode/LightPath. Keep the coarse band mean marked as a fallback only.
- Implement the excitation-side source SPD × excitation-filter transmission × dichroic reflection calculation, then compose it with fluorophore excitation response, collection-path throughput, exposure/readout and relevant signal/noise/dose constraints.
- Keep performance mismatches and unknown sensor data warning-only when the selected camera is physically connected and operable.
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
