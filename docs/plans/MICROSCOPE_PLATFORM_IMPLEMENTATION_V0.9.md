# Microscope Platform — v0.9 Implementation Plan

**Architecture reference:** ../architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Status:** Phase 1 completed; Phase 2 equipment modeling in progress. Cross-platform CI passed on Ubuntu GCC, macOS Clang and Windows MSVC for Core standalone, Application standalone and root platform-only builds/tests. [Workflow run](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45583316832).

## Objective

Migrate from the current native-app-centered microscope configuration into two independent C++ libraries:

- Microscope.Core: complete microscope model and behavior.
- Microscope.Application: JSON definition loading, adapter composition, runtime management and frontend-neutral application API.

Keep the current macOS application as one frontend. Preserve the existing camera/preview pipeline. Do not create speculative empty libraries or duplicate active implementations.

## Current repository baseline

The current branch contains:
- A root CMake project with optional native macOS application and AmScope targets. Core and Application are included independently; `OPTIKA_BUILD_AMSCOPE=OFF` permits a platform-only root configuration.
- `Microscope::Core` and `Microscope::Application` as separate C++17 static libraries with standalone CMake configuration; Application brings in Core as its source dependency when configured independently.
- `Camera/Amscope` as the existing reusable camera capability and `Application/OptikaB293` as the current SwiftUI/AppKit frontend, Objective-C++ bridge and transitional Swift microscope configuration/resolution implementation.
- Core contracts for strong IDs, distinct runtime/device-session generations, [[nodiscard]] Result/Status, validated diagnostics, operation/step status values, typed feedback correlation and ingress/receipt metadata.
- Application value contracts for runtime/operation handles, factory-created immutable RuntimeSnapshot values with validated lifecycle/positive revision, and Core-snapshot-backed OperationView with validated composite identity.
- Core identity/diagnostic/feedback and Application handle/projection test sources registered with CTest. They have not been built or executed here.
- Architecture, public-contract and standalone-library documentation.

The two libraries are an initial Phase 1 source slice, not a completed platform implementation. Equipment/configuration models, the serialized operation executor, JSON loading, adapter factory/runtime composition, persistence, calibration, acquisition and hardware workflow integration are not implemented or claimed.

The feedback ingress is currently a typed contract, not a queue/executor. Application currently provides handle/snapshot value contracts, not a runtime manager or adapter-composition implementation.

## Migration rules

1. Preserve existing behavior unless a change is explicitly intended.
2. Do not rewrite the camera acquisition worker, frame pool, frame lease, latest-frame mailbox or preview pipeline as part of the library extraction.
3. Move one coherent behavior at a time: define Core contract, implement/test Core behavior, integrate through Application, then migrate the frontend.
4. Never keep two authoritative implementations of the same microscope rule. During a vertical-slice migration, the new C++ path becomes authoritative before the old Swift path is removed.
5. Keep JSON parsing outside Core.
6. Keep concrete hardware SDKs outside Core and Application.
7. Use simulated adapter implementations for Core runtime tests; do not require a connected microscope for pure/domain tests.
8. Distinguish dispatch, adapter/device acknowledgment, hardware completion, observation and Core verification.
9. Do not claim runtime or hardware success without evidence from the user's environment.

## Phases

### Phase 0 — Architecture baseline (documentation)
- Establish one authoritative platform architecture.
- Define Core/Application boundaries.
- Specify operation and feedback semantics.
- Record calibration, slide, acquisition and persistence invariants.
- Identify existing native app code as transitional migration source.

**Exit:** architecture and implementation plan are committed and linked from the architecture index.

### Phase 1 — C++ library foundations and public contracts

**Completed**

**Accepted evidence**
- Core and Application are separate C++17 static libraries; Core depends only on the C++ standard library, and Application links publicly to Core.
- Each target configures independently; Application's standalone project adds its Core source dependency. The root project can configure the platform libraries with AmScope and the native macOS application disabled.
- Strong identity values, [[nodiscard]] Result/Status, validated diagnostics, operation/step status values, immutable feedback envelopes/receipts, feedback correlation/provenance validation, device/runtime session generations, and immutable runtime/operation snapshots are implemented.
- Feedback ingress overflow, wakeup, ordering, provenance and correlation invariants are specified as contracts. The actual queue/executor is reserved for Phase 4.
- Core, Application and the root platform-only composition all built and passed their registered CTest suites on Ubuntu GCC, macOS Clang and Windows MSVC in [GitHub Actions run 6](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45583316832).
- The existing AmScope driver and Camera::Common pipeline remain separate targets; no camera acquisition/preview rewrite was part of Phase 1.

**Boundary of completion**
This closes the initial library/build/API foundation only. It does not claim a microscope equipment model, configuration resolver, operation executor, JSON loader, adapter registry, persistence, calibration, acquisition or frontend migration.

**Exit:** satisfied. Further runtime feedback lifecycle tests, simulated-device execution and hardware acceptance remain assigned to Phase 4 and hardware acceptance respectively.

### Phase 2 — Core equipment and optical model

**In progress — 2A–2D source slices validated**

**Modeling sequence**
1. **2A — Installed equipment identity and optical topology:** unique physical component identities; typed optical ports; undirected physical port connections; ordered directional light paths; shared components represented once and referenced by multiple paths; topology validation.
2. **2B — Optical specifications and imaging:** objectives and their specification values; sensor geometry; camera output/readout modes; camera-to-optical-path attachment; explicit sensor-pixel to delivered-pixel mapping inputs.
3. **2C — Illumination and condenser:** source identity/control capability and condenser identity/optical compatibility data, independent of objective classes.
4. **2D — Stage and axes:** linear/rotary axis identity and unit; range, nominal resolution, accuracy, repeatability/precision, feedback provenance and uncertainty fields. No mechanical transmission implementation details.
5. **2E — Complete definition validation:** validate references, uniqueness, port ownership/direction, connected route continuity, component sharing and required subsystem references.

**Phase 2B committed source progress**
- Added distinct positive finite domain values for micrometers, nominal magnification and numerical aperture. The type domains are intentionally narrow; no general-purpose units framework was added.
- Added ObjectiveSpecification attached to EquipmentComponentId, with magnification, NA, working distance, chromatic/field correction, immersion medium and optional design coverglass thickness.
- Added CameraSpecification attached to EquipmentComponentId, including active sensor dimensions, independent X/Y pixel pitch and unique output modes.
- Added CameraOutputMode with physical sensor ROI, binning, delivered image dimensions, pixel layout and stable pixel-format code. The net sensor-to-delivered-pixel mapping is independently derived for X and Y; binning remains separately recorded.
- MicroscopeEquipmentDefinition validates objective/camera references against installed component identity and broad component kind, and rejects duplicate specialized records.
- Added contract tests for finite/positive values, NA above 1, objective characteristics, anisotropic pixel pitch/mapping, ROI and binning limits, invalid pixel formats/layouts, duplicate modes/specifications, unknown component references and wrong component kinds.
- Cross-platform CI passed on Ubuntu GCC, macOS Clang and Windows MSVC for Core standalone, Application standalone and root platform-only composition/tests in [GitHub Actions run 33](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45589429447).

**Phase 2C committed source progress**
- Added condenser specifications attached to installed OpticalElement identities: minimum/maximum aperture NA (minimum may be zero), design, immersion medium and optional working distance.
- Added illumination source specifications attached to installed IlluminationDevice identities, with multiple unique channels per source.
- Each illumination channel carries an optional spectral band, control kind and optional unit-bearing intensity range. Continuous control requires a valid finite non-negative increasing range; fixed/device-defined/unknown controls do not pretend to expose one.
- Equipment aggregate validation rejects condenser/illumination specifications for unknown components or components of the wrong kind, and rejects duplicate specializations.
- Added tests for spectral ranges, intensity units/ranges/control semantics, multichannel source identity, condenser aperture bounds, invalid enums, duplicate records and aggregate reference/kind validation.
- Cross-platform CI passed for Core standalone, Application standalone and root platform-only builds/tests on Ubuntu GCC, macOS Clang and Windows MSVC in [GitHub Actions run 46](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45590795457).

**Phase 2D committed source progress**
- Added StageAxisId and AxisSpecification with linear/rotary/other/unknown motion types and explicit coordinate units (µm, mm, degrees, radians, device-defined, unknown).
- Added signed finite operating range and positive nominal resolution. Range units must match the axis declaration and known linear/rotary motion types reject incompatible units.
- Added independent optional accuracy, repeatability/precision and measurement-uncertainty specifications. Accuracy and repeatability retain test-condition descriptions; repeatability records its metric. No performance value is inferred from resolution.
- Added explicit position-feedback capability and measurement-location values. A no-feedback axis cannot claim a measurement location; these are capability declarations, not live position states.
- Added StageSpecification with one or more uniquely identified axes and aggregate binding to an installed MechanicalAssembly component.
- Added tests for typed linear/rotary units, signed ranges, unit mismatch, independent performance values, measurement conditions, feedback capability/location, duplicate axes, empty stage and stage/component binding.
- Cross-platform CI passed for Core standalone, Application standalone and root platform-only builds/tests on Ubuntu GCC, macOS Clang and Windows MSVC in [GitHub Actions run 58](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45592166839).

**Implementation constraints**### Phase 3 — Declarative configuration
- Define profile selection and applicability.
- Define required components, component positions, illumination/camera presets and constraints.
- Define conflict diagnostics and compatibility rules.
- Implement pure resolution, validation and transition-plan generation.
- Build representative data-driven cases for brightfield, phase contrast and DIC without encoding behavior in component subclasses.

**Exit:** configuration results are deterministic; impossible combinations are rejected before hardware commands are issued.

### Phase 4 — Core operation runtime
- Implement operation and step lifecycle.
- Implement dependency-plan execution.
- Implement typed command ports and normalized adapter feedback.
- Correlate all feedback to operation/step/command/device identities.
- Serialize event processing per runtime.
- Implement timeouts, cancellation, stale feedback rejection, partial completion and reconciliation.
- Test both feedback-rich and feedback-poor/manual devices.

**Exit:** no operation is marked complete solely because a command was dispatched or accepted.

### Phase 5 — JSON and application composition
- Define versioned microscope-definition JSON schema.
- Define deployment JSON schema for adapter bindings.
- Add Application-owned definition catalog/loader.
- Add explicit adapter factory registration and binding.
- Add per-device resource claims and runtime manager.
- Create/open/close multiple microscope instances.

**Exit:** application can load a JSON definition, resolve its adapter factories and create a validated Core runtime.

### Phase 6 — Calibration
- Implement nominal image scale from sensor and optical specifications.
- Implement ruler-slide observation capture contract and measured X/Y scale fit.
- Define optical calibration key and invalidation behavior.
- Implement linear/rotary motion calibration and evidence quality.
- Store immutable revisions through calibration persistence ports.

**Exit:** calibration is reused by matching configuration; no unnecessary per-slide calibration duplication or stage-to-slide registration.

### Phase 7 — Sample, slides and persistence
- Define distinct strong identity types for SampleId, SlideId and TileId; identifiers are opaque and independent of labels, paths and display order.
- Define Sample and SampleType as source-specimen identity and taxonomy.
- Define Slide and SlideType as physical preparation identity and taxonomy.
- Enforce the baseline relationship: each Slide references exactly one existing Sample; one Sample may have many Slides.
- Make Slide.SampleId the authoritative relationship. Do not persist an authoritative SlideIds collection on Sample; provide reverse lookup through a query/index.
- Define the Sample record fields: stable SampleId, SampleTypeId when classified, display label, optional external accession and source/collection metadata.
- Define the Slide record fields: stable SlideId, required SampleId, SlideTypeId when classified, display label, versioned preparation snapshot and package reference.
- Define Tile metadata: stable TileId/SlideId, optional display label/order hint and optional position evidence with provenance; never treat display order as physical coordinates.
- Define the versioned SlideConfigurationSnapshot and immutable Sample/Slide identity semantics.
- Make Tile belong to one Slide; permit no physical position and do not interpret ordering/index as coordinates.
- Define archive/deletion constraints: Sample archival does not cascade-delete slides or image data; a referenced Sample cannot be destructively deleted.
- Define independent Sample and Slide package persistence. Slide packages retain SampleId and remain independently movable; the Sample record is not duplicated inside the Slide package as another authority.
- Define portable relative payload references and a rebuildable workspace catalog/index.
- Define deterministic safe storage-key encoding/mapping for opaque IDs; do not use raw IDs as filesystem path segments.
- Enforce mandatory active slide session in Core.
- Support opening/browsing Sample and Slide packages without a microscope connected.

**Exit:** identity and relationship invariants are enforced; one sample can have multiple slides; Slide-to-Sample ownership is unambiguous; folder moves do not change IDs; indexes can be rebuilt; invalid/missing active slide session blocks acquisition; archival cannot destroy referenced slide data.

### Phase 8 — Acquisition
- Define distinct AcquisitionPlanId, AcquisitionRunId and ImageFrameId identities.
- Associate each AcquisitionPlan with exactly one Slide; freeze its intended settings when execution begins.
- Associate each AcquisitionRun with exactly one Slide, one AcquisitionPlan and the actual MicroscopeId used.
- Resolve the Sample context through Slide.SampleId rather than maintaining a second editable Sample relationship on the run.
- Define Tile as a stable field identity: a run can produce multiple tiles, and a tile can receive frames from multiple runs.
- Require each ImageFrame to reference exactly one AcquisitionRun and one Tile belonging to that run's Slide.
- Define ImageFrame metadata: immutable frame identity, run/tile references, payload reference and integrity metadata, capture timestamp, delivered dimensions/pixel format, camera/readout context and actual settings when available.
- Preserve original frame payloads immutably and separate payload storage from capture metadata.
- Keep requested values distinct from actual device observations; missing readbacks remain unknown.
- Commit a recoverable run identity before dispatch; persist payload/integrity metadata before publishing a frame as available.
- Add optional position evidence, explicit missing/failed frames, Z planes, exposure brackets and partial-run persistence.
- Integrate existing camera frame delivery through its adapter without changing the existing bounded frame/lease/preview pipeline.

**Exit:** plans and actual runs remain distinct; every frame has valid run/tile provenance; repeated acquisitions can contribute to an existing tile; cross-slide frame references are rejected; incomplete runs recover correctly; original frames remain immutable; no additional preview queues or pixel copies are introduced.

### Phase 9 — Image documents and annotation
- Define persistent annotation document and geometry types.
- Keep geometry in source-image coordinates.
- Store measurement calibration revision, unit, result and provenance.
- Add image processing provenance for HDR/focus stack.

**Exit:** reopening a saved image reconstructs the same original pixels and annotation state without a connected microscope.

### Phase 10 — Frontend migration
- Add Objective-C++ bridge to Microscope.Application.
- Replace Swift-owned microscope configuration behavior with the C++ application API.
- Keep SwiftUI/AppKit presentation and transient interaction state.
- Preserve native frame streaming as a separate path.
- Add CLI/service host only when needed.

**Exit:** frontends consume one application API and do not contain microscope policy.

## Verification strategy

- **Static/model validation:** identity integrity, graph/topology consistency, schema validation and dependency direction.
- **Sample/slide contract tests:** unknown SampleId rejection, Sample-to-many-Slides cardinality, distinct SampleType/SlideType semantics, stable IDs after folder moves, non-authoritative reverse indexes and archive/deletion referential integrity.
- **Acquisition contract tests:** plan/run distinction, actual MicroscopeId capture, one-slide run constraint, frame-to-run/tile consistency, cross-slide rejection, multi-tile runs, repeated tile acquisition, idempotent duplicate frame delivery and interrupted-run recovery.
- **Pure unit tests:** configuration resolution, calibration fitting, unit conversion, transition planning and state reducers.
- **Runtime contract tests:** simulated adapter responses, delayed callbacks, duplicates, stale/late events, queue saturation/feedback-loss latching, reconciliation before verification, timeout, failure and manual confirmation.
- **Application integration tests:** definition loading, adapter registration, resource conflicts, runtime lifecycle and frontend projections.
- **Hardware acceptance:** separate user-run checks for actual camera, stage, objective selector, illumination, optical selectors and calibration equipment.

Do not run hardware tests in CI. Do not interpret pure test success as proof of actual hardware precision or device behavior.

## Immediate next implementation task

The public contract review is recorded in ../architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md.

Phase 1 is closed by cross-platform standalone/composed CI. The current implementation task is Phase 2A: build the first typed optical-equipment and route-topology vertical slice in Core, with deterministic validation and tests. Keep JSON parsing in Phase 5, configuration behavior in Phase 3, operation execution in Phase 4 and concrete adapter registration in Phase 5. Do not add empty modules or speculative framework abstractions.
