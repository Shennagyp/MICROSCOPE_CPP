# Controller Stage and Hardware Adapter Contract

**Status:** Authoritative stage runtime and adapter boundary for OPTIKA-B293-DUALSLIDE-APP-v.0.9.  
**Scope:** C++ Controller OOP model and the hardware contract required to develop a physical stage adapter.

## 1. Ownership and authority

~~~text
Microscope equipment definition (Core, static)
                 | one-way projection
                 v
MicroscopeInstance (Controller, runtime)
└── Stage
    ├── installed LinearAxis inventory
    ├── installed RotationAxis inventory
    ├── StageConfiguration[N]
    │   ├── selected working axes/ranges
    │   └── StageSlot[N]
    ├── current global position/evidence
    └── pending motion plan
                 |
                 v
       StageMotionCommandTranslator
                 |
                 v
       Core IStageMotionPort
                 |
                 v
      physical stage adapter/controller
~~~

The Controller Stage is the only runtime source of truth for stage axes, configurations, slots, position and motion lifecycle. The legacy Core working-surface canvas, fixed carrier, dual-slide navigation planner, and Application stage navigation runtimes are retired and are not adapter dependencies.

Core keeps two narrow roles:
1. Immutable installed-equipment specifications, including axis characteristics and versioned Stage configurations.
2. Backend-neutral command/feedback value types and IStageMotionPort.

The application owns Slide records and associates SlideId/revision with StageSlotId. StageSlotAssignments and StageSlotRegistration are application data; they do not own stage geometry or physical motion.

## 2. Coordinate and configuration invariants

- Global Stage coordinates are machine coordinates. Homing establishes their origin.
- Linear coordinates, mechanical ranges, effective ranges, minimum increments, accuracy, precision and measurement uncertainty are expressed in integer nanometres inside Controller.
- Rotation angles, travel, increments and tolerances use radians inside Controller.
- Equipment schema v2 uses nanometres for every linear axis coordinate/range/resolution/tolerance and radians internally for configuration rotation ranges. Device-native units are not the Core Stage coordinate system.
- Equipment schema v1 remains a legacy import format; a v1 Stage specification has no StageConfiguration/StageSlot setup and must be migrated to v2 before runtime Controller construction.
- A configuration selects installed axes; it does not copy or redefine mechanical properties.
- Every runnable configuration defines X, Y and Z effective ranges. Rotation is optional.
- Each configuration owns N non-overlapping StageSlots. A slot is a physical holder region with a global XY origin and local X/Y ranges.
- Slide does not exist in Controller's physical stage model. A Slide is assigned to a slot by the application.

## 3. Existing driver interface

A concrete driver implements microscope::core::IStageMotionPort from Libraries/Microscope.Core/include/microscope/core/motion/StageMotionPort.h.

~~~cpp
class IStageMotionPort {
public:
    virtual ~IStageMotionPort() = default;

    virtual StageMotionSubmissionResult moveAxesTo(
        const StageMotionCommandContext&,
        const MoveAxesTo&,
        IFeedbackIngress<StageMotionFeedbackPayload>&) = 0;

    virtual StageMotionSubmissionResult moveAxesBy(
        const StageMotionCommandContext&,
        const MoveAxesBy&,
        IFeedbackIngress<StageMotionFeedbackPayload>&) = 0;

    virtual StageMotionSubmissionResult homeAxes(
        const StageMotionCommandContext&,
        const HomeAxes&,
        IFeedbackIngress<StageMotionFeedbackPayload>&) = 0;

    virtual StageMotionSubmissionResult stopAxes(
        const StageMotionCommandContext&,
        const StopAxes&,
        IFeedbackIngress<StageMotionFeedbackPayload>&) = 0;
};
~~~

The adapter receives the exact runtime/device/operation context and a bounded feedback ingress. It must preserve that correlation for every callback derived from that operation and device session.

StageMotionSubmissionResult has an important semantic distinction:
- Accepted: adapter accepted responsibility for dispatching/executing the request.
- Rejected: adapter definitively did not accept the request.
- An exception or ambiguous controller response must not be fabricated into a definite rejection if the command may already have reached the device. The host treats uncertain submission as indeterminate and reconciles state conservatively.

The adapter must not treat a successful submission as movement completion.

## 4. Feedback contract

StageMotionFeedbackPayload supports:
- StageMotionAcknowledgement
- StageMotionActionCompletion
- AxisMotionStateObservation
- AxisPositionObservation
- AxisLimitObservation
- StageHomeReferenceObservation
- StageMotionFaultObservation

### Movement completion

For a move command the adapter reports the exact completed action and exact axis set. It must not omit a partially moved axis and then report the whole command as completed. If only some axes moved or one axis failed after others moved, report the operation/fault outcome so the host invalidates or reconciles the affected position rather than assuming rollback.

An axis position observation must use the coordinate unit declared for that installed equipment axis. Preserve its evidence class and uncertainty. Do not report command-derived estimates as encoder measurements or hardware readback.

### Homing and global origin

The Stage accepts a home only when both facts are supplied:
1. Exact correlated HomeAxes action completion with the requested axis set.
2. A normalized StageHomeReferenceObservation containing every requested axis, with physical-endstop/reference evidence and compatible units.

The current Controller Stage establishes machine origin at zero after this fact. Therefore the adapter must normalize device-specific switch offsets, encoder indices or controller coordinates so the reported machine-reference coordinates correspond to the global Stage origin (0 nm / 0 rad). If the physical home reference is not the desired coordinate origin, that offset must be represented explicitly in the installed/mechanical coordinate transform; do not silently report raw motor steps as global coordinates.

An idle status or generic successful HomeAxes completion is not sufficient evidence of a global origin.

### Faults and stop

Normalize limit reached, following error, controller fault, communication loss and emergency-stop status through the existing typed observations. A stopAxes call is a controlled stop only. A safety-rated E-stop chain is external hardware/safety design and must not be represented as this software call.

## 5. Adapter responsibilities

The adapter is the only layer that knows the vendor protocol. It owns:

1. **Device lifecycle:** open, configure, close, reconnect, session generations and stale callback suppression.
2. **Axis binding:** map stable StageAxisId values to physical controller channels, motor/drive channels and readback sources.
3. **Unit conversion:** map canonical command coordinates to controller engineering units, motor steps, encoder counts or native command representation. There must be exactly one physical scale conversion in the active path.
4. **Sign and frame mapping:** apply electrical direction polarity, controller coordinate sign and the documented mapping to global X/Y/Z/rotation roles.
5. **Homing:** execute the hardware-specific reference sequence, including switch/index polarity, search direction, velocity, backoff, latch/index phase, repeatability and final coordinate zeroing.
6. **Trajectory:** implement the controller's synchronized or sequential multi-axis movement semantics, speed/acceleration/deceleration constraints, travel limits and any device-specific queue behavior.
7. **Feedback:** translate device events/readback into the existing normalized facts with correct correlation, evidence provenance and uncertainty.
8. **Failure semantics:** distinguish rejected, accepted, indeterminate, completed, timed out, disconnected and faulted outcomes. Do not emit success after a partial or uncertain move.
9. **Stop semantics:** provide controlled stop and document the independent hardware E-stop path.

Controller must not contain motor pulses, step counts, vendor SDK types, serial framing, USB packet logic or controller-specific homing sequences.

## 6. Required hardware inputs before a hardware-specific adapter can be completed

The OOP and port contract exist. The following device facts are still required to implement a real adapter safely:

| Input | Required detail |
|---|---|
| Controller identity | Exact controller model, firmware, supported protocol/SDK, host OS and CPU architectures |
| Device topology | USB/serial/Ethernet interface, controller address, device/channel identifiers and whether all stage axes share one controller |
| Axis mapping | StageAxisId → controller axis/channel for X, Y, Z and optional rotation |
| Mechanical envelope | Measured travel limits and software-safe limits for each axis |
| Scale | Steps per millimetre, encoder counts per millimetre, or controller-native engineering units; include gearing/microstepping where applicable |
| Direction | Positive command direction versus physical global X/Y/Z/rotation direction |
| Home reference | Switch/index type, polarity, search direction, backoff, latch, repeatability, offset and safe homing sequence |
| Motion profile | Maximum velocity, acceleration/deceleration, jerk if supported, coordinated-move limits and safe defaults |
| Feedback | Available position readback, status, limit and fault sources; units, update rates, latency and uncertainty |
| Backlash | Measured directional lost motion and required final-approach/compensation policy |
| Stop/fault behavior | Controlled stop response, drive disable semantics, limit handling, fault reset and separate hardware emergency-stop circuit |
| Lifecycle/failure | Open/close behavior, reconnect semantics, command timeout, disconnect detection and late callback behavior |

Do not derive these values from nominal motor labels or guessed steps/mm. They must come from the actual controller configuration, documentation or measurement.

## 7. Adapter implementation sequence

1. Build a vendor-specific Controller::Adapters::<Controller> target implementing the existing IStageMotionPort. Do not create a parallel Stage or port abstraction.
2. Add a deterministic fake controller/protocol test harness. Verify unit conversion, sign mapping, travel-limit rejection, multi-axis completion and home normalization before hardware access.
3. Implement device open/session generation and axis binding.
4. Implement moveAxesTo and controlled stop. Test accepted/rejected/indeterminate submission and partial failure.
5. Implement homing and the exact complete StageHomeReferenceObservation contract.
6. Implement readback/status/fault observations and session-loss behavior.
7. Add hardware smoke tests for each axis, each homing procedure, full travel, coordinated moves, repeatability and backlash.
8. Bind the concrete port to the long-lived MicroscopeInstance through the host composition. Do not instantiate a fake port in production.

## 8. Remaining integration gaps before production motion

The low-level adapter can be developed now against IStageMotionPort. Production movement still requires the following C++ integration work:

- A Controller-owned or explicitly host-owned feedback reconciliation path must consume the correlated runtime receipts and apply measured axis observations/faults to the live Stage. The current Stage API records command-derived move completion and normalized homing; it does not yet expose a complete per-axis measured-position/uncertainty update API.
- The runtime must define what a partial move, indeterminate submission, timeout, feedback loss and reconnect do to each axis reference and the overall Stage position. Until this exists, an uncertain operation must invalidate position and require explicit reconciliation/homing.
- A Stage calibration record/procedure remains separate from refreshPosition(); it is not part of the hardware adapter.
- The native composition must eventually create the real Stage, bind the adapter's DeviceId, retain the port for the microscope lifetime and provide the feedback owner/dispatcher. This host integration must not recreate stage logic in Swift or Application.

These are explicit follow-on contracts, not reasons to introduce the retired navigation model again.
