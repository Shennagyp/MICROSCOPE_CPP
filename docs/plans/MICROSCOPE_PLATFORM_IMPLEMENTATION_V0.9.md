# Microscope Platform Implementation Plan v0.9

**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Authority:** docs/architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md and docs/architecture/CONTROLLER_STAGE_AND_HARDWARE_ADAPTER_CONTRACT_V1.md  
**Status:** Core equipment contracts and the Controller Stage OOP model exist. Concrete physical stage adapters and host integration remain outstanding.

This plan is the current dependency-ordered implementation sequence. It supersedes earlier Phase 4 stage-navigation/canvas/carrier descriptions in this file. The previous Core working-surface canvas, fixed dual-slide carrier, dual-slide navigation state, Application stage navigation runtimes and their split persistence stores are retired. They must not be restored as compatibility dependencies.

## 1. Current ownership model

- Camera/ owns camera discovery, driver lifecycle, streaming, frame ownership and vendor SDK integration.
- Microscope.Core owns static installed-equipment specifications, microscope-domain values, backend-neutral command/feedback contracts and serialized operation primitives.
- Controller/ owns the live physical microscope mechanics model. MicroscopeInstance owns Stage; Stage owns LinearAxis/RotationAxis objects, StageConfigurations, N StageSlots, global machine position evidence and motion-plan lifecycle.
- Microscope.Application owns application persistence, Slide records, Slide-to-StageSlot assignment and registration persistence, and capability dispatch integration. It does not own another Stage model.
- Swift owns the acquisition workflow, image/tile organization, slide-folder persistence, gallery and application presentation.
- ImageProcessing remains a separate project.

## 2. Implemented baseline

### Core equipment and command contracts

Present:
- Installed equipment topology and component specifications.
- Stage physical axis metadata, axis roles, rotary-axis geometry and StageConfiguration/StageSlot specifications.
- Schema-v2 Stage configuration with nanometre linear geometry and radian rotation configuration.
- Typed motion commands, observations, home-reference facts, limits and faults.
- Existing IStageMotionPort contract and correlated operation runtime.

### Controller Stage

Present:
- MicroscopeInstance → Stage ownership.
- LinearAxis and RotationAxis runtime models.
- StageConfiguration with selected working axes/ranges and N StageSlots.
- Slot-relative and absolute movement planning, home, refreshPosition and restore-position transaction.
- Derived currentSlotId.
- Exact pending-plan identity checks and correlated completion/failure handling.
- Translator from Controller plans to Core HomeAxes/MoveAxesTo commands.
- Contract tests for configuration, coordinate conversion, plans, homing and refresh lifecycle.

### Application Slide association and persistence

Present:
- Existing physical Slide aggregate/revisions.
- StageSlotAssignments for sparse SlideId/revision to StageSlotId association.
- StageSlotRegistration for measured four-corner transforms tied to the exact Slide revision.
- StageSlotAssignmentsSqliteStore for append-only assignment/registration snapshots.
- StageMotionCommandDispatcher and StageMotionPlanDispatcher as command dispatch integration.

## 3. Next work: concrete Stage adapter

Create a controller-specific implementation under Controller/Adapters/<controller>. It must implement the existing IStageMotionPort; no second Stage type or driver interface is required.

Implementation sequence:
1. Record exact controller model, firmware, host API/protocol and transport.
2. Define StageAxisId → physical controller channel mapping for all installed linear/rotary axes.
3. Record measured mechanical travel, safe limits, scale/encoder conversion, axis direction and backlash.
4. Specify the exact homing sequence per axis, including switch/index type, polarity, search direction, backoff, latch, offset and safe speed.
5. Build a deterministic fake controller/protocol harness for unit conversion, sign mapping, bounds, coordinated moves, home and failure cases.
6. Implement device/session lifecycle and axis binding.
7. Implement moveAxesTo, controlled stop, homeAxes, normalized completion, readback, limit and fault feedback.
8. Add physical hardware smoke tests for each axis, homing, full travel, coordinated motion, repeatability, readback and stop/fault behavior.

Adapter rules:
- Device-unit/step/encoder scaling occurs exactly once in the adapter/controller chain.
- The adapter preserves the supplied runtime/device/operation correlation.
- Submission Accepted is not physical completion.
- Home requires exact HomeAxes completion and a complete normalized StageHomeReferenceObservation.
- A partial or indeterminate move must not be reported as complete.
- stopAxes is controlled stop, not a safety-rated E-stop.

See CONTROLLER_STAGE_AND_HARDWARE_ADAPTER_CONTRACT_V1.md for the required physical inputs and full feedback contract.

## 4. Required Controller integration alongside the adapter

The current Stage API updates command-derived position after exact completion and establishes global origin after normalized homing evidence. It does not yet expose a complete per-axis measured-position/uncertainty reconciliation API.

Implement:
- A Controller-owned or explicitly host-owned serialized feedback reconciliation path.
- Per-axis position state retaining measured value, evidence source/provenance and uncertainty.
- Fault/limit/feedback-loss semantics, including partial moves and reconnects.
- Conservative position invalidation/reconciliation rules for timeout, ambiguous submission, late callback and device-session replacement.
- An accumulated positioning-error model only where it is grounded in measured motion behavior; do not treat controller-reported coordinates as proof of physical accuracy.

The adapter can be developed against IStageMotionPort while this work proceeds, but closed-loop position feedback must not be treated as authoritative until the Stage reconciliation path exists.

## 5. Stage calibration

Stage calibration is separate from refreshPosition:
- refreshPosition performs a home-and-return position-reference transaction.
- calibration measures physical relationships such as scale, repeatability, backlash/approach behavior and uncertainty.
- calibration must produce explicit revisioned measurement evidence and remain independent of StageSlot equipment geometry.
- a calibration procedure must not be implemented by pretending that homing removes scale error or backlash.

## 6. Host composition

After the concrete adapter and feedback owner exist:
1. Load a schema-v2 installed equipment definition and select a valid StageConfiguration.
2. Construct one long-lived Controller::MicroscopeInstance per controlled physical microscope.
3. Bind one long-lived IStageMotionPort adapter and its DeviceId.
4. Retain the Stage, adapter and feedback owner for the microscope runtime lifetime.
5. Publish immutable position/motion snapshots through a narrow host bridge.
6. Keep SwiftUI as producer/observer. Do not recreate stage state, coordinates, homing or navigation in Swift.

No dummy equipment definition or fake driver should be installed into production composition.

## 7. Independent remaining platform work

- Preserve the Camera-v2.0 architecture and integrate it as a controlled camera capability change; do not merge camera SDK types into Core or Controller.
- Keep objective/illumination/optical adapters separate from the stage adapter and preserve their own command/feedback lifecycles.
- Maintain equipment and calibration revision provenance.
- Keep acquisition, Slide image/tile data, file persistence and gallery in Swift.
- Keep ImageProcessing in its separate project.

## 8. Acceptance gates

A stage adapter is not hardware-ready merely because it compiles. Require separate evidence for:
- Protocol-level fake tests: units, signs, axis mapping, range rejection, coordinated commands, stale sessions, partial failures and timeouts.
- Controller contract tests: exact plan/correlation, completion, home-reference and position invalidation semantics.
- Hardware tests: actual travel, homing repeatability, position readback, directional backlash, motion accuracy, stop and fault recovery.
- Host integration: one long-lived MicroscopeInstance, adapter and feedback owner; no duplicate stage authority.
