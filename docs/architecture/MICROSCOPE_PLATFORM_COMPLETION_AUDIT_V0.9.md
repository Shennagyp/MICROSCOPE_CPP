# Microscope Platform Completion Audit v0.9

**Repository:** Shennagyp/MICROSCOPE_CPP  
**Branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Audit focus:** current C++ Core, Controller, Application and hardware-adapter boundary  
**Status:** source audit for the current branch; build/test status must be taken from the CI run associated with the latest commit.

## 1. Architectural decision

The physical stage runtime is owned by Controller/, beside Camera/. Controller::MicroscopeInstance owns Controller::Stage. Stage owns the installed LinearAxis and RotationAxis runtime objects, its available StageConfigurations, active configuration, global machine position evidence, derived current StageSlot and pending motion-plan lifecycle.

This replaces the previous split between Core StageWorkingSurface2D/DualSlideNavigation and Application stage navigation runtimes, reducers and canvas/carrier persistence. Those models are retired; they are not migration layers or alternate stage authorities.

The remaining stage-related Application model is intentionally narrower:
- StageSlotAssignments maps existing Slide identities/revisions to physical StageSlot identities.
- StageSlotRegistration stores measured four-corner Slide-to-StageSlot transforms.
- StageSlotAssignmentsSqliteStore persists immutable assignment/registration snapshots and references the existing Slide revision store.

The application owns Slide. Controller owns Stage. A StageSlot is a physical fixture region, not a Slide.

## 2. Current implementation inventory

### Microscope.Core

Implemented:
- Strong identities and equipment graph.
- Static installed Stage specification with linear/rotary axis metadata, axis roles and rotation geometry.
- Versioned StageConfiguration specifications selecting installed axes and owning N StageSlots.
- Validation of effective ranges against mechanical travel, StageSlot fit inside configured XY ranges, duplicate IDs and slot overlap.
- Canonical nanometre Stage geometry and radian rotation ranges for schema v2.
- Backend-neutral axis command, action-completion, position-observation, home-reference, limit and fault value types.
- IStageMotionPort as the driver-facing contract.

Not a runtime stage owner:
- Core retains equipment specifications and generic motion contracts. It does not contain a second live Stage position or slide-navigation state.

### Controller/Common

Implemented:
- MicroscopeInstance owns one live Stage and the runtime stage DeviceId binding.
- Stage::fromEquipmentSpecification one-way projection from a validated equipment Stage into runtime mechanics.
- LinearAxis and RotationAxis runtime objects with separate mechanical range, minimum increment, accuracy, precision and measurement uncertainty.
- StageConfiguration with selected working axes, effective ranges, optional rotation and N StageSlots.
- StageSlot with global XY origin and local X/Y usable ranges.
- Stage moveTo(slot-relative), moveTo(absolute), moveBy, home, refreshPosition and restoration operations.
- Derived currentSlotId; no separately mutable slot identity.
- Correlated motion plans, exact plan identity checks and serialized pending-plan ownership.
- Completion and failure reconciliation; uncertain movement invalidates position.
- Homing requires exact HomeAxes completion plus complete normalized StageHomeReferenceObservation.
- StageMotionCommandTranslator verifies controller/equipment identity and translates plans into existing Core HomeAxes/MoveAxesTo commands.

Known limits:
- Stage calibration is separate from refreshPosition and is not yet implemented as a complete record/procedure lifecycle.
- The runtime currently distinguishes Unknown, HomingReference and CommandDerived position evidence.
- The Stage API does not yet expose complete per-axis measured-position, source/provenance and uncertainty reconciliation from AxisPositionObservation.
- There is no physical adapter under Controller/Adapters yet.

### Microscope.Application

Retained:
- Slide and SampleType records/revisions.
- StageSlotAssignments and StageSlotRegistration as application-owned Slide occupancy/registration data.
- StageSlotAssignmentsSqliteStore with immutable revision snapshots and Slide revision references.
- Existing correlated StageMotionCommandDispatcher and StageMotionPlanDispatcher as dispatch integration; neither owns Stage position or navigation policy.

Removed/retired:
- Fixed left/right carrier state and DualSlideCarrierSqliteStore.
- StageWorkingSurface2D/CAD canvas and StageWorkingSurfaceSqliteStore.
- DualSlideNavigation and StageNavigationPlanner.
- StageWorkingSurfaceNavigationPlanner.
- Application StageNavigationRuntime/Executor/ControlQueue and the older StagePosition reducers.
- SlideCornerCorrespondence and SlideRegistrationWorkingSurfaceComparison, which joined the retired carrier/canvas pair.

The C++ Application layer must not recreate a stage coordinate system, stage position aggregate, StageSlot geometry model or navigation planner.

## 3. Equipment definition and units

Schema v2 is the canonical runtime equipment format:
- Linear axis coordinate unit, operating range, nominal resolution and axis tolerances use nanometres.
- Rotation axis travel and nominal resolution are specified in their declared angular unit and normalized to radians in Controller.
- Rotation geometry specifies a point on the global Stage axis and a cardinal axis direction.
- Every runtime configuration provides working X, Y and Z ranges and may select rotation axes.
- Every configuration owns N StageSlots.

Schema v1 remains a legacy import path for migration. Its stage description lacks the complete configuration/slot model; it cannot be used to construct a runnable Controller::Stage until migrated to schema v2. The fixed dual-slide carrier field is not part of the accepted equipment contract.

## 4. Adapter readiness

The existing IStageMotionPort contract is sufficient to begin implementing a concrete low-level adapter. It exposes moveAxesTo, moveAxesBy, homeAxes and stopAxes and sends normalized typed feedback through a bounded ingress.

The adapter owns device communication, logical axis-channel binding, device-unit/step/encoder conversion, axis direction, homing procedure, trajectory generation, readback, limits, faults, timeouts and device-session lifecycle. It must preserve runtime/device/operation correlation. Submission acceptance is not movement completion. Home requires both exact action completion and normalized reference evidence.

Before physical adapter implementation is completed, the actual controller information must be supplied:
- Exact controller model, firmware and host API/protocol.
- Connection/interface and physical channel topology.
- Mapping of StageAxisId values to controller axes.
- Mechanical travel and safe software limits.
- Steps/mm, encoder counts/mm or controller-native units.
- Axis polarity/direction.
- Home sensor/index type, polarity, search direction, backoff, latch and offset.
- Velocity/acceleration limits and multi-axis coordination behavior.
- Position readback, status, limit and fault availability and units.
- Measured backlash/approach behavior.
- Stop, drive-disable, E-stop and reconnect semantics.

Do not guess scale factors or infer them from a motor's nominal step angle.

## 5. Remaining work before production motion

1. **Concrete hardware adapter.** Create Controller/Adapters/<controller> implementing IStageMotionPort. Start with a fake protocol harness and validate conversion/sign/bounds before hardware tests.
2. **Measured feedback reconciliation.** Add a Controller Stage API that consumes validated per-axis observations and preserves evidence source and uncertainty. Define how partial moves, feedback loss, controller disconnect, late feedback and timeouts affect position validity.
3. **Stage calibration lifecycle.** Implement measured mechanical calibration as a separate model/record from refreshPosition; include calibration revision and evidence where required.
4. **Host composition.** Create and retain MicroscopeInstance once per controlled microscope, bind the adapter DeviceId/port and provide one serialized feedback owner. Do not create stage policy in Swift or duplicate it in Application.
5. **Hardware verification.** Verify each axis, home, limit, full travel, coordinated movement, readback, repeatability, backlash and stop/fault behavior on the physical stage.

## 6. Verification boundary

CMake target presence and unit-test source presence are not evidence of a successful build. CI run status must be checked on the exact branch commit. A passing portable CI matrix verifies software compilation/contracts; it does not verify physical stage travel, homing repeatability, accuracy or safety behavior.

The camera subsystem remains independent. Camera SDK/device lifecycle and frame transport stay inside Camera/. Swift continues to own the acquisition workflow, image/tile organization and gallery. ImageProcessing remains a separate project.
