# OPTIKA B293 v0.9 — Continuation Handoff

**Repository:** Shennagyp/MICROSCOPE_CPP  
**Branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Purpose:** current continuation checkpoint after consolidating the stage OOP model in Controller/.

## 1. Source-of-truth architecture

The C++ stage runtime is in Controller/, beside Camera/. Controller::MicroscopeInstance owns one live Controller::Stage. The Stage is the only runtime owner of:
- Installed LinearAxis and RotationAxis objects.
- Available StageConfigurations and the active configuration.
- Effective working-axis ranges and N physical StageSlots per configuration.
- Global machine position evidence and derived currentSlotId.
- Motion-plan sequence, pending-plan identity and completion/failure lifecycle.

Microscope.Core retains immutable installed-equipment definitions and backend-neutral command/feedback contracts. Controller projects the Core equipment Stage into a runtime Stage through Stage::fromEquipmentSpecification.

Microscope.Application does not contain another Stage, stage position, canvas, fixed carrier or navigation planner.

## 2. Current object model

~~~text
MicroscopeInstance
└── Stage
    ├── LinearAxis[N]
    ├── RotationAxis[N]
    └── StageConfiguration[N]
        ├── WorkingLinearAxis[N]
        ├── WorkingRotationAxis[N] (optional)
        └── StageSlot[N]

StageMotionCommandTranslator
        |
        v
Core MoveAxesTo / HomeAxes
        |
        v
IStageMotionPort
        |
        v
Controller adapter
~~~

A StageSlot is a physical fixture region, not a Slide. It has a global XY origin and local XY usable ranges. A StageConfiguration selects the installed axes and defines effective X/Y/Z ranges. Rotation is optional. All runtime linear coordinates and mechanical values use integer nanometres; angles use radians.

The holder count is N. One-slot Petri fixtures, two-slide holders and ten-slot holders use exactly the same StageConfiguration/StageSlot model.

## 3. Application Slide ownership

The existing physical Slide model remains application-owned. The application maps a SlideId plus exact immutable Slide revision to a StageSlotId through StageSlotAssignments. Optional StageSlotRegistration retains the measured four-corner transform between that Slide revision's local coordinates and its assigned StageSlot.

StageSlotAssignmentsSqliteStore is the canonical assignment/registration persistence. It uses immutable revisions and foreign-key references to the existing Slide revision store. It does not persist another copy of stage slot geometry.

Slide acquisition workflow, capture orchestration, image/tile organization, file persistence and gallery remain in Swift as already established. Camera remains an independent C++ camera capability; no stage or microscope acquisition policy is moved into the camera library.

## 4. Motion lifecycle already implemented

- moveTo(slot-relative, slotId) translates local XY to global Stage coordinates.
- moveTo(absolute) plans global XYZ plus all active rotary axes.
- moveBy computes an absolute target from a known current position.
- home plans the exact active axis set.
- refreshPosition saves the current global position, homes, and then creates a separate return plan after the home completion and reference fact have been accepted.
- A pending plan carries a sequence and active configuration identity. A stale/tampered plan cannot be dispatched or reconciled.
- Command submission is not physical completion. Only the exact correlated completion can update command-derived position.
- Home requires both exact action completion and a complete normalized reference observation.
- Failed/indeterminate movement invalidates position.

Stage calibration is separate from refreshPosition and is not yet a complete runtime procedure/record implementation.

## 5. Old stage design status

The former Core XY working-surface canvas, user-drawn slide rectangles, fixed DualSlideCarrier/DualSlideNavigation types, Core StagePosition3D/StagePositionState aggregates, old stage planners, Application stage navigation runtimes/reducers and the separate canvas/carrier persistence stores are retired from the canonical design and build.

Do not restore them as compatibility wrappers or use them as a foundation for the adapter. The generic StageSlot assignment and registration model is the replacement.

## 6. Adapter contract and what remains

The existing Core IStageMotionPort is the adapter contract:
- moveAxesTo
- moveAxesBy
- homeAxes
- stopAxes

A concrete adapter belongs in Controller/Adapters/<controller> and must implement the port. It owns the physical controller's device lifecycle, axis-channel mapping, unit/step/encoder conversion, direction, homing procedure, trajectory, readback, limits, faults, timeout and reconnect behavior. It must use the supplied correlation context and feedback ingress.

The current Controller Stage API reconciles command-derived move completion and normalized homing. It still needs a full per-axis measured-position/uncertainty feedback reconciliation API before encoder/readback observations can become authoritative Stage state. The behavior for partial moves, feedback loss, late callbacks, disconnect/reconnect and timeouts must be specified in that path.

Before hardware-specific code is finalized, capture the actual controller model/protocol, host interface, channel map, steps/mm or encoder scale, direction polarity, mechanical limits, homing sensor/procedure, velocity/acceleration limits, readback capabilities, backlash and stop/fault semantics. Do not invent or guess those values.

A low-level adapter can begin with a deterministic fake protocol harness using IStageMotionPort now; production hardware commissioning depends on the exact device facts and a feedback reconciliation owner.

## 7. Immediate implementation order

1. Complete the Controller per-axis measured-feedback reconciliation and uncertainty/error policy.
2. Implement a fake-controller adapter harness for unit conversions, axis mapping, travel limits, coordinated moves, homing, partial failure and session loss.
3. Implement the physical IStageMotionPort adapter for the selected controller.
4. Bind one long-lived MicroscopeInstance and adapter into the native application composition. Keep SwiftUI as producer/observer and keep Slide/acquisition ownership in the application.
5. Verify physical travel, homing, accuracy/repeatability, backlash, readback and stop behavior on the manufactured stage.

See Controller Stage and Hardware Adapter Contract v1 for the exact port and hardware-input checklist.
