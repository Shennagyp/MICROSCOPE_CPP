# OPTIKA B293 Motorized Stage — Hardware Model

**Status:** As-built modelling record initiated  
**Branch:** `motorized-stage-model-v0.1`  
**Baseline:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9` at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`  
**Scope:** Physical motorized-stage assembly, its kinematics and limits, and the boundary to the Microscope platform's Stage and Motion models.

This document is the hardware-modelling continuation for the manufactured motorized stage. It is not a camera-development plan, UI plan, or a claim that the stage has already been fully characterized.

The only new as-built fact established for this record is that the motorized stage has been manufactured. The repository baseline does not yet contain a verified inventory of its mechanical parts, controller, sensors, axis count, dimensions, travel, or measured motion performance. Those values must come from the actual assembly, drawings, component markings, controller configuration, and measurements. They must not be inferred.

## 1. Modelling objective

Create one traceable model of the actual manufactured assembly that can support:

1. Mechanical understanding and maintenance of the stage.
2. A correct mapping between physical motion and microscope coordinates.
3. Safe, typed motion commands in the Microscope platform.
4. Position observations with honest provenance.
5. Motion calibration and slide/tile coordinate registration.
6. A hardware adapter that translates platform commands into the actual controller and actuator protocol.

The model must preserve the distinction between **what is physically built**, **what the stage can do**, **what the controller was asked to do**, and **what position was actually observed or calibrated**.

## 2. Three separate models

### 2.1 Physical assembly model — as built

This describes the manufactured mechanism and its interfaces. It records physical parts and the relationships that matter to load paths, kinematics, adjustment, service, sensing, and safety.

The assembly record should identify only meaningful mechanical/electrical subassemblies. It should not create software identities for every screw, washer, or ordinary fastener unless an item is independently replaceable, calibrated, safety-critical, or otherwise has a real lifecycle.

The as-built inventory must establish, rather than assume:

- Fixed mounting/base structure and the microscope interface datum.
- Moving carriage/platform and the slide-holder interface.
- The number and arrangement of independently motorized axes.
- For each axis: guide/constraint mechanism, actuator, transmission, coupling, bearings/supports and mechanical end stops.
- Home/reference sensors and travel-limit sensors, including their physical locations and normally-open/normally-closed or equivalent electrical semantics where applicable.
- Motor driver/controller, power requirements, communication/control interface and firmware/configuration identity.
- Cable routing, moving cable constraints and any collision or interference envelope.
- The physical payload interface and the permitted mounted load, when established.

The inventory must distinguish a physical component from a logical axis. A motor, screw, belt, rail, encoder, switch and carriage are physical items; an axis is the resulting constrained degree of freedom.

### 2.2 Logical equipment specification — Microscope.Core

The current Core model already represents a stage as one installed `MechanicalAssembly` with a `StageSpecification` containing one or more `AxisSpecification` values.

That model records static capability and declared performance:

- Axis identity and motion type.
- Coordinate unit and finite operating range.
- Nominal command/readout resolution.
- Optional, independently supplied accuracy, repeatability and measurement uncertainty with test conditions.
- Position-feedback capability and measurement location.

It intentionally does **not** record mechanical transmission design, current position, motor phase, requested target, homing state, or controller state. Do not overload `StageSpecification` with motor-driver registers or low-level parts merely because the stage is motorized.

Populate this specification only from the as-built record and justified measurements. Unknown accuracy, repeatability, uncertainty, feedback, travel or resolution remains absent/unknown; it is not calculated from a motor label or microstepping setting.

### 2.3 Runtime Motion model — commands and observations

Motion is the capability that operates the installed stage. It is separate from the static equipment definition and from the native camera capability.

The platform's operation runtime owns command/step lifecycle and verification. The Motion adapter owns protocol translation and physical I/O. The adapter must not mutate Core runtime state directly.

The planned capability vocabulary includes axis connection/session, absolute and relative motion, velocity constraints, stop, homing where physically supported, limit state, position/status observation and fault reporting. Each command must be typed, correlated to its operation/step/command identity, and accepted or rejected synchronously by the adapter. Later device feedback is a different fact.

Do not implement a generic string-command API or a universal device executor.

## 3. Coordinate and kinematic model

### 3.1 Coordinate frames

The as-built record must define explicit frames before coordinate values are used by acquisition:

- **Microscope frame:** fixed to the microscope body and independent of stage motion.
- **Stage-base frame:** fixed to the manufactured stage base/mounting datum.
- **Axis frame:** the positive direction and zero/reference convention for each physical axis.
- **Carriage/slide-holder frame:** fixed to the moving specimen-support structure.
- **Slide/specimen frame:** defined by the mounted slide and any established registration.

For every frame, record its datum, axis directions, handedness, units and transform to its parent frame. A sign convention must be measured/verified against physical movement; do not assume that a motor's positive direction is microscope +X or +Y.

A software pane, slide index, tile order or image coordinate is not a mechanical coordinate. The mapping from stage coordinates to image/specimen coordinates requires optical/motion calibration and explicit provenance.

### 3.2 Axis kinematics

For each linear or rotary axis, record the actual motion chain:

`controller command → driver → actuator → transmission → constrained carriage → measured or estimated output`

Record only the conversion parameters applicable to the mechanism actually built. Examples include motor step angle/full steps per revolution, microstep setting, gear ratio and screw lead for a screw-driven axis, or pulley pitch/radius and belt transmission for a belt-driven axis. These are examples of possible fields, not claims about this stage.

For a screw-driven axis, a theoretical command increment can be derived from the measured screw lead, motor full-step count, microstep setting and transmission ratio. The ratio convention must be explicitly stated. The theoretical increment is a command quantization estimate; it is not physical accuracy or repeatability.

Microstepping does not by itself establish corresponding physical displacement. Friction, load, compliance, missed steps, screw/guide errors, backlash, alignment and controller behavior can make actual motion differ from the ideal kinematic calculation.

Keep the following separate:

- Command quantization / nominal resolution.
- Actual measured displacement.
- Accuracy against a reference.
- Bidirectional backlash or lost motion.
- Repeatability under stated conditions.
- Measurement uncertainty.
- Maximum usable speed/acceleration under stated payload and operating conditions.

Do not derive accuracy by multiplying motor, screw or microstep specifications.

### 3.3 Position and reference semantics

A motor step count or commanded displacement without position feedback is an estimate. It is not an encoder or specimen-position measurement.

Record separately:

- Commanded target or relative displacement.
- Controller/device-reported position and its native unit.
- Encoder/linear-scale measurement and the physical location at which it is measured.
- Calibrated stage-coordinate estimate, including calibration revision.
- Unknown position/reference state.

A home switch establishes a reference event only to the extent supported by its physical location, repeatability and homing procedure. It does not automatically establish specimen-plane coordinates or absolute accuracy.

Power loss, controller reset, a detected stall, missed-step suspicion, manual movement, limit impact or a feedback fault may invalidate the estimated position. The runtime must then expose position as unknown until a defined homing or reconciliation procedure restores a valid reference.

## 4. Limits, homing and stop semantics

The as-built model must distinguish:

- **Mechanical end stop:** physical boundary of motion.
- **Hardware limit sensor:** electrical sensor that the controller/driver can observe or act on.
- **Software travel limit:** configured command boundary.
- **Usable travel:** measured safe motion interval for the installed payload and microscope clearance.
- **Home/reference sensor:** sensor used to establish a coordinate origin.
- **Operating target:** requested position, which must be inside validated limits.

A limit switch is not automatically a homing switch; one sensor may serve both functions only when the hardware and controller behavior establish that fact.

Homing is supported only when the manufactured mechanism and controller expose a defined, safe homing procedure. If an axis has no homing reference, the model must not invent one.

A normal stop and an emergency stop are different safety actions. Their electrical path, stopping behavior, whether they remove drive power, whether they preserve holding torque, and the state required before motion can resume must be documented from the actual design. Do not label a software stop as an emergency stop.

## 5. Measurement and characterization record

Every measured performance value must carry the conditions that make it interpretable. The record must include, as applicable:

| Measurement | Required context |
|---|---|
| Travel/range | Physical datum, direction, hard/software/usable boundary, payload |
| Resolution | Controller mode, microstep/gear/transmission configuration, native unit |
| Accuracy | Reference instrument, range sampled, direction, payload, environment, method |
| Repeatability | Number of cycles, approach direction, payload, metric and statistic |
| Backlash/lost motion | Reversal procedure, load, reference instrument and method |
| Velocity/acceleration | Payload, commanded profile, observed profile, measurement method |
| Orthogonality/squareness | Axis travel, reference geometry and measurement method |
| Position feedback | Sensor type, location, resolution, calibration and signal path |
| Homing repeatability | Switch/sensor, approach direction, cycles and measured spread |

A value without sufficient conditions may be retained as an observation, but it must not be promoted to a guaranteed specification.

## 6. Safety and operational invariants

The physical and software models must preserve these invariants:

1. No command may target outside the validated software operating range.
2. Hardware limits and mechanical stops remain independent protection layers; software range checks do not replace them.
3. A move request is not proof that motion began, completed, or reached its target.
4. Adapter acceptance, device acknowledgement, action completion, state observation and Core verification remain separate facts.
5. A stop request is not proof that the stage is stationary.
6. A cancelled or timed-out operation may leave the physical stage in an unknown or partially moved state.
7. Feedback loss, stale session feedback or controller reset cannot leave a previously verified position silently trusted.
8. The runtime must serialize state reduction; device callbacks submit immutable feedback and never update Core state directly.
9. An acquisition workflow must not assume stage position is known merely because a previous move command returned successfully.
10. The camera preview/frame pipeline remains independent and unchanged by Motion integration.

## 7. Evidence ledger — current state

| Item | State |
|---|---|
| Physical motorized stage manufactured | Confirmed by project owner in this conversation |
| Mechanical assembly drawing/CAD and mounting datum | Not yet recorded in this branch |
| Number and arrangement of motorized axes | Not established in this record |
| Actuator and controller models/configuration | Not established in this record |
| Transmission type and conversion parameters | Not established in this record |
| Home/limit sensors and safety behavior | Not established in this record |
| Measured travel and usable range | Not established in this record |
| Position-feedback capability and location | Not established in this record |
| Measured resolution, accuracy, repeatability and backlash | Not established in this record |
| Motion command/feedback adapter | Not implemented by this modelling record |
| Motion calibration | Planned separately in Microscope platform Phase 6 |

“Not established” means the data has not been evidenced in the inspected repository baseline or recorded for this model. It does not mean the manufactured stage lacks that component or capability.

## 8. Next implementation sequence

1. **As-built inventory:** record the manufactured assembly, axis count, mounting interface, moving structure, actuator/transmission, controller and sensors from physical evidence.
2. **Coordinate definition:** establish the microscope/stage/axis/slide-holder frames, origins, directions and safe travel limits.
3. **Kinematic derivation:** calculate the theoretical command mapping from verified component parameters and explicitly label it as derived, not measured accuracy.
4. **Motion capability contract:** complete Phase 4B with typed axis move/stop/home commands, command correlation and stage-specific feedback payloads. Only include homing, speed profiles or feedback operations the hardware supports.
5. **Adapter integration:** bind the logical stage/axes to the actual controller and translate Core commands without exposing controller registers or handles to Core.
6. **Characterization and calibration:** measure travel, repeatability, backlash, accuracy and homing behavior; then create calibrated position evidence as a separate immutable calibration revision.
7. **Acquisition integration:** associate position observations with slide/tile/image-frame provenance without modifying the camera acquisition or frame-lease pipeline.

The next code change must be derived from the as-built inventory, not from a generic stepper-stage template.

## 9. Governing references

- [Microscope Platform Architecture v0.9](MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md), especially §5.6 (Stage and axes), §7 (operations and feedback) and §8 (runtime ownership).
- [Microscope Core and Application Contracts v0.9](MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md), especially §14.2–14.3 (Stage/axis specifications and position feedback).
- [Microscope Platform Implementation Plan v0.9](../plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md), Phase 4 (Core operation runtime), Phase 5 (JSON/application composition) and Phase 6 (motion calibration).
- [OPTIKA B293 Dual-Slide Refactor Plan](../plans/OPTIKA_B293_DUALSLIDE_REFACTOR_PLAN.md), Phase 5 (Motion capability expansion).

