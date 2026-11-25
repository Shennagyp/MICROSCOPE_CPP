# Microscope Platform — v0.9 Implementation Plan

**Architecture reference:** ../architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Status:** Planned; architecture documentation is established, source migration is incremental.

## Objective

Migrate from the current native-app-centered microscope configuration into two independent C++ libraries:

- Microscope.Core: complete microscope model and behavior.
- Microscope.Application: JSON definition loading, adapter composition, runtime management and frontend-neutral application API.

Keep the current macOS application as one frontend. Preserve the existing camera/preview pipeline. Do not create speculative empty libraries or duplicate active implementations.

## Current repository baseline

The current branch contains:
- Root CMake project using C++ and an optional native macOS application target.
- Camera/Amscope as the reusable camera driver capability.
- Application/OptikaB293 containing the current SwiftUI/AppKit application, Objective-C++ camera bridge and current Swift microscope configuration/resolution models.
- Existing architecture documentation describing this transitional native application and its preview invariants.

No Microscope.Core or Microscope.Application C++ library is claimed to exist yet. Those targets are introduced in Phase 1 when the first real contracts and implementations are added.

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

### Phase 1 — C++ library foundations
- Add Libraries/Microscope.Core and Libraries/Microscope.Application.
- Add independent CMake targets using the current C++17 baseline.
- Define identity types, units, Result/diagnostic conventions and API visibility.
- Define Core adapter ports, operation IDs, command IDs, feedback envelopes and observation provenance.
- Define Application public API and subscription/operation-handle semantics.
- Ensure Core has no dependency on JSON, UI, vendor SDK or Application.

**Exit:** independent library build graph and contract tests; Application depends on Core only.

### Phase 2 — Equipment model
- Model microscope identity and installed component identity.
- Model optical components, component ports/connections, light paths and shared elements.
- Model objectives, cameras/sensor modes, illumination, condenser and stage/axes.
- Include motion capability and performance specifications without mechanical transmission detail.
- Define how the JSON data maps to typed Core values.

**Exit:** equipment definitions validate, including shared optical components and multiple camera paths.

### Phase 3 — Declarative configuration
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
- Define Sample and SampleType as source-specimen identity and taxonomy.
- Define Slide and SlideType as physical preparation identity and taxonomy.
- Enforce the baseline relationship Sample 1-to-many Slides; each Slide references one Sample.
- Define SlideConfigurationSnapshot and Tile, with Tile owned by a Slide.
- Implement sample/slide folder lifecycle through storage ports without making the microscope the specimen owner.
- Enforce mandatory active slide session in Core.
- Define stable identities and portable relative references.
- Support opening and browsing sample/slide records without a microscope connected.

**Exit:** sample identity is distinct from preparation identity; one sample can have multiple slides; invalid/missing active slide session blocks acquisition; existing data remains portable and recoverable.

### Phase 8 — Acquisition
- Define AcquisitionPlan, AcquisitionRun, ImageFrame and DerivedImage identities.
- Make each acquisition record identify the actual MicroscopeId and the Sample/Slide/Tile context relevant to that run.
- Preserve requested plan separately from actual run results.
- Add optional position evidence and explicit missing/failed frames.
- Integrate existing camera frame delivery through its adapter.
- Add Z-stack, exposure bracket and partial-run persistence semantics.

**Exit:** instrument history is represented through acquisition records rather than sample ownership; irregular acquisitions and recovery are represented without a fixed dense tensor; original frames are immutable.

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
- **Pure unit tests:** configuration resolution, calibration fitting, unit conversion, transition planning and state reducers.
- **Runtime contract tests:** simulated adapter responses, delayed callbacks, duplicates, stale/late events, timeout, failure and manual confirmation.
- **Application integration tests:** definition loading, adapter registration, resource conflicts, runtime lifecycle and frontend projections.
- **Hardware acceptance:** separate user-run checks for actual camera, stage, objective selector, illumination, optical selectors and calibration equipment.

Do not run hardware tests in CI. Do not interpret pure test success as proof of actual hardware precision or device behavior.

## Immediate next implementation task

Start Phase 1 only after reviewing the public contract names and event/feedback envelope. The first source change should create real, independently buildable C++ targets with a small set of consumed public contracts. Do not create empty folders or stub classes for every future subsystem.
