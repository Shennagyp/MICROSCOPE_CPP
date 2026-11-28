# Controller

The C++ Controller capability lives beside `Camera/`. It owns the backend-
neutral microscope mechanics model and produces typed motion plans. It does not
define a second hardware-driver interface.

## Object ownership

`MicroscopeInstance` owns one live `Stage`. The Stage owns the installed
`LinearAxis` and `RotationAxis` objects, its available `StageConfiguration`
objects, active configuration, and runtime position evidence.

Each configuration selects its working axes and owns a variable collection of
generic `StageSlot` regions. The application owns physical Slide objects and
associates Slide IDs with StageSlot IDs. A two-slide holder is simply a
configuration with two StageSlots; a ten-slide holder has ten.

## Coordinates and mechanical properties

All linear positions, travel ranges, slot dimensions, accuracy, precision and
increments use integer nanometres. Rotation uses radians. A rotation axis is
specified by a point in global stage XYZ coordinates and a cardinal direction
(X, Y, or Z).

Each configuration defines effective X, Y and Z ranges. Rotation axes are
optional per configuration. A StageSlot has a global XY origin and local X/Y
usable ranges. Slot-relative targets are translated to global stage
coordinates. The current slot is derived from active configuration and current
XY position; there is no separately mutable current-slot field.

## Command and lifecycle boundary

The Stage and axis objects create `StageMotionPlan` values. They do not call
hardware directly or treat command acceptance as movement completion. The
host dispatches plans through the existing Core `IStageMotionPort` and
correlated serialized-operation runtime. After the exact operation completion
is accepted, the host calls `recordCompletedMotion`. Failed or indeterminate
movement calls `recordFailedMotion` or `invalidatePosition`.

This preserves the existing lifecycle distinction between command submission,
device completion, and position verification. Hardware adapters retain
responsibility for STEP/DIR conversion, intelligent-controller commands,
feedback, timeouts, device faults and transport behavior.

## Operations

- `moveTo(SlotRelativePosition, StageSlotId)`: plan absolute XY movement
  relative to a slot origin; leave Z and rotation unchanged.
- `moveTo(StageAbsolutePosition)`: plan absolute global XYZ and all active
  rotation targets.
- `moveBy(StageDisplacement)`: calculate a target from known current position.
- `home()`: plan homing of every axis used by the active configuration.
- `refreshPosition()`: capture the saved global position and produce a home
  plan; after home completion, `restorePositionAfterRefresh()` produces the
  return movement plan.

Refresh is a two-step host workflow. The Stage updates position only from
correlated completion calls. Slide identity, acquisition, image/tile
persistence and UI ownership remain outside this library.
