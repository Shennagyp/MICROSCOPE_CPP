# Microscope Platform — v0.9 Implementation Plan

**Architecture reference:** ../architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Status:** Phases 1 and 2 completed; Phase 3 declarative configuration in progress. The equipment model and all registered contract tests passed on Ubuntu GCC, macOS Clang and Windows MSVC. [Latest Phase 2 validation](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45595205508).

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

**Completed — 2A through 2E validated**

#### 2A — Installed equipment identity and optical topology

- Added strong IDs for installed equipment components, optical ports, optical connections and optical paths.
- Added factory-created immutable OpticalPort, EquipmentComponent, OpticalConnection, OpticalPathStep and OpticalPath values.
- Added MicroscopeEquipmentDefinition as the immutable aggregate for one installed microscope definition.
- Component kind constrains port capability: optical elements need input/output capability; imaging devices need input; illumination devices need output; mechanical assemblies have no optical ports.
- Physical OpticalConnection is undirected adjacency between ports on different components. Ordered OpticalPathStep values define propagation direction. One physical connection can be reused in opposite directions when port capabilities permit.
- Shared components are stored once and referenced from multiple optical paths. The tests cover a shared condenser and beam splitter with alternative camera output paths, and one physical adjacency traversed in opposite directions.
- Validation covers duplicate/foreign identities, port ownership/direction, one connection per physical port, same-component connection rejection, required interior path ports and route continuity.
- Cross-platform CI passed: [run 22](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45587716888).

#### 2B — Objective and camera specifications

- Added distinct positive finite domain values for micrometers, nominal magnification and numerical aperture; no general-purpose units framework.
- ObjectiveSpecification attaches to the installed EquipmentComponentId and stores magnification, NA, working distance, chromatic/field correction, immersion medium and optional design coverglass thickness.
- CameraSpecification attaches to the installed imaging component and stores active sensor dimensions, independent X/Y pixel pitch and unique output modes.
- CameraOutputMode stores sensor ROI, binning, delivered dimensions, pixel layout and stable pixel-format code. Net sensor-pixels-per-delivered-pixel X/Y mapping is derived independently from ROI and delivered dimensions; hardware binning remains a separate fact.
- Aggregate validation rejects unknown/wrong-kind component references and duplicate specialized records.
- Tests cover NA above 1, finite/positive constraints, anisotropic pitch/mapping, ROI/binned output, invalid pixel modes, duplicate modes and invalid component associations.
- Cross-platform CI passed: [run 33](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45589429447).

#### 2C — Illumination and condenser specifications

- IlluminationSpecification attaches to an installed IlluminationDevice and supports multiple independently identified channels.
- Each channel may have an optional spectral band, an intensity-control kind and a unit-bearing intensity range. Continuous control requires a finite non-negative increasing range.
- CondenserSpecification attaches to an installed OpticalElement and stores aperture NA range (minimum may be zero), design, immersion medium and optional working distance.
- Aggregate validation rejects unknown/wrong-kind references and duplicate specifications.
- Tests cover spectral ranges, intensity units/control semantics, multiple channels, condenser aperture bounds and invalid roles.
- Cross-platform CI passed: [run 46](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45590795457).

#### 2D — Stage and axis specifications

- StageSpecification attaches to an installed MechanicalAssembly and contains one or more StageAxisId values.
- Axis specifications distinguish linear/rotary/other/unknown motion and micrometer/millimeter/degree/radian/device-defined/unknown coordinate units.
- Each axis stores a finite signed operating range and positive nominal resolution.
- Optional accuracy, repeatability/precision and measurement uncertainty are separate values. Accuracy/repeatability preserve test conditions; repeatability preserves its metric. No metric is derived from resolution.
- Position-feedback capability and measurement location are explicit; no-feedback axes cannot claim a measurement location.
- Tests cover linear/rotary units, signed ranges, unit mismatches, independent performance values, test conditions, feedback semantics and stage-axis identity.
- Cross-platform CI passed: [run 58](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45592166839).

#### 2E — Complete definition validation

- MicroscopeEquipmentDefinition validates non-empty installed component sets; uniqueness of component, port, connection and path IDs; component and port ownership; path entry/exit direction; adjacency continuity; and physical component sharing.
- Objective, camera, condenser, illumination and stage specifications bind to installed component identities of the correct broad component kind.
- Duplicate specialization records are rejected. One physical OpticalElement cannot simultaneously be classified by both ObjectiveSpecification and CondenserSpecification.
- Stage axes are unique within their stage; camera output modes and illumination channels are unique within their owning device specification.
- Tests cover route reversal across one shared physical connection and conflicting optical specialization roles.
- Full Core standalone, Application standalone and root platform-only build/test matrices passed on Ubuntu GCC, macOS Clang and Windows MSVC: [run 72](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45595205508).

**Phase 2 boundary**
The equipment definition now describes installed topology and static capabilities/specifications. It does not store current device state, selected objective, aperture position, current illumination intensity, camera online status, calibrated positions or adapter-specific bindings. Those belong to configuration/runtime/calibration/deployment phases.

**Exit:** satisfied. Phase 2 equipment model and contract validation are complete. JSON parsing remains Phase 5; calibration remains Phase 6; operation execution remains Phase 4.

### Phase 3 — Declarative configuration

**In progress — 3A–3C implemented and cross-platform validated; 3D transition description next**

**Objective:** resolve a requested microscope configuration from installed equipment and declarative profile data using deterministic pure functions. The resolver must produce a validated desired configuration, diagnostics and a transition/dependency description; it must not communicate with hardware or execute transitions.

**Modeling sequence**
1. **3A — Configuration vocabulary:** objective selection, optical path, contrast method, camera output mode, illumination channel/preset, condenser configuration, phase/DIC/polarization components, exposure preset and exposure policy.
2. **3B — Profile applicability:** declarative profile requirements, selectors and compatibility constraints over installed component identity/capability data. Profiles must not contain C++ class names, SDK calls or executable callbacks.
3. **3C — Pure resolution:** request + equipment definition + profile catalog → resolved desired configuration + typed diagnostics. Current/effective state comparison is not part of this pure target-compatibility step.
4. **3D — Transition description:** derive required component selections/settings and dependency ordering, including conflicts and safe preconditions. This is a value plan only; Phase 4 executes it.
5. **3E — Representative data-driven profiles:** brightfield, phase contrast, DIC and polarization cases, including unavailable/mismatched installed equipment.


**Phase 3A committed source progress — cross-platform CI passed**
- Added ConfigurationProfileId, immutable ConfigurationProfile and ConfigurationProfileCatalog values. Profiles declare compatible installed objective/condenser/path/camera/illumination identities and optional required path components.
- Added immutable ConfigurationRequest with explicit contrast method, installed component selections, one-or-more unique optical path IDs, camera output mode, illumination channel and explicit exposure policy.
- Added pure ConfigurationResolver over typed equipment/profile/request values. It validates profile membership, installed specifications, camera mode/channel availability and component membership across the union of selected paths.
- Multiple paths are supported by design; a split illumination route and imaging route can jointly satisfy one configuration without requiring all components on every path.
- Resolution returns either the immutable accepted request or stable Error diagnostics. It does not access current runtime state, issue hardware commands or claim hardware completion.
- Added contract tests for a valid brightfield path, separate illumination/imaging paths, unavailable camera modes/channels, profile conflicts, missing paths, required path components and invalid request/profile/catalog values.
- Cross-platform CI passed on Ubuntu GCC, Windows MSVC and macOS Clang for Core standalone, Application standalone and root platform-only builds/tests: [GitHub Actions run 85](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45597867770).

**Phase 3B committed source progress — cross-platform CI passed**
- Added OpticalElementFunction and OpticalFunctionCapability on installed optical components, including phase plate/annulus, polarizer, analyzer, DIC prism, excitation/emission filters, dichroic mirror and darkfield stop.
- Added optional OpticalCompatibilityId on capabilities so mating optical components can be matched by data rather than class behavior.
- Added OpticalFunctionRequirement to profiles with optional compatibility identity and minimum distinct-component count. Duplicate function/compatibility requirements and zero counts are rejected.
- Extended ConfigurationRequest with explicit additional optical component selections. Duplicate selections and overlap with primary objective/condenser selections are rejected.
- Resolver verifies each additional selection is installed, is an OpticalElement, belongs to a selected path and satisfies at least one optical-function requirement in the selected profile.
- Resolver counts required functions across the selected objective, condenser and additional optical components. One physical component counts once; compatibility identity must match exactly when specified.
- Added split-path data-driven phase-contrast and DIC tests. Phase plate/annulus may lie on the illumination/condenser path, while downstream DIC prism/analyzer and camera components are validated over the imaging path. DIC requires a matching polarizer, analyzer and two prisms.
- Negative tests cover mismatched compatibility identities, insufficient prism count and unrelated additional optical component selection.

**Phase 3C committed source progress — cross-platform CI passed**
- Added CameraSpecification exposure-time capability range as an optional installed camera capability, expressed in positive microseconds.
- Added profile target values for condenser aperture NA, unit-bearing illumination intensity and an initial exposure preset.
- Resolver validates aperture against the selected condenser range, illumination target against the selected channel's continuous range and exact unit, and exposure preset against the selected camera's declared exposure-time range.
- Missing exposure-time capability data rejects a profile preset rather than treating the camera as unlimited.
- Added microscope-scoped ResolvedConfiguration, retaining MicroscopeId, immutable request, optional condenser aperture target, optional illumination intensity target and optional exposure preset.
- The request's ExposurePolicy remains separate. A retained preset does not issue a command or overwrite Manual/Automatic/ConstrainedAutomatic.
- Added tests for valid retained targets, aperture/intensity/exposure out-of-range values, intensity unit mismatch, missing camera exposure capability and microscope-scoped resolved output.
- Full Core standalone, Application standalone and root platform-only build/test matrix passed on Ubuntu GCC, macOS Clang and Windows MSVC: [GitHub Actions run 114](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45629930294).

**Implementation constraints**
- Objective, contrast method, optical path, camera mode and illumination remain independent dimensions. The resolver combines them; behavior is not implemented in Objective4x/PhaseObjective/DICObjective subclasses.
- Objective selection may select an initial exposure preset, but ExposurePreset and ExposurePolicy are distinct. Manual/automatic/constrained-auto policies must not be overwritten implicitly by a profile.
- Phase contrast requirements describe the compatible phase ring/condenser arrangement. DIC requirements describe the required polarizer/analyzer/prism arrangement and their optical route. The installed definition determines whether the arrangement exists.
- Profiles are data and constraints. Pure resolver functions are deterministic and testable without a microscope, JSON parser, frontend or adapter.
- Unknown/unspecified equipment data is not silently treated as compatible. Return explicit diagnostics when required information is absent.
- Resolver output must not claim hardware state was changed or verified. Transition execution and device feedback remain Phase 4.
- Keep JSON schema/loading in Application Phase 5. Phase 3 operates on typed Core values.

**Phase 3D next vertical slice — transition description**
- Add immutable transition-plan, transition-step and dependency-edge values with stable identities.
- Generate target requirements for selected objective/condenser/additional optics, optical path set, camera mode, illumination channel and optional profile settings.
- Express safe preconditions and dependency ordering as a DAG; do not invent fixed hardware command sequences or vendor operations.
- If a verified effective-configuration snapshot is supplied, omit only requirements already known to match. Unknown/unverified state must not be treated as a match.
- Keep the plan declarative and adapter-neutral. No device commands, retries, operation state machine or persistence are introduced in Phase 3D; those remain Phase 4.

**Phase 3A–3C exit:** satisfied. Pure target resolution and profile target validation are deterministic and cross-platform validated. **Phase 3D exit:** transition requirements and dependencies are represented as a deterministic adapter-neutral plan; execution remains Phase 4.

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

Phases 1 and 2 are closed. Phase 3A–3C are implemented and cross-platform validated. The current task is Phase 3D: define a small adapter-neutral transition-plan value model and deterministic dependency planner over ResolvedConfiguration, optionally compared with verified effective state. Do not introduce commands, executors or a universal workflow engine; those belong to Phase 4.
