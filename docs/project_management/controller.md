# Workstream — Controller and Motion

**Primary repository:** [MICROSCOPE](https://github.com/Shennagyp/MICROSCOPE)  
**Primary implementation line:** \`station-development\` (Swift Station); Manager records are maintained separately on \`refactor-2\`.  
**Status:** contracts and model packages exist; physical controller provider/actuation and end-to-end stage operation remain deferred.

## 1. Responsibility model

Controller, Motion and Stage are related but not interchangeable concepts.

- **Controller** represents a physical controller device, its stable identity, capabilities, resources and provider/session function bundle.
- **Motion** represents provider-neutral axes, endpoints, units, ranges, coordinate frames, actuator groups and immutable capability snapshots.
- **Stage** represents microscope-specific stage geometry, logical axes, carrier/rack/slot topology and qualification against Motion capability evidence.
- **Application/Station** coordinates a requested physical workflow and owns local execution lifecycle.
- **Manager** stores Station-scoped controller identities and registered stage-drive relationships. It does not directly execute hardware.

The controller provider must not infer stage topology; Motion must not import controller implementations; Stage qualification consumes Motion capability snapshots without opening hardware or issuing movement commands.

## 2. Development history

The MICROSCOPE repository contains several historical architecture lines:
- \`refactor-dotnet\`: an early .NET runtime/model proposal, including an accumulated-error-only DeviceBridge measurement boundary and stage calibration foundations. It is reference history, not the current distributed runtime architecture.
- \`refactor-2\`: the distributed .NET Manager model. Controllers are Station-scoped records; stage drive-channel records refer to those controller identities. Manager owns registration and shared records, not local physical execution.
- \`station-development\`: the independent Swift Station implementation. It introduced separate Controller, Motion and Stage Swift packages and the composition/persistence contracts around them.
- \`station-refactor-2\`: a later historical Station refactor line. Use the current \`station-development\` source and handoff as the active Station authority unless explicitly redirected.

The approved distributed design separates Manager-owned registration from Station-owned hardware interaction. Earlier single-process .NET hosting proposals must not be revived as the implementation authority.

## 3. Current implementation in Station source

The current Station tree contains these packages:

| Package | Responsibility | Current limitation |
|---|---|---|
| \`MicroscopeControllerCore\` | Controller/device/resource identities and immutable capability descriptors | No vendor SDK, SwiftUI, persistence adapter or physical command execution |
| \`MicroscopeControllerFunctions\` | Provider/session function bundles and provider registry | No concrete accepted physical controller provider |
| \`MicroscopeMotionCore\` | Provider-neutral linear/angular coordinate semantics, axis/sensor endpoint identity, units, ranges, coordinate frames and immutable capability snapshots | No controller dependency and no physical execution |
| \`MicroscopeMotionFunctions\` | Motion operation/provider-function registration | Contract/registration layer, not proof of motor execution |
| \`MicroscopeStageCore\` | Deterministic Stage/carrier qualification against immutable Equipment and Motion snapshots | No SDK, session, UI, persistence, mapping execution or physical movement |
| \`MicroscopeStageCore\` tests | Stage/motion topology and qualification tests | Source tests do not prove physical stage accuracy or safety |

The Manager side contains Station-scoped ControllerDevice records and stage-drive/channel references in its registration/domain model. The current Manager registry API has been reported as lacking a controller-device read query required by the Station projection; treat this as an integration gap until the exact current API proves otherwise.

MICROSCOPE_CPP also contains C++ Core stage specifications and tests. Those are microscope-equipment specifications, not a controller driver or proof of stage actuation.

## 4. Safety and evidence boundary

No physical movement is authorized by the presence of a model, function registry or qualified topology.

The Station milestone plan places controller/stage integration at S14. The current Station continuation record states that stage motion, controller registration/channel catalogue, drive assignment, Manager StageConfiguration submission, endstop binding and physical calibration remain deferred.

Until a separate reviewed acceptance authorizes it:
- no stage movement, homing, jogging or motor enable;
- no objective/nosepiece movement;
- no condenser/aperture/illumination actuator writes;
- no assumption that accumulated controller error is an absolute position;
- no assumption that a command acceptance is an acknowledgement, physical completion or verified position.

The earlier DeviceBridge design restricts Stage/Motion measurement input to an accumulated-error snapshot with unit, epoch, sequence, timestamp and error-semantics revision. Raw encoder position, step count, pulse count, digital endstop levels and instantaneous velocity are not substitutes for that contract. Preserve datum, channel calibration, revision and provenance requirements.

## 5. Current status

**Implemented at source/contract level:**
- Controller identity/capability package.
- Controller provider/session function registry package.
- Provider-neutral Motion axis and sensor endpoint models.
- Motion function registration package.
- Stage/motion topology and pure Stage qualification contracts/tests.
- Manager-side controller identity and stage-drive references.

**Not established:**
- Concrete vendor controller provider.
- Native controller communication and device-session lifecycle.
- Controller telemetry/feedback against physical hardware.
- Motion command execution.
- Endstop/sensor hardware integration.
- End-to-end Stage-to-controller mapping.
- Physical homing, movement, calibration, accuracy or repeatability.
- Station/Manager controller registry synchronization for the current branch.
- Safety acceptance.

Do not report controller functionality as hardware-complete.

## 6. Continuation handoff

**Active milestone:** controller/stage physical integration is deferred (Station S14).  
**Current work mode:** preserve contracts and record cross-project integration blockers; do not jump directly to motor commands.

### Next bounded task

The next responsible step is a contract reconciliation, not actuation:

1. Read \`docs/station-development-milestones.md\`, \`docs/station-continuation-handoff.md\`, \`docs/stage-mechanics-and-carrier-architecture.md\`, \`docs/equipment-composition-and-stage-architecture.md\` and the Controller/Motion/Stage package README files.
2. Reconcile Manager controller records and stage-drive references with the Station registration projection. Identify the precise missing Manager query and required revision/identity fields.
3. Define the provider-neutral endpoint/resource/session mapping without equating controller IDs, controller channel IDs, Motion endpoint IDs and Stage logical-axis IDs.
4. Preserve the split: Controller owns physical-device/provider identity; Motion owns kinematic and endpoint semantics; Stage owns microscope-stage topology and qualification.
5. Keep all movement commands disabled until the separate hardware-safety and execution acceptance is approved.

This task may clarify contracts and integration prerequisites. It must not claim a working controller provider or implement unapproved physical actuation.

### Handoff update requirements

After an approved controller slice, record exact repository/branch/commit, package changed, contract/test added, hosted CI result, target-Mac build/launch result, physical controller/stage model and exact hardware evidence. A package build cannot establish actuator operation or calibrated movement.

## 7. Authoritative records

- [Station milestones](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-development-milestones.md)
- [Station continuation handoff](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-continuation-handoff.md)
- [Stage mechanics and carrier architecture](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/stage-mechanics-and-carrier-architecture.md)
- [Equipment composition and stage architecture](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/equipment-composition-and-stage-architecture.md)
- [ControllerCore](https://github.com/Shennagyp/MICROSCOPE/tree/station-development/swift/Packages/Controller/MicroscopeControllerCore)
- [ControllerFunctions](https://github.com/Shennagyp/MICROSCOPE/tree/station-development/swift/Packages/Controller/MicroscopeControllerFunctions)
- [MotionCore](https://github.com/Shennagyp/MICROSCOPE/tree/station-development/swift/Packages/Motion/MicroscopeMotionCore)
- [StageCore](https://github.com/Shennagyp/MICROSCOPE/tree/station-development/swift/Packages/Stage/MicroscopeStageCore)
