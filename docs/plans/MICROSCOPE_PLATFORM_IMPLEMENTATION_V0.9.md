# Microscope Platform — v0.9 Implementation Plan

**Architecture reference:** ../architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Status:** Phase 1 in progress; initial Core/Application targets and foundational contracts are committed. Build/test validation has not been performed in this environment.

## Objective

Migrate from the current native-app-centered microscope configuration into two independent C++ libraries:

- Microscope.Core: complete microscope model and behavior.
- Microscope.Application: JSON definition loading, adapter composition, runtime management and frontend-neutral application API.

Keep the current macOS application as one frontend. Preserve the existing camera/preview pipeline. Do not create speculative empty libraries or duplicate active implementations.

## Current repository baseline

The current branch contains:
- Root CMake project using C++ and an optional native macOS application target; the new Core and Application targets are added before existing camera/application targets. `OPTIKA_BUILD_AMSCOPE=OFF` permits a platform-only root configuration without the macOS/vendor camera dependency; the default remains ON.
- Microscope.Core and Microscope.Application are C++17 static libraries with standalone CMake entry support; configuring Application independently adds its required Core source dependency.
- Camera/Amscope as the reusable camera driver capability.
- Application/OptikaB293 containing the current SwiftUI/AppKit application, Objective-C++ camera bridge and current Swift microscope configuration/resolution models.
- Libraries/Microscope.Core and Libraries/Microscope.Application as separate C++17 static-library targets integrated by the root CMake project.
- Initial Core contracts: strong IDs, RuntimeGeneration, Result/Status, diagnostics, operation/step status values and typed feedback metadata/ingress.
- Initial Application contracts: runtime/operation handles and runtime/operation value projections.
- Focused Core identity/result/diagnostic, feedback and Application runtime-handle test sources registered with CTest; they have not been built or executed here.
- Existing architecture documentation describing the transitional native application and its preview invariants.

The new libraries are an initial Phase 1 source slice, not a completed platform implementation. No equipment model, configuration resolver, operation executor, JSON loader, adapter factory registry, persistence implementation or hardware integration is claimed to exist yet.

The first source slice is intentionally foundational: Core currently implements identifier-value validation, while the feedback ingress is a typed adapter-facing contract rather than a completed queue/executor. Application currently provides runtime/operation handle values rather than a runtime manager or adapter composition implementation.

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

**Committed source progress (not yet build/test validated):**
- Root CMake includes the independent Core and Application targets.
- Core static library contains strong opaque IDs, distinct RuntimeGeneration/DeviceSessionGeneration value types, Result/Status, Diagnostic, operation/step status values, runtime/device feedback scope, optional operation correlation, normalized FeedbackKind with factory-enforced enum/correlation/provenance validation, validated DeviceTimestamp, MicroscopeId/RuntimeId/RuntimeGeneration-scoped FeedbackIngressSequence and factory-validated feedback receipt values.
- FeedbackMetadata, DeviceTimestamp, FeedbackIngressSequence and FeedbackReceipt cannot be default-constructed around their validated creation paths; negative contract tests cover missing command/operation correlation, invalid evidence provenance and cross-runtime/cross-microscope receipt scopes.
- Feedback correlation supports unsolicited telemetry without fabricating OperationId/StepId, while operation feedback requires both and optionally carries CommandId. Device-session generation rejects callbacks from a previous device connection even when the microscope runtime remains active; Core receipt sequence is separate from adapter source sequence.
- Application static library contains runtime/operation handles and value projections; OperationSnapshot carries the full MicroscopeId/RuntimeId/RuntimeGeneration/OperationId identity, and OperationView is created only through a factory that validates this composite identity against its handle and returns a typed mismatch error. Handles are scoped to the runtime incarnation and are locator values, not authorization tokens.
- Static review corrections committed: direct standard-library includes in tests, explicit feedback enqueue outcome (Enqueued), validated device timestamp/timebase/clock-domain values, per-device-session generation, runtime-scoped positive Core ingress sequence/receipt factory, sequence assignment atomically with successful FIFO enqueue, no-wrap runtime-generation exhaustion rule, feedback fact correlation/provenance/enum validators, queue-overflow loss latch plus independent runtime wake contract, full composite OperationSnapshot identity, factory-only OperationView creation, [[nodiscard]] Result/Status and exact C++17 mode with extensions disabled.
- Focused Core identity/result/diagnostic tests, feedback contract tests and Application runtime-handle tests are registered with CTest.
- The current AmScope camera driver and Camera::Common targets remain separate and unchanged.

**Remaining Phase 1 work:**
- Review unit value conventions and ensure no premature general-purpose dimensional framework is introduced.
- Complete static review of public headers, CMake dependency direction, API naming and source compatibility.
- Build each library independently and then through the root project.
- Run focused Core/Application contract tests and inspect generated dependency graphs.

The implementation bullets below define the full Phase 1 intent; the committed source progress above must not be mistaken for phase completion.
- Implement the minimum independently buildable Libraries/Microscope.Core and Libraries/Microscope.Application C++17 targets.
- Make Application depend on Core; keep Core dependent only on the C++ standard library.
- Implement strongly typed opaque identity wrappers that validate caller-supplied values. Keep ID generation outside Core; add an injected identity-source contract only when a real entity/runtime creation use case first consumes it.
- Implement typed Result/Status, boundary-specific errors and Diagnostic conventions.
- Define feedback correlation metadata, source/provenance and runtime ingress contracts; do not invent device payloads before a real capability is modeled.
- Define the minimal immutable OperationHandle/snapshot and Application runtime-lifecycle boundary.
- Establish public-header naming, ownership, include and ABI conventions.
- Keep capability-specific command ports with the equipment/capability phase that first consumes them; do not create a universal device command interface.
- Keep JSON parsing, concrete adapter factories and full runtime composition for Phase 5.
- Keep the operation/step reducer, dependency scheduling, timeout and recovery execution for Phase 4.
- Ensure Core has no dependency on JSON, UI, OS APIs, vendor SDKs, Camera::Common or Application in its initial public API.

**Exit:** both targets build independently; Application links to Core and not vice versa; initial public contracts compile and have deterministic type/validation tests; feedback ingress metadata has contract tests. Full operation-state execution and simulated-device lifecycle tests remain Phase 4 acceptance, not a Phase 1 claim.

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

The initial Phase 1 source slice is committed. The next work is static review, build validation and focused contract validation in the user's development environment. Then address any findings before expanding the Core/Application API. Do not move into the full workflow executor, JSON loader, factory registry, microscope equipment catalog, device-specific command ports or acquisition persistence until their planned phases. Do not create empty directories or stub classes for future subsystems.
