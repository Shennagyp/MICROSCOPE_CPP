# Controller

The C++ Controller capability lives beside `Camera/`. It contains backend-neutral
mechanical models and motion contracts. Vendor/controller-specific implementations
will be separate drivers and must not leak protocol or pulse-generation details
into the common Stage model.

## Stage model

A Stage owns installed LinearAxis and RotationAxis objects and a collection
of StageConfigurations. Every configuration selects its working X, Y and Z
linear axes, its effective range for each, optional working rotation axes, and
a variable-size collection of StageSlots. Thus ten slide slots and one Petri-dish
slot are configurations of the same Stage model.

Linear dimensions, positions, mechanical ranges, accuracy, precision and minimum
increments use integer nanometres. Rotation geometry is a line defined by a point
in global XYZ nanometres and a cardinal direction (X/Y/Z); rotation ranges,
positions, accuracy, precision and increments use radians.

A StageSlot represents the usable region of a holder location. It has a global
stage-coordinate origin and local X/Y ranges. It does not contain a Slide object:
the application owns physical slides and associates them with slot identities.

The current slot is derived from the current known global XY position and the
active configuration. There is no second mutable current-slot state.

## Driver contract

`IStageDriver` exposes homing and absolute movement for linear and rotational
axes. Methods must return after completion or terminal failure. An asynchronous
controller driver waits internally for completion. STEP/DIR drivers own physical
unit-to-step calibration and pulse generation; intelligent controllers may accept
engineering coordinates directly.

The Stage records command-derived positions after completed movement and
homing-referenced positions after successful homing. This interface does not claim
encoder verification. A future feedback path must carry explicit measurement
evidence rather than silently presenting commanded coordinates as measured.

## Operations

- `moveTo(SlotRelativePosition, StageSlotId)`: translate a slot-relative XY point
  into global coordinates; requires homed/known XY and leaves Z/rotation unchanged.
- `moveTo(StageAbsolutePosition)`: absolute global XYZ plus all configured
  rotation targets.
- `moveBy(StageDisplacement)`: relative displacement from known current position.
- `home()`: home axes used by the active configuration.
- `refreshPosition()`: save the current XYZ/rotation target, home active axes,
  and return to the saved target.

This introduces the backend-neutral Controller foundation. Vendor drivers,
position-feedback integration, equipment JSON migration and retirement of the
legacy Core navigation model remain separate integration work; they are not
claimed complete by this addition.
