# Microscope Test Strategy and Implementation Gap Register — v0.9

**Branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Purpose:** Define what the current unit and integration suites prove, identify weak or absent subsystems, and prevent a simulated adapter test from being reported as physical controller verification.

## 1. Testing contract

A passing unit test proves only the behavior exercised by that test. A simulated-port integration test proves the orchestration across the simulated boundary. Neither proves the vendor SDK, physical controller, motor wiring, mechanical limits, optical selector positions, illumination output, or camera sensor behavior.

Tests are therefore classified as:

1. **Core unit/contract:** deterministic domain values, validation, state transitions, geometry, resolvers and planners.
2. **Application component:** dispatchers, reducers, persistent owners, queues, workers and persistence adapters.
3. **Workflow integration:** more than one real subsystem contract joined through its intended interfaces. Device adapters may be simulated only when explicitly named as simulated.
4. **Hardware-in-the-loop (HIL):** the concrete controller/driver, real device, feedback and physical behavior are exercised. This is a separate evidence gate.
5. **Application acceptance:** the actual composition root, native bridge, UI commands, persistence and device lifecycles are exercised together.

Do not rename a Core unit test a system test. Do not describe a mock or simulated port as a controller implementation. Do not infer hardware success from CMake target registration or source compilation.

## 2. Current test inventory

The branch contains these registered test groups in the subproject CMake files:

| Test group | Registered targets | Build condition |
|---|---:|---|
| Microscope.Core | 22 | Root build |
| Microscope.Application | 17 | Root build |
| Camera.Common | 5 | Root build |
| Camera.Amscope | 7 | `OPTIKA_BUILD_AMSCOPE=ON` |
| Camera.Qt | 1 | Qt-enabled AmScope build |
| Camera.QtWidgets | 3 | Qt-enabled AmScope build |
| Native OPTIKA B293 app | 6 | `OPTIKA_BUILD_NATIVE_APP=ON`, macOS, Swift and Objective-C++ |

The root default enables Camera.Amscope and leaves the native app disabled. The always-added root baseline is 44 registered targets (22 Core + 17 Application + 5 Camera.Common). Optional subprojects add their registered targets when their build conditions are met.

The six native-app tests cover automatic resolution policy, workspace/dock behavior, microscope calibration data, image geometry and preview frame-rate measurement. They do **not** currently exercise `NativeCameraController`, `OptikaB293Composition` or actual device command orchestration.

The Core/Application groups cover equipment topology, configuration compatibility, camera suitability, mode/calibration contracts, stage geometry and navigation, operation/feedback lifecycle, SQLite stores, camera model discovery enrichment and serialized runtime integration.

The AmScope tests remain separate and must be preserved. They cover the camera backend, session/frame primitives and Qt request/stream components; they do not cover the complete native `NativeCameraController` owner or the missing stage/optical hardware adapters.

## 3. Workflow integrations added or strengthened

### 3.1 Optical desired-configuration workflow

`Libraries/Microscope.Core/tests/MicroscopeOpticalConfigurationWorkflowIntegrationTests.cpp`

Exercises this exact implemented chain:

~~~text
MicroscopeEquipmentDefinition
  -> selected LightPath
  -> camera endpoints derived from Imaging route terminals
  -> OperationMode / equipment revision binding
  -> LightPathCameraResolver
  -> LightPathConfigurationRequestFactory
  -> ConfigurationProfileCatalog
  -> ConfigurationResolver
  -> ConfigurationTransitionPlanner
~~~

It checks the connected camera/output-mode choice, selected route provenance, microscope/equipment/mode revision binding, compatibility resolution, transition DAG structure, disconnected-camera rejection and stale equipment-revision rejection.

**Boundary:** the transition planner produces declarative requirements and dependency layers. This test does not execute an objective changer, filter wheel, condenser, illumination source or camera command. No optical transition executor is currently implemented.

### 3.2 Stage owner/worker integration

`Libraries/Microscope.Application/tests/StageWorkingSurfaceNavigationWorkerIntegrationTests.cpp`

Exercises:

~~~text
immutable slide-local target
  -> bounded control queue
  -> serialized owner worker
  -> persistent stage navigation runtime
  -> StageMotionCommandDispatcher
  -> injected SimulatedStageController (IStageMotionPort)
  -> correlated asynchronous completion feedback
  -> Core operation runtime
  -> command-derived XY estimate / verification
  -> terminal operation snapshot
~~~

It proves that the application queue, owner-thread binding, runtime session, typed port and asynchronous feedback can be composed through the test adapter.

**Boundary:** `SimulatedStageController` is deliberately a test double. It is not a physical stage driver and does not implement controller communication, pulse generation, position readback, safety interlocks or real homing.

### 3.3 Stage verification policy unit tests

`Libraries/Microscope.Application/tests/StageWorkingSurfaceNavigationVerificationPolicyTests.cpp`

Covers valid bounded-estimate and device-observation modes, per-axis tolerance validation, finite-value constraints, non-negative home uncertainty and invalid enum values.

### 3.4 Stage runtime weak-point tests

`Libraries/Microscope.Application/tests/StageWorkingSurfaceNavigationRuntimeTests.cpp`

The expanded runtime suite explicitly challenges:
- A correlated controller following-error during XY movement.
- Preservation of independent focus-Z reference when XY becomes uncertain.
- Rejection of a second movement while another movement is active.
- Rejection of working-surface/registration replacement during motion.
- Acceptance of a new registration only after motion terminates.
- Stale command completion cannot complete the active command.
- A stale queued target cannot move against a superseded slide-registration revision.
- XY homing and target restoration do not fabricate focus-Z state.
- One measured axis cannot verify a coordinated two-axis move.
- A measured but off-target position remains recorded as measured evidence while the operation fails.
- Adapter rejection, pre-dispatch failure, indeterminate submission, partial completion and retry.

### 3.5 Stage dispatcher command coverage

`Libraries/Microscope.Application/tests/StageMotionCommandDispatcherTests.cpp`

Covers the port dispatch boundary for `MoveAxesTo`, `MoveAxesBy`, `HomeAxes` and controlled `StopAxes`, including accepted/rejected/throwing submission semantics and stale device sessions.

**Important:** dispatching a controlled stop is not the same as cancelling and verifying an active navigation. The current stage-navigation owner does not expose a complete cancel/stop lifecycle, does not allocate a correlated stop operation for an active move, and has no timeout/deadline mechanism to reconcile a command that never completes.

## 4. Implementation coverage versus missing evidence

| Subsystem | Implemented now | What remains missing |
|---|---|---|
| Optical topology | Typed components/ports, physical connections, ordered optical paths, LightPaths and derived camera endpoints | Full equipment constitution persistence and optical-element physical state/plane modelling are incomplete |
| Camera model discovery | Shared model catalog, equipment component binding, discovery enrichment and suitability logic | Complete active camera-session composition with selected LightPath, configuration and acquisition workflow |
| AmScope camera | Existing native camera driver/controller, feature access, frame pipeline and preview path | Preserve current behavior; do not replace it with the simulated stage test harness |
| Microscope configuration | Profiles, selection-driven validation, LightPath camera resolution/factory, OperationMode and SQLite persistence | A whole-configuration candidate solver, composable multi-technique mode semantics and complete resolved hardware state |
| Configuration transition | Validated declarative DAG with typed requirements and dependency ordering | Runtime executor, per-device command adapters, readback/evidence reconciliation and recovery |
| Illumination/calibration | Typed source/channel specifications, irradiance calibration model, exact revision binding and SQLite persistence | Concrete source controller, channel control, physical meter workflow and actual intensity readback |
| Stage Core | Axis/stage specifications, 2D working surface, slide transforms, XYZ position state, axis reference calculations, navigation planners and typed motion feedback | Complete focus-Z command/reference runtime; complete physical stage/controller configuration model |
| Stage Application | Owner-thread navigation runtime, request queue, worker integration, XY command dispatch and feedback verification | Physical controller adapter; runtime cancellation/stop/timeout lifecycle; bridge from native composition |
| Stage hardware | Abstract `IStageMotionPort` contract only | No concrete controller/driver implementation was found in this branch |
| Acquisition | Existing camera preview/frame delivery and image-save UI | No complete persisted AcquisitionPlan/AcquisitionRun/AcquisitionField domain and execution pipeline |
| Slide/SampleType | Core versioned requirements model; Application SQLite store appends immutable SampleType and Slide revisions, maintains current pointers, and enforces the exact SampleTypeId/revision foreign key | Native workspace loading/editing, user-facing save workflows and migration of any legacy external records remain unimplemented |
| Native application composition | Camera, optics Swift models/coordinators, workspace/gallery | Swift composition does not yet construct/use the C++ stage owner/runtime or a physical stage adapter |
| HIL/application acceptance | None established by this test change | Real stage, focus, objective/filter/illumination hardware; native composition; persistence/recovery; operational safety acceptance |

## 5. Controller completeness: exact answer

The stage **domain and XY navigation logic** is substantial: the stage has axis and geometry models, position/reference state, a motion port contract, typed commands, correlated feedback, operation state, a long-lived XY navigation owner, homing/restore behavior, measured/estimated verification and bounded command ingress.

The stage **physical controller subsystem is not complete**. There is no concrete stage adapter in this repository and no composition-root wiring to a real controller. In particular, this branch does not yet demonstrate:
- A vendor/controller protocol implementation and connection/session lifecycle.
- A controller-specific axis mapping and calibrated engineering-unit ↔ native-command conversion.
- Configured velocity, acceleration, jerk, motor/drive limits and controller-level soft/hard limits.
- Concrete homing procedure selection and controller switch/encoder-index handling for each installed axis.
- Focus-Z command/reference execution integrated with the native application.
- Active navigation cancellation, controlled-stop verification, timeout/deadline handling and late-completion reconciliation.
- Preservation/exposure of controller-fault kind and affected axis in the public terminal operation result; the current navigation runtime terminalizes a correlated adapter failure but does not retain the typed fault detail in its update.
- Device disconnect/reconnect behavior against a real controller.
- Hardware-in-the-loop tests of accuracy, repeatability, backlash, travel bounds, limit switches and stop latency.

The `AxisSpecification` is a static capability/performance model. It is not a motor-driver/controller configuration. The driver owns controller-specific details such as pulse generation or native coordinate conversion, but an implementation must exist before those details can be tested.

The optics subsystem also has domain models and planning logic, but no complete optical hardware controller/executor. The camera subsystem is further developed because the AmScope native controller is an actual implementation.

## 6. Required negative and failure testing before hardware claims

The next implementation slices must add tests at the layer that owns the behavior:

### Stage controller adapter contract
- Reject malformed axis maps, unsupported units, non-finite targets and targets outside configured software limits before transport.
- Convert physical coordinates to native controller units with explicit calibration and round-trip error bounds.
- Verify command/axis/session correlation on every response; stale generations must not update current state.
- Inject communication loss before submission, after submission with unknown outcome, during motion and after controller completion.
- Distinguish accepted, acknowledged, action-completed, readback-observed and verified.
- Test per-axis homing against the selected method, endstop polarity, encoder/index behavior and maximum reference uncertainty.
- Test limits, following error, emergency-stop input, controlled stop and late feedback.
- Test all axes independently and concurrently, including focus-Z isolation from XY navigation.

### Optical configuration executor
- Test selector commands and verified selector position readbacks.
- Test filter/objective/condenser incompatibility, a selector that stops between detents, absent feedback and a changed equipment revision mid-transition.
- Test source/channel selection, calibrated irradiance interpolation, no extrapolation and readback mismatch.
- Test failure at each dependency layer and verify recovery does not report the desired configuration active prematurely.

### Acquisition workflow
- Test true device still/capture separate from preview-frame save.
- Test run identity and recovery checkpoint persistence before hardware commands.
- Test actual settings/readbacks versus requested settings, frame integrity, missing frames, duplicate delivery and interrupted acquisition.
- Test camera disconnect/reconnect and partial multi-camera/synchronized capture only when the corresponding controller capabilities are implemented.

### SampleType and Slide contract
Implemented in the Core model and test sources:
- SampleType carries versioned microscopy requirements and optional reusable slide-preparation requirements.
- Each Slide directly stores SampleTypeId and the exact SampleType revision, plus its own geometry, observed preparation facts and optional OriginLabId.
- Multiple independent Slides can reference the same SampleType revision; no Sample entity, SampleId, SlideType or SlideTypeId exists in the revised Core contract.
- The assessor accepts a Slide directly, rejects a mismatched pinned SampleType revision, and evaluates actual Slide preparation facts against the SampleType requirements.
- Unit coverage checks independent Slides, absent optional OriginLabId, version pinning, incomplete observed facts, invalid preparation ranges, and fluorescent-label metadata.
- Integration coverage follows LightPath selection through resolved microscope configuration into SampleType/Slide assessment; valid, off-spec and stale-revision cases are represented.

Persistence is implemented in the Application layer:
- `SampleSlideSqliteStore` uses independent, namespaced schema metadata and append-only `sample_type_revision` and `slide_revision` tables.
- `sample_type_current` and `slide_current` pointers are updated in the same `BEGIN IMMEDIATE` transaction as the revision insert.
- `slide_revision` has a composite foreign key to the exact `(sample_type_id, sample_type_revision)` row. A Slide cannot be saved against merely the latest SampleType when its pinned revision is absent.
- Revision writes use compare-and-swap `expectedCurrentRevision`; revision rows reject direct UPDATE and DELETE through database triggers.
- Versioned, bounded little-endian payload codecs round-trip complete objective/camera/condenser/illumination/technique/preparation requirements, Slide geometry and measured preparation, fluorescent excitation/emission metadata, emission spectrum, filter-set compatibility identity and optional OriginLabId.
- Tests cover historical revision retrieval, full model round-trip, exact-revision foreign-key rejection, stale writers across independent connections, direct SQL mutation/deletion rejection, reopen/recovery and no partial Slide record on failed writes.

Still outstanding:
- Native workspace loading/editing and application-level save/dirty lifecycle for Slides and SampleType definitions; the store is not yet wired into Swift composition.
- Migration tooling for any legacy external Sample/Specimen/SlideType data. This store introduces a new versioned schema but does not import legacy records.
- Schema/payload compatibility is versioned; a future payload change must add an explicit migration or a new payload format version rather than reinterpret format 1.
- No patient identity, Sample instance identity or patient relationship is part of this microscope model.

## 7. Execution policy and result reporting

The test sources and CTest registrations have been authored. They have **not been compiled or executed as part of this change**. Therefore:
- No test is reported as passing.
- No C++ compile status is claimed.
- No hardware behavior is claimed.
- Newly added integration tests are evidence of designed test paths only until compiled and run in the project build environment.

When executed, report test results by target name, compiler/build configuration and commit SHA. HIL evidence must separately record controller/firmware identity, axis mapping/calibration, mechanical setup, test conditions, measurements and acceptance tolerances.
