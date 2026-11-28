# Controller

The C++ Controller capability lives beside `Camera/`. It owns the backend-
neutral microscope mechanics model and generates typed motion plans. It does
not define a second hardware-driver interface.

## Object ownership

`MicroscopeInstance` owns one live `Stage` and an explicit runtime `DeviceId`
binding for the physical Stage controller. The binding is distinct from the
Stage's equipment `EquipmentComponentId`; the dispatcher checks the runtime
microscope ID and the bound Stage device ID independently. The Stage owns its installed
`LinearAxis` and `RotationAxis` objects, available `StageConfiguration`
objects, active configuration, and current position evidence.

Each configuration selects working axes and owns a variable collection of
generic `StageSlot` regions. The application owns physical Slide objects and
associates Slide IDs with StageSlot IDs. Two-slide, ten-slide, Petri-dish and
other holders are all represented by the same N-slot model.

## Coordinates and mechanical properties

All linear positions, travel ranges, slot dimensions, accuracy, precision,
minimum increments and measurement uncertainty use integer nanometres.
Minimum increment, accuracy, precision and measurement uncertainty are stored
independently; unavailable measurement uncertainty is not replaced with zero.
Rotation uses radians. A rotation axis is
specified by a point in global stage XYZ coordinates and a cardinal direction
(X, Y, or Z). Rotary accuracy, precision, minimum increment and measurement
uncertainty use radians.

Each configuration defines effective X, Y and Z ranges. Rotation axes are
optional per configuration. A StageSlot has a global XY origin and local X/Y
usable ranges. Slot-relative targets are translated to global stage
coordinates. The current slot is derived from active configuration and current
XY position; there is no separately mutable current-slot field.

## Motion and completion lifecycle

The Stage and axis objects create typed `StageMotionPlan` values. A plan
has a monotonically increasing sequence and the active configuration identity.
Only one plan can be pending for a Stage at a time. The translator validates
that the plan is exactly the live Stage's pending plan before generating Core
commands. For schema-v2 equipment, it also requires the selected runtime
configuration, physical axis roles/rotation geometry, working ranges, and slot
rectangles to match the canonical equipment definition.

The Application `StageMotionPlanDispatcher` translates the plan and submits it
through the existing correlated `StageMotionCommandDispatcher`,
`SerializedOperationRuntime`, and Core `IStageMotionPort`. It returns the
plan sequence with the adapter-submission receipt. Submission acceptance is
not treated as physical completion.

After the runtime correlates the exact Core `StageMotionActionCompletion`,
the host passes that fact to `Stage::recordCompletedMotion`. The Stage checks
the action type and exact axis set before atomically applying command-derived
position state. That state is a command-derived estimate, not a measured
position; subsequent Core position observations remain a separate feedback fact. An uncertain or failed execution is sent to
`recordFailedMotion`, which invalidates position. A definite pre-submission
or adapter rejection cancels the pending plan without changing known position.

Homing has a stricter two-fact contract. The host supplies both the correlated
`StageMotionActionCompletion` for `HomeAxes` and Core's normalized
`StageHomeReferenceObservation` to `recordCompletedHome`. The latter fact
is emitted only after the configured reference procedure has established all
requested axis references. A generic successful HomeAxes action does not by
itself establish a valid global coordinate origin.

## Stage operations

- `moveTo(SlotRelativePosition, StageSlotId)`: plan absolute XY movement
  relative to a slot origin; preserve current Z and rotation.
- `moveTo(StageAbsolutePosition)`: plan absolute global XYZ and all active
  rotation targets.
- `moveBy(StageDisplacement)`: calculate a target from known current position.
- `home()`: plan homing of every axis used by the active configuration.
- `refreshPosition()`: capture the saved global position and produce a home
  plan; after the home action and reference observation are accepted,
  `restorePositionAfterRefresh()` produces the return movement plan. Refresh
changes neither mechanical calibration nor slot geometry; it is a home-and-
return position-reference operation. The separate `calibrate()` procedure is
not implemented by this Stage command model yet.

## Equipment-unit boundary

Controller geometry remains in nanometres and radians. The
`StageMotionCommandTranslator` validates component/axis identity and converts
targets to the coordinate units declared by the installed Core equipment
definition (µm, mm, degrees or radians). It does not perform hardware I/O.
The existing motion adapter remains responsible for device-specific unit
conversion, trajectories, feedback, timeouts and faults.

Slide identity, acquisition orchestration, image/tile persistence and UI
ownership remain outside Controller.
