# Controller

Controller/ is the C++ runtime mechanics capability beside Camera/. It is the sole runtime owner of the physical microscope stage. It is not another Application navigation layer and it does not depend on the retired canvas, fixed-carrier or slide-navigation models.

## Authority and dependency direction

- Microscope.Core defines immutable installed-equipment specifications and the backend-neutral motion command/feedback contract.
- Controller::Common projects the canonical equipment Stage specification into one live OOP Stage owned by MicroscopeInstance.
- A physical adapter implements the existing Core IStageMotionPort. It performs hardware I/O and reports normalized feedback.
- The Swift/Application layer owns physical Slide documents and associates their existing Slide IDs with configured StageSlot IDs. It does not own stage axes, position, homing, motion planning or physical stage geometry.

There is no second stage model in Microscope.Application. StageWorkingSurface2D, DualSlideNavigation, fixed left/right carrier state and their navigation runtimes are retired.

## Runtime object model

~~~text
MicroscopeInstance
└── Stage
    ├── LinearAxis[N]
    ├── RotationAxis[N]
    ├── StageConfiguration[N]
    │   ├── WorkingLinearAxis[N]
    │   ├── WorkingRotationAxis[N]   (optional)
    │   └── StageSlot[N]
    ├── active configuration
    ├── current position evidence
    └── one pending correlated motion plan

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
        |
        v
physical controller / motors / feedback
~~~

Stage::fromEquipmentSpecification(coreStage, activeConfigurationId) is the only supported construction path from persisted equipment to the runtime Stage model. It projects the installed axis inventory, mechanical limits, axis roles, rotation geometry, configurations and slots into the runtime aggregate. The installed equipment specification remains the source for translator verification; runtime state is not persisted back into the equipment definition.

MicroscopeInstance owns one live Stage and the runtime DeviceId binding used by the dispatch boundary.

## Canonical mechanics model

### Axes

- LinearAxis has a stable StageAxisId, X/Y/Z role, mechanical travel range, minimum increment, optional accuracy, precision/repeatability and measurement uncertainty.
- RotationAxis has a stable StageAxisId, a point on its global machine axis, cardinal direction X/Y/Z, angular travel, minimum increment and independent accuracy/precision/uncertainty.
- Accuracy, precision, minimum increment and measurement uncertainty are not interchangeable. Missing values remain absent.
- Runtime linear values use integer nanometres. Runtime angular values use radians. Equipment schema v2 must express linear coordinate, mechanical range, resolution and tolerances in nanometres. Schema-v1 equipment remains a legacy migration input; a v1 Stage without v2 configurations is not a runnable Controller configuration.

### Configurations and slots

Each StageConfiguration selects the installed axes active for one holder setup and owns a variable N-element collection of StageSlot values. X, Y and Z working ranges are mandatory; rotation is optional. Every effective range must fit within the corresponding installed mechanical range.

A StageSlot describes a physical holder region, not a Slide. It has:
- A global XY origin in the Stage machine coordinate system.
- Local non-negative X and Y usable ranges, in nanometres.

The Stage has one global coordinate system. Homing establishes the global machine origin. Slot-relative target coordinates are translated by adding the selected slot origin. Slot overlap and out-of-envelope geometry are rejected at configuration construction. currentSlotId() is derived from the current global XY position and active configuration; it is not a separately mutable state field.

Two slides, ten slides, a Petri dish or any other holder are represented by the same StageConfiguration + N StageSlots model.

## Stage command and position lifecycle

The Stage exposes:
- moveTo(SlotRelativePosition, StageSlotId) for a local XY target in one slot.
- moveTo(StageAbsolutePosition) for an absolute global XYZ target and all active rotation axes.
- moveBy(StageDisplacement) based on a known current position.
- home() for all axes active in the selected configuration.
- refreshPosition() as a two-stage home-and-return transaction.

Motion planning does not update position. A plan has a monotonic sequence and active configuration identity; only one plan may be pending. A plan must be the exact pending plan before translation or completion reconciliation.

A completed move updates a command-derived estimate only after the correlated StageMotionActionCompletion matches the exact action and axis set. An uncertain/failed motion invalidates position. A successful HomeAxes action alone does not establish the coordinate origin: the host must also provide a matching normalized StageHomeReferenceObservation covering every requested axis.

refreshPosition() privately retains the saved pre-home position. After the exact home and reference facts are accepted, restorePositionAfterRefresh() creates the return plan. Callers cannot modify the saved restoration coordinates; cancelled undispatched restoration can be retried, while a completed/failed/superseded refresh cannot be replayed. Refresh does not change calibration or slot geometry. It does not by itself eliminate mechanical backlash; any consistent final-approach or compensation policy must be based on the actual mechanics.

Stage calibration is separate from position refresh. A complete Stage calibration procedure/record lifecycle is not yet implemented by the runtime Stage API.

## Application-owned Slide association

The application retains the actual Slide aggregate and slide revisions. These generic records remain in Microscope.Application:
- StageSlotAssignments: sparse occupied-slot mapping from StageSlotId to existing SlideId and exact Slide revision.
- StageSlotRegistration: four measured corners and a revision-bound transform between one Slide's local coordinates and its assigned StageSlot's stage coordinates.
- StageSlotAssignmentsSqliteStore: immutable, revisioned assignment/registration snapshots linked to the existing Slide revision store.

These records consume StageSlot identities from the canonical equipment configuration. They do not redefine slot geometry or maintain an alternative stage coordinate system.

## Motion port and adapter boundary

The existing Core port is the hardware boundary. A driver implements IStageMotionPort:
- moveAxesTo
- moveAxesBy
- homeAxes
- stopAxes

The Controller translator currently emits Core HomeAxes and MoveAxesTo commands. The port also exposes MoveAxesBy for an adapter/runtime path when needed. The application dispatch bridge forwards translated commands through the existing correlated operation runtime; it does not own Stage state.

Adapter submission acceptance means only that the adapter accepted responsibility for the operation. It is not physical completion. The adapter reports correlated acknowledgements, progress, exact action completion, axis position observations, home-reference observations, limits and faults through the supplied feedback ingress. It must not mutate Stage state from a device callback thread.

The adapter owns:
- Vendor/controller connection and session lifecycle.
- Logical StageAxisId to physical controller channel mapping.
- Device-unit/step/encoder conversion and direction polarity.
- Hardware-specific homing sequence and switch/index interpretation.
- Motion profile generation, synchronized multi-axis movement and trajectory limits.
- Controller communication, cancellation, timeouts, disconnection and fault normalization.
- Backlash or approach-direction behavior where the mechanism requires it.

stopAxes is a controlled motion stop, not a safety-rated emergency-stop circuit.

## Build and tests

Controller/Common is a standalone CMake target and is linked by the platform composition. Its tests validate the Stage object model, configuration validation, slot-relative/global transformations, plan identity, completion/homing semantics, refresh transaction and Core command translation.

A hardware adapter should be added as a separate target under Controller/Adapters/<vendor-or-controller> and implement the existing port. It must not add another Stage type, coordinate system or driver interface.
