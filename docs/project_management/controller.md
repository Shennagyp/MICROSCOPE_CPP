# Development Line — Controller / Motion / Stage

**Active implementation repository:** [MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP)  
**Active implementation branch:** [`OPTIKA-B293-DUALSLIDE-APP-v.0.9`](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9)  
**Source checkpoint inspected:** `c7bb4b4dc41441989d71ee800c6d33b51dc54abe` (25 November 2026)  
**Historical design/source repository:** [MICROSCOPE](https://github.com/Shennagyp/MICROSCOPE), branches `station-development` and `refactor-2`  
**Line status in the v0.9 target:** microscope Stage specifications exist; a C++ controller/provider/session and physical motion execution path are not established.

This is one independently managed development line inside the shared OPTIKA v0.9 branch. It is not the Slide Scanning line. Slide Scanning consumes the capabilities produced here; it does not own or implement them.

## 1. Responsibility and boundaries

Controller, Motion and Stage are related but distinct concepts. Keep their contracts separate even though they are tracked together as the Controller development line.

- **Controller** owns physical controller identity, provider identity, resources/channels, connection and session lifecycle, controller capabilities, command transport, device feedback and controller-specific failure/recovery semantics.
- **Motion** owns provider-neutral movement semantics: axes, endpoints, units, ranges, coordinate frames, actuator groups, command meaning and immutable capability snapshots. Motion must not depend on a vendor controller implementation.
- **Stage** owns microscope-specific stage mechanics and topology: stage identity, logical axes, coordinate relationships, carrier/rack/slot geometry and qualification against Motion capability evidence. Stage must not open a hardware session or issue a physical command.
- **Microscope Core/Application** owns operation meaning, lifecycle, command orchestration and application composition. Core must not retain vendor SDK handles or perform transport I/O.
- **Slide Scanning** owns the scan workflow and its requested coverage/provenance. It calls into the application capability boundary; it does not address a controller provider directly.
- **Camera** remains a sibling capability with its own single camera workstream. Controller code must not become a second camera/session owner.

The identities must not collapse into one another: microscope identity, controller device identity, controller resource/channel identity, provider/session handle, Motion endpoint/axis identity and Stage logical-axis identity represent different things and require explicit mapping.

## 2. Development history

### Earlier MICROSCOPE repository work — historical reference

The earlier `MICROSCOPE` repository contains three relevant historical architecture lines:

- `refactor-dotnet`: an early .NET runtime/model proposal, including an accumulated-error-only DeviceBridge measurement boundary and stage-calibration foundations. It is historical reference, not the current v0.9 execution authority.
- `refactor-2`: the distributed .NET Manager model. Controllers are Station-scoped records and stage-drive/channel records refer to those identities. Manager owns shared registration/records, not physical execution.
- `station-development`: the Swift Station implementation. It introduced separate `MicroscopeControllerCore`, `MicroscopeControllerFunctions`, `MicroscopeMotionCore`, `MicroscopeMotionFunctions` and `MicroscopeStageCore` packages.

The historical Station code is useful for concepts, invariants, tests and decisions. It is not evidence that those Swift packages have been ported into the C++ OPTIKA branch. The ongoing implementation location is now the OPTIKA v0.9 branch in MICROSCOPE_CPP.

### OPTIKA v0.9 target branch

The checked v0.9 tree currently contains:
- `Libraries/Microscope.Core/include/microscope/core/equipment/StageSpecifications.h`;
- `Libraries/Microscope.Core/src/StageSpecifications.cpp`;
- `Libraries/Microscope.Core/tests/StageSpecificationsTests.cpp`;
- Core operation/step lifecycle semantics from Phase 4A.

StageSpecifications are microscope equipment specifications and validation. They do not constitute a controller model, provider, session, Motion executor, hardware feedback path or physical stage driver.

At the inspected source checkpoint, no C++ Controller provider/session implementation, physical controller communication, Motion command executor or end-to-end Stage-to-controller actuation path is established. Do not report the historical Swift packages as implemented in this target branch.

## 3. Relationship to the application runtime

The v0.9 Core operation lifecycle is the authority for operation and step state. The 4A reducer distinguishes command dispatch, adapter acceptance, device acknowledgement/progress/action completion, device-state observation and Core verification. A command being submitted or accepted is not proof of physical movement; an action-completed report is not proof that the requested position was observed or verified.

The next application contract dependency is Phase 4B — typed command values and capability-specific adapter ports. Controller development must use that shared Core contract rather than creating a second controller-specific lifecycle state machine.

Expected dependency direction:

```text
Slide Scanning
      |
      v
Microscope.Application use cases
      |
      v
Microscope.Core operation / typed command contracts
      |
      v
Controller adapter ---- Motion provider functions
      |                         |
      +-----------+-------------+
                  v
        Stage-specific mapping
                  |
                  v
           Physical hardware
```

This is a responsibility/dependency sketch, not a claim that the complete path currently exists.

## 4. Current status and evidence

**Present in the OPTIKA v0.9 source:**
- microscope Stage specifications and their tests;
- Core operation/step lifecycle transition semantics;
- application-level microscope configuration and equipment architecture.

**Historical reference in MICROSCOPE:**
- Controller identity/capability contracts;
- provider/session function-bundle and registry concepts;
- provider-neutral Motion axis/endpoint models;
- Stage topology and pure qualification concepts;
- Manager-side controller registration and stage-drive references.

**Not established in the active OPTIKA v0.9 line:**
- C++ controller identity/resource/provider contracts;
- a concrete controller provider or native transport;
- controller connection/session lifecycle;
- feedback ingestion and correlation with Core operations;
- Motion command execution;
- endstop/sensor hardware bindings;
- explicit Stage logical-axis to Motion endpoint to controller-resource mapping;
- physical homing, movement, calibration, accuracy or repeatability;
- safety acceptance for physical actuation.

Do not count historical source, a model, a provider registry, a simulated adapter or a Stage specification as hardware execution evidence.

## 5. Safety and semantic invariants

1. No physical movement is authorized by the presence of a model, command interface or qualified topology.
2. Do not enable movement, homing, jogging, motor power or actuator writes until a separately reviewed hardware-safety and execution acceptance authorizes the exact operations.
3. A requested command, adapter receipt, device acceptance, device acknowledgement, action completion, observed state and Core verification are distinct facts.
4. Cancellation remains requested until the device/runtime confirms the cancellation outcome; a cancelled request is not proof that motion physically stopped.
5. Never treat an accumulated error, step count or pulse count as an absolute position.
6. Preserve units, coordinate frame, datum, channel calibration, calibration revision, measurement epoch/sequence/timestamp and error-semantics provenance wherever the selected measurement contract requires them.
7. Do not map controller IDs, channel IDs, provider handles, Motion endpoints and Stage axes by coincidental numeric/string equality.
8. Device loss, timeout, communication failure, stale feedback and unknown physical state must remain distinguishable; never synthesize a safe or completed position from missing evidence.

The earlier DeviceBridge proposal's accumulated-error-only measurement contract is historical design input. Re-adopt it only after checking it against the current C++ Core command/feedback model; do not copy it into a parallel runtime without reconciling semantics.

## 6. Continuation handoff

**Active source branch:** `MICROSCOPE_CPP / OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Current target status:** Stage specification layer present; Controller/Motion execution path not yet implemented or accepted.  
**Integration dependency:** Core Phase 4B typed commands/ports and later runtime/feedback work.

### Next bounded task

Establish the C++ Controller/Motion/Stage contract map against the v0.9 Core before implementing a hardware provider:

1. Read `docs/architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md`, `docs/architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md` and `docs/plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md`.
2. Inspect `StageSpecifications.h`, its implementation and tests. Record which stage facts are specifications, which are runtime state, and which require physical observation.
3. Translate the useful historical Controller/Motion/Stage concepts from `MICROSCOPE/station-development` into a C++ ownership map. Reuse semantics where applicable; do not port package structure mechanically.
4. Define distinct typed identities for controller, provider, device resource/channel, Motion endpoint/axis and Stage logical axis, including the explicit mapping edges between them.
5. Align command and feedback semantics with Core Phase 4B. Do not introduce a second lifecycle reducer or treat adapter submission as device completion.
6. Define capability discovery, session ownership, error/timeout/device-loss states and the no-actuation default.
7. Add contract tests before any concrete hardware provider. Keep simulated contract evidence separate from target-device/hardware acceptance.

Do not make physical movement the first implementation task. The first deliverable is an internally consistent contract and ownership model that can be consumed by the separate Slide Scanning line without leaking controller implementation details into scan policy.

### Handoff update requirements

After each substantive slice, record:
- exact MICROSCOPE_CPP commit and branch;
- Controller, Motion or Stage sub-area changed;
- typed identities/contracts and mapping rules added;
- Core operation/feedback semantics consumed;
- tests written and actual CI result for that exact commit;
- provider/simulator status, explicitly labelled;
- physical controller model and exact hardware operations tested, if any;
- safety approval and remaining failure modes;
- next bounded task.

A successful package build or simulated-provider test does not establish physical movement, calibrated accuracy, repeatability or safe stop behavior.

## 7. Historical authoritative records

These links are retained as prior-art references, not as the active implementation branch:

- [Station milestones](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-development-milestones.md)
- [Station continuation handoff](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-continuation-handoff.md)
- [Stage mechanics and carrier architecture](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/stage-mechanics-and-carrier-architecture.md)
- [Equipment composition and stage architecture](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/equipment-composition-and-stage-architecture.md)
- [Historical ControllerCore](https://github.com/Shennagyp/MICROSCOPE/tree/station-development/swift/Packages/Controller/MicroscopeControllerCore)
- [Historical ControllerFunctions](https://github.com/Shennagyp/MICROSCOPE/tree/station-development/swift/Packages/Controller/MicroscopeControllerFunctions)
- [Historical MotionCore](https://github.com/Shennagyp/MICROSCOPE/tree/station-development/swift/Packages/Motion/MicroscopeMotionCore)
- [Historical StageCore](https://github.com/Shennagyp/MICROSCOPE/tree/station-development/swift/Packages/Stage/MicroscopeStageCore)

## 8. Active OPTIKA source references

- [OPTIKA v0.9 source branch](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9)
- [Stage specifications](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9/Libraries/Microscope.Core)
- [Core/Application contracts](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md)
- [v0.9 implementation plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md)
