# Microscope Platform Architecture — v0.9

**Status:** Authoritative target architecture for OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Date:** 2026-11-25  
**Scope:** Microscope domain model, C++ libraries, adapter integration, operation lifecycle, calibration, slides, persistence and frontend independence.

This document supersedes earlier application-level architecture descriptions wherever they conflict with the platform boundaries defined here. The initial C++17 Microscope.Core and Microscope.Application static-library targets and foundational contracts now exist in the repository. They are only the Phase 1 foundation: the microscope equipment model, configuration engine, operation executor, JSON loading, adapter composition, persistence and hardware integration are not yet implemented. The existing native application and camera pipeline remain the implementation baseline during migration.

## 1. Architectural decisions

The platform consists of two principal C++ libraries, infrastructure implementations and independent front ends.

1. **Microscope.Core** is the microscope engine. It owns equipment semantics, microscope behavior, configuration, operation planning and execution state, feedback evaluation, calibration, motion, acquisition, slide/image semantics and domain recovery.
2. **Microscope.Application** is a separate C++ application library. It loads microscope JSON definitions and deployment data, registers and composes adapters, creates and manages Core runtimes, coordinates application-level workflows and exposes a frontend-neutral API.
3. Hardware and storage adapters implement contracts declared at the appropriate Core boundary. They perform external I/O and report observations; they do not own microscope policy.
4. Front ends consume Microscope.Application. They do not implement microscope behavior or call device SDKs.
5. Each physical microscope is described by JSON data. Runtime state, calibration records and slide data are persisted separately.
6. Microscope operations are asynchronous, ordered, correlated and feedback-driven. A request being accepted is not equivalent to a physical action completing or a microscope configuration being verified.
7. Optical and motion calibration are independent. Optical calibration starts from equipment specifications and can be refined with a microscope ruler slide. It is reused for matching optical configurations, not repeated for every slide.
8. Sample, Slide and Tile records represent source material, physical preparation and acquired fields separately. Acquisition is recorded against a Slide and the actual Microscope; it cannot start without an active, valid slide session.
9. The live microscope viewer and saved-image viewer are separate presentation workflows. Annotations and measurements are persistent structured data, never destructive overlays on source pixels.
10. Existing camera acquisition, frame-pool, lease and preview behavior must be preserved during migration.

## 2. Architectural objective and invariants

The architecture must support different microscope models, hardware configurations, adapter implementations and front ends without duplicating microscope rules.

The following invariants govern implementation:

- Equipment definitions describe physical components and relationships; they do not contain executable behavior.
- Microscope behavior is owned by Microscope.Core, not by objective classes, hardware adapters, front ends or the application library.
- Microscope.Application composes and exposes the Core; it does not reimplement Core behavior.
- Core calculations and transition decisions are pure functions wherever practical.
- External effects are explicit ports with typed commands and typed feedback.
- The Core never claims that a device operation completed or a state was verified without corresponding evidence.
- Requested, observed and verified state are distinct.
- Core runtime state has one serialized logical owner per microscope instance. Adapter callbacks do not mutate it directly from device threads.
- Every asynchronous feedback item is correlated to the operation, step, command and device that produced it.
- Calibration claims preserve measurement provenance and uncertainty.
- Unknown physical position or scale remains unknown; it is never replaced by a fabricated value.
- A slide's historical acquisition configuration and calibration evidence remain interpretable after equipment or calibration changes.
- Frontends may differ in technology and layout but use the same application semantics.
- No generic plugin framework, visual node editor, robotics framework or universal workflow engine is introduced without a concrete requirement.

## 3. Principal layers and dependency direction

~~~text
Front ends
  macOS SwiftUI / AppKit
  CLI
  Web or remote client
       |
       v
Microscope.Application (C++)
  definition catalog and JSON loading
  deployment loading
  adapter factory registry and composition
  runtime/session manager
  application commands, queries and notifications
  frontend-neutral state projections
       |
       v
Microscope.Core (C++)
  microscope equipment model
  configuration and operation planning
  operation runtime and feedback evaluation
  calibration and motion
  acquisition, slides and image documents
       ^
       |
Core-defined ports and contracts
       |
       +---- Camera adapters (AmScope and future vendors)
       +---- Stage / axis adapters
       +---- Optical selector adapters
       +---- Illumination adapters
       +---- Persistence adapters
       +---- Image and DICOM adapters
~~~

Dependency rules:

- Microscope.Core does not depend on Microscope.Application, JSON, SwiftUI, AppKit, a camera SDK, a particular filesystem layout or DICOM.
- Microscope.Application depends on Microscope.Core.
- Adapter implementations depend on Core contracts and their vendor/platform SDKs. They do not depend on the application frontend.
- The executable or host links the application library and the concrete adapter implementations, registers their factories, and supplies deployment-specific settings.
- A macOS bridge is a thin language boundary between Swift/Objective-C++ and Microscope.Application; it is not a third business-logic layer.
- A web service host exposes the same application library over a transport protocol; it is a host, not a second application engine.

The application library contains an explicit adapter registry/composition mechanism. Concrete drivers are registered by the host using factory functions. This is not a dynamic plugin platform or service locator framework.

## 4. Microscope.Core — the microscope engine

### 4.1 Responsibilities

Core owns:

- Equipment identities, specifications, component relationships and optical topology.
- Device capabilities and operating constraints as domain data.
- Objective, contrast, light-path and camera configuration semantics.
- Configuration profile resolution, compatibility validation and transition planning.
- Ordered operation plans, dependencies, progress state and completion criteria.
- Normalized feedback evaluation and reconciliation of requested versus observed state.
- Motion-axis semantics, position evidence and motion calibration.
- Optical calibration and pixel-to-specimen measurement conversion.
- Microscope workflows, including objective changes, contrast changes, calibration and acquisition.
- Slide, tile, image-frame and derived-image semantics.
- Active slide-session preconditions for acquisition.
- Annotation and measurement geometry/provenance.
- Domain-level timeout, cancellation, failure, partial-completion and recovery decisions.

Core does not own vendor SDK calls, JSON parsing, UI state, image-file codecs or operating-system resource APIs. It defines the contracts through which these effects are supplied.

### 4.2 Functional core and runtime

Core combines pure functional decisions with a runtime that owns real workflow lifecycles.

Pure operations include:

- Resolve a requested microscope configuration from equipment data and profiles.
- Validate compatibility and constraints.
- Calculate a transition plan and its step dependencies.
- Fit optical and motion calibration models from supplied observations.
- Convert coordinates and physical units using validated calibrations.
- Reduce a current operation state and a normalized event into a new operation state.
- Evaluate whether an observation satisfies a step's completion condition.

The Core runtime performs the imperative orchestration:

- Accepts a domain intent.
- Invokes the pure planner.
- Creates and owns the active operation state.
- Dispatches effects through injected ports.
- Receives and serializes adapter feedback.
- Calls pure transition/evaluation functions.
- Schedules dependent steps or stops/reconciles after failure.
- Publishes snapshots and operation events.

The runtime is not a generic workflow platform. It is the execution mechanism for microscope-domain operations.

## 5. Equipment model

### 5.1 Microscope composition

A microscope definition describes the installed instrument:

~~~text
Microscope
├── Identity
├── OpticalSystem
│   ├── Components
│   │   ├── Objectives
│   │   ├── Lenses and relay optics
│   │   ├── Condenser and condenser components
│   │   ├── Filters and optical selectors
│   │   ├── Phase components
│   │   ├── Polarization and DIC components
│   │   ├── Beam splitters and mirrors
│   │   └── Other optical elements
│   └── LightPaths
├── ImagingSystem
│   └── Cameras and sensor modes
├── IlluminationSystem
│   └── Sources and controls
├── Stage
│   └── Axes
└── ConfigurationProfiles
~~~

These are logical model boundaries, not separate libraries or mandatory class hierarchies.

### 5.2 Optical topology

A light path is an explicit physical optical route through installed components. Its representation preserves component identity, physical port adjacency and ordered propagation.

- A physical OpticalConnection is an undirected adjacency between two ports on different components. Endpoint storage order does not encode propagation direction.
- Ordered OpticalPathStep values determine propagation direction through each component. Entry/exit port capabilities are validated for that direction.
- One physical connection can be reused by different paths traversing the same adjacency in opposite directions, provided the involved ports support each traversal.
- A component shared by multiple paths has one EquipmentComponentId and may participate in multiple paths.
- A beam splitter or selector may create alternative paths using distinct physical ports for the branches.
- Cameras attach to appropriate output ports/paths, not merely to an objective.
- The topology describes equipment; it is not a visual node editor, executable behavior graph or configuration resolver.
- Configuration profiles select valid routes and component states in Phase 3.

The model must not assume that all microscopes have the same optical ordering or component inventory.

### 5.3 Objectives and magnification

ObjectiveSpecification records attach to the installed physical EquipmentComponentId; they do not create a second identity or per-objective behavior class. They include nominal magnification, numerical aperture, working distance, chromatic/field correction, immersion medium and optional design coverglass thickness.

Nominal effective magnification is derived from the relevant optical train, including objective and magnification-affecting relay/tube optics. It is not assumed to be simply the objective marking in every optical design.

The nominal camera object-space pixel size can be estimated from sensor pitch and effective magnification:

    S_nominal = P_sensor / M_effective

The result must account for the actual output-pixel mapping. Sensor pitch alone is not enough when binning, ROI, subsampling, demosaicing, cropping or resampling changes the relation between sensor pixels and delivered image pixels.

Nominal values are estimates until refined by a suitable reference calibration.

### 5.4 Cameras

CameraSpecification attaches to the installed ImagingDevice EquipmentComponentId. It describes:

- Active sensor dimensions and independent X/Y sensor pixel pitch.
- Supported output modes with unique CameraModeId values and pixel-format codes.
- Sensor ROI origin/extents, hardware binning factors and delivered image dimensions as distinct data.
- The net X/Y sensor-pixel-per-delivered-pixel mapping, derived independently from ROI and delivered dimensions.
- Capability declarations such as exposure/gain only when specified by the installed equipment data.
- The optical output/light path to which the camera component is connected.

Do not assume sensor pitch alone maps directly to delivered pixels or that horizontal and vertical mapping ratios are equal.

The adapter supplies actual device capabilities and observations. The Core validates them against the definition and requested configuration.

### 5.5 Illumination and condenser

IlluminationSpecification attaches to an installed IlluminationDevice component and contains one or more independently identified channels. A channel may declare a spectral band and intensity-control kind; continuous control requires a finite range with explicit units. One source can therefore expose multiple channels with different spectra and control semantics.

CondenserSpecification attaches to an installed OpticalElement component and declares design, immersion medium, aperture NA range and optional working distance. The minimum aperture NA may be zero; it must not exceed the positive maximum.

Illumination and condenser data remain independent of objective classes. Configuration profiles define compatibility and selection relationships between objective, optical path, contrast method, condenser and illumination system in Phase 3.

### 5.6 Stage and axes

A stage contains a collection of axes. An axis has a motion type and unit:

- Linear: e.g. X, Y or Z displacement.
- Rotary: e.g. theta or another angular degree of freedom.

XYZ are common axis identifiers, not a hard limit on the model.

StageSpecification attaches to an installed MechanicalAssembly component and contains one or more uniquely identified AxisSpecification values. Axis specifications focus on motion capability and measured performance, not mechanical construction details. Relevant properties include:

- Motion type (linear/rotary/other/unknown) and coordinate unit.
- Finite signed operating range and limits.
- Positive nominal command/readout resolution.
- Optional independently measured accuracy (maximum absolute error) with test conditions.
- Optional precision/repeatability with its metric and test conditions.
- Optional measurement uncertainty with test conditions.
- Position feedback capability and measurement location/provenance.
- Calibration identity and validity, added with calibration behavior in Phase 6.

Accuracy, precision/repeatability, nominal resolution and uncertainty are distinct. Unknown metrics remain absent or explicitly unknown; they are never inferred from one another.

Resolution, accuracy, precision and repeatability are not interchangeable. Nominal resolution does not establish physical accuracy. Accuracy must not be calculated by blindly multiplying component specifications.

The position model distinguishes:
- Requested/commanded position.
- Device-reported position.
- Calibrated estimated position.
- Unknown position.

If an axis has no position feedback, a command-derived value is an estimate, not a measured physical position.

## 6. Configuration model and functional resolution

### 6.1 Equipment data versus configuration data

The equipment model describes what physically exists and what it supports.

Configuration profiles describe valid desired arrangements and relationships. They are declarative data, not executable behavior attached to objective/filter classes.

A profile can specify:

- Applicability/selection conditions.
- Required components and component states.
- Required light paths.
- Illumination and condenser settings.
- Camera presets and exposure policies.
- Dependencies, exclusions and compatibility constraints.
- Whether a value is mandatory, recommended or optional.

### 6.2 Independent configuration dimensions

Objective, contrast method, light path and camera are independent request dimensions. The final operating configuration is resolved from their relationships.

Examples:
- Selecting 4× can resolve the corresponding brightfield illumination and camera preset.
- Selecting phase contrast resolves the compatible phase components, condenser arrangement and illumination.
- Selecting DIC resolves its actual compatible polarization/prism/analyzer arrangement described by the instrument definition.
- Selecting another camera or output path changes the available optical route and may change the applicable optical calibration.

The implementation must not assume a universal hardcoded filter sequence for every microscope.

An exposure preset is distinct from an exposure policy. Objective selection may load a useful initial preset, but manual or automatic exposure remains an independently selected policy subject to camera and acquisition constraints.

### 6.3 Pure configuration pipeline

~~~text
Configuration request + equipment definition + current state
                         |
                         v
               Pure configuration resolver
                         |
                         v
              Compatibility / constraint validation
                         |
                         v
             Desired configuration + diagnostics
                         |
                         v
                Pure transition planner
                         |
                         v
                Ordered dependency plan
                         |
                         v
                 Core runtime execution
~~~

Composition must be conflict-aware. Unrestricted last-write-wins merging is prohibited. Contradictory profile contributions produce explicit validation diagnostics.

## 7. Operations, actions, feedback and completion

This section is normative. It resolves the distinction between a user request, a hardware command, adapter feedback and verified operation completion.

### 7.1 Responsibility split

**Core knows:**
- What operation was requested.
- Why the operation is needed.
- Which steps and dependencies are required.
- Which command must be sent and to which logical device.
- What evidence is required to consider each step complete.
- Whether received feedback satisfies the completion condition.
- Which step can execute next.
- Whether the overall operation succeeded, failed, timed out, was cancelled or partially completed.

**Adapter knows:**
- How to translate a typed command to its SDK/protocol/device.
- Whether it accepted responsibility for dispatching that command.
- What the SDK/device reported.
- How to obtain progress and hardware state observations when supported.
- How to normalize vendor-specific errors and observations into Core contract values.

**Application knows:**
- Which adapter implementation is registered and bound to each logical endpoint.
- How the Core runtime is created and kept alive.
- How operation snapshots/events are exposed to front ends.

The Core never invents hardware feedback. It receives feedback through its injected adapter ports and derives domain-level verification from that evidence.

### 7.2 Do not conflate command events with device events

The following distinctions are mandatory:

| Event/fact | Origin | Meaning |
|---|---|---|
| OperationRequested | Application/frontend intent | A user or application requested a domain operation |
| CommandDispatched | Core runtime | Core invoked the adapter port with a command |
| AdapterAccepted | Adapter | Adapter accepted responsibility for processing/dispatching the command |
| DeviceAcknowledged | Adapter, based on SDK/device response | SDK/device acknowledged or accepted the command, when this distinction is available |
| DeviceProgress | Adapter | Progress/status was reported by the SDK/device |
| DeviceActionCompleted | Adapter | SDK/device/driver reports that this individual hardware action completed |
| DeviceStateObserved | Adapter | A hardware state value was read or reported |
| StepVerified | Core | Core evaluated evidence and the step's completion predicate successfully |
| OperationSucceeded | Core | All required steps reached their required completion/assurance conditions |
| OperationFailed | Adapter or Core, with explicit origin | A device/adapter failure or domain/workflow failure occurred |
| TimeoutElapsed | Core runtime | A Core-owned deadline expired without sufficient feedback |

CommandDispatched is known by the Core because it invoked the port. It is not adapter feedback.

DeviceActionCompleted is not the same as OperationSucceeded. A device can report that a selector stopped moving without proving that it selected the requested objective.

StepVerified and OperationSucceeded are Core conclusions, not device events.

TimeoutElapsed is produced by the Core runtime's deadline mechanism. An adapter may separately report a device timeout, but it must identify that as a device/adapter error.

### 7.3 Feedback envelope and correlation

Every adapter feedback item carries a runtime/device scope:

- MicroscopeId.
- RuntimeId and RuntimeGeneration.
- Logical DeviceId.
- DeviceSessionGeneration for the currently bound/open logical-device session.

Core issues a new DeviceSessionGeneration for each device open/reopen within a runtime and passes it to the adapter. Adapters preserve the generation associated with the callback's originating session. Feedback from an earlier device session is rejected even if the enclosing microscope runtime remains active.

Feedback associated with an active operation additionally carries OperationId and StepId as one indivisible correlation, plus CommandId where related to a dispatched command. Unsolicited telemetry has no operation correlation; it cannot satisfy an active operation's completion predicate.

Every item also carries a normalized feedback fact category, source/provenance, optional adapter/device source sequence, optional device timestamp with a positive explicit tick rate and a non-empty clock-domain label when one is supplied, and a typed payload or normalized diagnostic. Initial adapter fact categories are DeviceAcknowledged, DeviceProgress, DeviceActionCompleted, DeviceStateObserved and AdapterFailure. DeviceAcknowledged requires operation/step/command correlation and SDK/device-event provenance; DeviceActionCompleted requires operation/step correlation and hardware-readback/SDK/device-event provenance. Operator confirmation and calibrated estimates remain observations and cannot be promoted into acknowledgement/completion facts. AdapterFailure uses AdapterReport provenance, not sensor evidence. Other fact categories may be operation-correlated or unsolicited where their semantics permit. FeedbackMetadata factory validation rejects missing correlation or incompatible provenance. CommandDispatched and TimeoutElapsed remain Core runtime facts.

Adapter/device source sequence is scoped to DeviceSessionGeneration; zero may be a valid first sequence and absence means the source does not report one. When supplied, the adapter preserves ordering for that session; a wrapping/resetting source counter requires a new device-session generation rather than ambiguous reuse. On ingress, Core wraps successfully enqueued feedback in a receipt with a separate positive FeedbackIngressSequence scoped to MicroscopeId, RuntimeId and RuntimeGeneration, plus a monotonic steady-clock receipt time. The sequence is assigned atomically with successful FIFO enqueue, in the same synchronization order as queue insertion, so concurrent callbacks cannot receive sequence values in an order different from processing. QueueFull/Closed attempts do not create receipts. The sequence never wraps or reuses values; exhaustion retires the ingress/runtime generation before another sequence stream begins. The receipt sequence orders arrival only within that runtime incarnation; cross-scope ordering is rejected. The receipt factory rejects a sequence whose scope does not match the feedback envelope. It is not interchangeable with device source sequence. The steady-clock value is runtime-local for ordering/deadlines, not a durable wall-clock timestamp.

Ingress submission outcomes are Enqueued, QueueFull and Closed. Enqueued transfers ownership of the feedback to Core; it does not mean that an adapter accepted a hardware command. On QueueFull, the ingress itself must latch feedback loss, mark affected device/runtime state unknown and wake the serialized runtime through a control path independent of the full queue before returning. It cannot rely on a later adapter callback to report the loss or wait for queue capacity to recover. Correlated loss blocks verification of affected work; uncorrelated telemetry loss makes that device's observed state uncertain. The condition is cleared only after explicit reconciliation/readback establishes the required state. Closed means that runtime ingress has been disposed.

The Core uses these identities/generations to reject duplicate, stale, unrelated or late feedback. A callback from a previous operation, runtime generation or device-session generation must not update the current state merely because it concerns the same physical device.

### 7.4 Observation provenance

A state observation includes how it was obtained. Examples:

- Hardware readback/encoder.
- SDK status.
- Device event.
- Operator confirmation.
- Calibration-based estimate.
- Adapter report.
- Unknown/not observable.

The Core must preserve this distinction. An operator-confirmed manual filter is not sensor-verified. An estimated Z displacement is not measured specimen depth. A command acknowledgment is not physical position feedback.

Completion predicates can require a particular evidence quality. If the equipment cannot provide that quality, the Core reports the weaker assurance or leaves the step awaiting an observation/operator action. It must not silently promote weaker evidence into verified state.

### 7.5 Operation and step state

The Core runtime holds the current operation record and step states while work is active. It does not need to retain every event forever, and this design is not event sourcing.

Suggested operation lifecycle:

- Accepted.
- Running.
- AwaitingOperator.
- Verifying.
- Succeeded.
- Failed.
- PartiallyCompleted.
- CancellationRequested.
- Cancelled.

Suggested step lifecycle:

- Pending.
- Dispatched.
- AdapterAccepted.
- DeviceAcknowledged.
- Executing.
- AwaitingOperator.
- AwaitingObservation.
- Verifying.
- Verified.
- Failed.
- TimedOut.
- Skipped.

The exact public enum names can evolve, but the semantic distinctions must remain.

A current operation snapshot includes its identity, requested target, current status, ordered/dependency-linked steps, initial state, latest observed state, verified outcomes and diagnostics. Historical operation records are persisted when required by recovery and audit requirements.

### 7.6 Ordered action plan: dependency graph, not a universal fixed script

The Core planner produces steps and dependencies from the selected configuration, equipment definition, current state and safety constraints.

For a transition from 4× brightfield to 40× phase contrast, a possible plan is:

1. Prevent a new acquisition from starting during the incompatible transition.
2. Establish any required safety condition (for example, close a shutter or reduce illumination) if the microscope definition requires it.
3. Change the objective and wait for the supported completion evidence.
4. Select/confirm the required phase optical component.
5. Apply the compatible condenser arrangement.
6. Apply illumination settings.
7. Apply camera preset/policy settings.
8. Verify the final effective configuration.

This is an example only. The Core must not hardcode this same sequence for all microscopes. Dependencies in the profile and device capabilities determine the valid order. Independent steps can run concurrently only when their resource claims and safety constraints permit it.

A step cannot be considered finished merely because the command function returned.

### 7.7 Execution cycle

~~~text
Intent
  -> Core validates request and preconditions
  -> Core plans operation steps and dependencies
  -> Core creates OperationId / StepId / CommandId
  -> Core records CommandDispatched
  -> Core invokes injected adapter port
  -> Adapter reports dispatch acceptance/rejection
  -> Adapter reports device acknowledgment/progress/completion/observation/failure
  -> Core runtime serializes incoming feedback
  -> Pure transition/evaluation function updates operation state
  -> Core verifies step completion predicate
  -> Core schedules dependent step(s) or handles failure
  -> Core verifies the overall desired configuration
  -> Core publishes terminal operation result
  -> Application forwards frontend-neutral notifications
~~~

Adapter callbacks must enqueue feedback into the Core runtime. They must not mutate Core state directly on SDK or device threads.

### 7.8 Failure, timeout, cancellation and partial completion

On a failed step:

1. Stop scheduling dependent steps.
2. Determine which already-dispatched operations are still running and apply their cancellation/stop contracts where available.
3. Read/reconcile actual device state where possible.
4. Preserve completed, failed, skipped and unknown step results.
5. Determine whether a safe recovery/compensation plan exists.
6. Never assume the microscope returned to its initial state.
7. Block acquisition when the resulting configuration is invalid or insufficiently known.
8. Publish a precise failure and recovery state.

Compensation is not automatically a rollback. Reversing a physical operation may be unsafe or impossible.

Timeouts are owned by the Core runtime. The runtime starts a deadline when a step enters the relevant waiting state. If required feedback does not arrive, it emits a timeout event for that step. Late feedback is correlated and reconciled; it cannot silently turn a previously failed operation into a successful one.

Cancellation is a request, not proof that the hardware stopped. The Core waits for stop/abort feedback or reports that the final device state is unknown.

### 7.9 Requested, observed and effective state

Keep three separate state concepts:

- **Desired state:** the configuration the user requested.
- **Observed state:** the latest facts reported by adapters or operator confirmation.
- **Effective verified configuration:** the subset of desired configuration whose requirements have been established with the required evidence.

A requested objective cannot be displayed as verified merely because its command was sent. If a device lacks feedback, the application must expose the weaker assurance clearly.

## 8. Microscope.Application — C++ application library

### 8.1 Responsibilities

Application is a separate C++ library that depends on Core. It is frontend-independent and provides:

- Microscope definition catalog and JSON loading.
- Deployment definition loading.
- Schema version checks and definition migration.
- Adapter factory registry and composition.
- Creation/opening/closing of Core microscope runtimes.
- Runtime lifecycle and multi-microscope management.
- Application-level microscope and slide workspace services.
- Coordination between Core persistence requests and storage implementations.
- Frontend-neutral commands, queries, operation handles and notifications.
- Frontend-neutral state snapshots.
- Live-frame stream exposure as a separate high-throughput contract.

Application delegates microscope-domain decisions to Core. It must not contain another objective/contrast resolver, calibration implementation, operation state machine or acquisition workflow.

### 8.2 Public application API

The API is grouped into:

**Commands:** open/close microscope, request configuration, start calibration, open/create/close slide, start/stop acquisition, edit annotations.

**Queries:** list microscopes, read definition/availability, read current and effective state, obtain calibrations, list/open slides, inspect acquisition and image documents.

**Notifications:** connection changes, operation progress/completion/failure, configuration state changes, calibration progress, slide session changes and acquisition updates.

Commands that initiate asynchronous work return an operation identity/handle. They do not block the frontend until a motor or camera action finishes.

A frontend can subscribe to operation/application events and query snapshots. Notifications are projections of Core/Application state, not the source of truth.

### 8.3 Multiple front ends

- macOS uses SwiftUI/AppKit and a thin Objective-C++ bridge to the C++ application API.
- CLI uses the same C++ application API directly.
- A web frontend connects to a C++ service host exposing the same application library over HTTP/WebSocket or another selected transport.
- Other frontends can be introduced without moving microscope policy into them.

Multiple frontends do not imply multiple independent processes may directly open the same physical camera/stage. A physical microscope should have one controlling runtime at a time. If several frontends must control/view the same microscope concurrently, they connect to one shared application/service host. Runtime-level resource ownership prevents duplicate claims within that host.

### 8.4 Frontend state and presentation purity

Presentation receives immutable or snapshot-style view state and emits user intents. It may own transient interaction state (selected ruler marks, active annotation tool, selected panel), but it does not own authoritative microscope state, operation sequencing, calibration or persistence policy.

The saved-image viewer can open slide documents without a connected microscope. The live microscope viewer is acquisition-oriented and uses a separate frame stream.

High-rate frame transfer must not be forced through generic application state notifications. The existing frame lease/mailbox path remains a specialized stream contract.

## 9. Microscope definitions and JSON

### 9.1 One definition per physical microscope instance

Each actual installed microscope has a stable MicroscopeId and a JSON definition file. The file describes what physically exists on that instrument, not executable code.

Example layout:

~~~text
Definitions/
├── Microscopes/
│   ├── optika-b293-01.json
│   └── another-microscope-01.json
├── Deployments/
│   ├── optika-b293-01.deployment.json
│   └── another-microscope-01.deployment.json
└── Schemas/
    ├── microscope-definition.schema.json
    └── microscope-deployment.schema.json
~~~

No actual OPTIKA B293 component inventory should be invented. The definition must be based on the user's installed optics, camera, illumination, condenser and stage.

### 9.2 Definition versus deployment

**Microscope definition JSON** describes:
- Stable microscope identity, manufacturer and model.
- Installed component identities and specifications.
- Optical topology and light paths.
- Objective inventory.
- Cameras and sensor modes.
- Illumination and condenser capabilities.
- Stage axes and motion performance specifications.
- Supported configuration profiles and relationships.
- Schema version.

**Deployment JSON** describes:
- MicroscopeId referenced by the definition.
- Adapter type keys registered by the host.
- Logical equipment endpoint to adapter binding.
- Host/device identifiers and non-secret connection settings.
- Deployment-specific availability or selection settings.

This separation prevents a driver or host change from modifying physical microscope truth.

### 9.3 JSON handling

- Microscope.Application owns discovery/loading of definition and deployment files and schema-version handling.
- The JSON parser maps external data into typed Core definition values.
- Microscope.Core validates semantic invariants after parsing.
- Core does not parse JSON or depend on a JSON library.
- JSON files contain no C++ class names, function names, function pointers or executable behavior.
- Schema migrations are explicit. Unknown major schema versions are rejected rather than guessed.
- Static microscope definitions do not hold transient runtime state, calibration observations or slide history.

Adapter factory names in deployment JSON must resolve through the explicit factory registry. Unknown or unavailable adapters produce a deployment diagnostic.

## 10. Calibration

Calibration is owned by Core as independent models and workflows. The application supplies persistence and live observations through adapters.

### 10.1 Optical calibration: image pixels to specimen dimensions

Two levels are supported:

**Nominal estimate:** calculated from camera and optical specifications, including sensor pitch, objective, tube/relay optics and output-pixel mapping.

**Measured calibration:** user refines the estimate using a microscope ruler/stage-micrometer slide in the live camera view.

For a known reference distance and measured image pixel distance:

    S_measured = L_known / L_pixels

Start with separate X and Y pixel scales when the observations support them. Multiple ruler intervals improve the fit and reveal inconsistent selections. More complex affine or distortion models are added only when measurements demonstrate a need.

Retain:
- Original reference specification.
- Selected image positions and known distances.
- Optical configuration key.
- Estimated X/Y scales.
- Fit/residual quality and uncertainty.
- Calibration method, timestamp and revision.
- Operator/reference provenance.

The initial scale may be calculated immediately. The measured refinement is reused whenever the matching optical geometry configuration is selected.

### 10.2 Optical calibration identity and invalidation

The calibration key includes geometry-affecting factors:
- Microscope instance.
- Objective identity and relevant optical configuration.
- Camera identity.
- Light path and magnification-affecting optical components.
- Camera sensor/readout mode.
- ROI, binning, crop and output resampling that alter pixel mapping.

Changing exposure or illumination intensity does not ordinarily invalidate image scale. A contrast component only changes the calibration key if it changes image geometry or magnification.

A new measured calibration creates a new immutable revision. Historical acquisitions and measurements continue to reference the revision used at their creation.

### 10.3 Motion calibration: command/feedback units to physical axis movement

Motion calibration is independent from optical calibration and is scoped to an axis and its relevant device/configuration.

For a linear axis, it can map step or encoder counts to micrometers. A rotary axis maps its device coordinate to an angular unit.

The result retains observations, fitted mapping, residuals, accuracy, repeatability/precision evidence and uncertainty. Nominal command resolution is not reported as physical accuracy.

No mechanical transmission construction details are part of this application model.

If position feedback is absent, the Core records command-derived position as estimated, not measured.

### 10.4 No slide-to-stage positioning calibration

This architecture does not require a transformation from microscope stage coordinates to a physical slide origin. Do not introduce a per-slide stage-registration calibration context.

Stage coordinates can be stored as stage-coordinate observations with their provenance. They must not be re-labelled as slide-global positions.

### 10.5 Slide/acquisition calibration history

Calibration records are owned by the microscope/configuration calibration stores, not duplicated inside every slide.

Each acquisition and physical measurement preserves the calibration revision and relevant values used. A slide is therefore interpretable historically without owning another copy of the calibration system.

## 11. Samples, slides, tiles and acquisition

### 11.1 Specimen, preparation and acquisition identities

The model distinguishes three physical/data concepts:

- **Sample:** the collected biological or material specimen. It represents source material before or independent of a particular slide preparation.
- **Slide:** a physical prepared/mounted substrate made from a sample. It represents a preparation, not the sample itself.
- **Tile:** an acquired image field associated with a particular slide.

The baseline cardinality is:

~~~text
Sample 1 ───── 0..N Slides 1 ───── 0..N Tiles
   │                         │
   └── SampleType            └── SlideType
~~~

One sample may produce multiple slides. Each slide belongs to one sample in this baseline model. Do not introduce a many-to-many contribution model unless a concrete workflow requires one physical slide to contain material from multiple samples.

SampleType describes the source material/category (for example, blood, bone marrow aspirate, kidney tissue or liver tissue). SlideType describes the preparation/mounting method or preparation category (for example, blood film, bone-marrow smear/aspirate preparation, tissue section or cytology preparation). These taxonomies must not be conflated: a source material does not uniquely determine its preparation.

A sample is not owned by a microscope. A microscope workspace may organize or filter samples and slides for convenience, but instrument association is established by acquisition records:

~~~text
Microscope 1 ───── 0..N AcquisitionRuns
Sample 1 ───── 0..N Slides 1 ───── 0..N Tiles
                         ▲
                         └── AcquisitionRun references the slide/tile being acquired
~~~

The user-facing workspace may show a microscope-scoped collection, but this is an application/workspace view rather than physical or domain ownership.

### 11.2 Sample and sample type

A Sample has:
- Stable SampleId.
- SampleTypeId when classified; an unclassified sample remains representable without inventing a type.
- User-facing label and optional external accession/reference. An external accession is not the SampleId.
- Collection/source metadata and timestamps where available.
- Creation/update metadata and optional workflow-specific descriptive metadata.

Sample identity and source metadata must remain independent of slide preparation, microscope identity and acquisition history. A sample can exist before any slide is prepared and may have multiple prepared slides. Patient or other source-system identifiers, when present in a particular deployment, are metadata with their own access/retention policy; they must not be used as the platform's primary identity.

### 11.3 Slide and slide type

A Slide has:
- Stable SlideId.
- Required SampleId identifying its source sample.
- SlideTypeId when classified.
- User-facing label and optional external slide/accession reference.
- Versioned preparation snapshot, including the SlideType version used.
- Creation/preparation timestamps where available.
- A storage/package reference maintained by the persistence adapter.

A Slide does not carry MicroscopeId as ownership metadata. Its acquisition history is obtained through the Slide's AcquisitionRun records, which identify the actual microscope used.

Slide types are reusable preparation templates such as blood film, bone-marrow smear/aspirate preparation, kidney tissue section, liver tissue section or custom preparation types. They describe how material is prepared on the slide, not the source material taxonomy.

A slide receives a preparation/configuration snapshot when created. Updating a reusable slide type must not silently change existing slides.

### 11.4 Tile semantics and positions

A Tile has:
- Stable TileId and owning SlideId.
- Optional user-facing field label or display-order hint.
- Optional position observations, each with coordinate frame, units, source and uncertainty/provenance when available.
- Creation metadata and optional descriptive metadata.

A Tile is the persistent identity of an acquired field/image region within a slide. Its frames are related through ImageFrame.TileId; the Tile does not duplicate frame payload metadata or maintain a second authoritative frame list.

A tile may have physical position metadata when supported by the microscope. Position is optional. The system must support unpositioned fields and must not fabricate physical XYZ coordinates.

A tile's sequence/index or virtual navigation order is not a physical slide coordinate.

The architecture does not require whole-slide scanning or stitching. It supports individual fields of view such as blood-film and bone-marrow cell acquisition workflows.

### 11.5 Mandatory active slide session

No acquisition may start without a valid active slide session.

This condition is enforced by Core when handling acquisition requests, not merely by disabling a button in a frontend.

Application coordinates folder selection/opening and persistence. Core owns the active-session precondition and rejects acquisition without it.

Switching or closing a slide during acquisition requires a safe stop/completion and recovery path.

### 11.6 Acquisition plan versus run

AcquisitionPlan is the intended sequence and settings for one Slide. It contains:
- AcquisitionPlanId and SlideId.
- Requested microscope configuration and acquisition policy.
- Intended field/tile targets and capture dimensions (for example, focal planes and exposure brackets) where applicable.
- Plan revision and creation metadata.

The plan is immutable once execution begins. Editing the intent after execution requires a new plan revision/identity; it must not rewrite the plan already referenced by a run.

AcquisitionRun is the actual execution record for one Slide and one physical Microscope. It contains:
- AcquisitionRunId, AcquisitionPlanId, SlideId and actual MicroscopeId.
- Runtime/operation correlation and start/end timestamps.
- Outcome/status, including incomplete and partially completed outcomes.
- Effective microscope configuration and calibration revisions used.
- Device observations and actual camera values, with provenance.
- Produced TileIds and ImageFrameIds.
- Completed, missing and failed steps/frames.
- Errors, interruptions and recovery checkpoints/status.

A run records what happened, not merely what was planned. The planned settings and actual observed settings remain separately inspectable. A run cannot be reassigned to another Slide or Microscope after execution.

### 11.7 Frames, Z and exposure

The data model must support irregular acquisition sets, not assume a dense fixed tensor.

Each original ImageFrame has:
- ImageFrameId, AcquisitionRunId and TileId.
- Immutable pixel-payload reference and integrity metadata.
- Capture timestamp, delivered pixel dimensions and pixel format.
- Source camera identity and the applicable readout/ROI/binning context.
- Actual exposure and other camera settings when supported/reported.
- Relevant optical configuration/calibration references and source geometry.
- Position/focus observations when available, preserving source and uncertainty.

Camera-specific fields remain capability-dependent; an unavailable readback is absent/unknown, not replaced with the requested value. Run-level configuration/calibration references may be inherited by frames when identical; frame-level overrides are recorded when actual capture conditions differ.

Z planes and exposure brackets are explicit relationships or typed capture dimensions, not inferred from array position. HDR combines exposures for the same field and focal plane; it must not merge different fields or different Z planes. Focus stacking is a separate operation across focal planes. Original frames remain immutable.

Incomplete runs are recoverable and marked incomplete.

### 11.8 Identity, ownership and cross-record invariants

All persisted entities use distinct, stable, opaque identifiers: SampleId, SlideId, TileId, AcquisitionPlanId, AcquisitionRunId, ImageFrameId and DerivedImageId. MicroscopeId remains the identity of the physical instrument. Type identifiers such as SampleTypeId and SlideTypeId are separate from instance identifiers.

Identifiers must not be derived from labels, folder names, array positions, dates or microscope identity. Renaming a sample, moving a package or changing a workspace index must not change its identity.

The authoritative ownership and reference rules are:

| Record | Authoritative relationship | Rule |
|---|---|---|
| Sample | SampleTypeId | Identifies collected source material; it does not own a persisted list of SlideIds |
| Slide | SampleId and SlideTypeId | References exactly one source Sample in the baseline model |
| Tile | SlideId | Belongs to exactly one Slide; its physical position is optional |
| AcquisitionPlan | SlideId | Describes intended acquisition for one Slide and is immutable after execution begins |
| AcquisitionRun | SlideId, AcquisitionPlanId, MicroscopeId | Records one actual run on one Slide, using one actual microscope |
| ImageFrame | AcquisitionRunId and TileId | One captured frame belongs to one run and one tile on the same Slide |
| DerivedImage | Source frame/derived-image IDs and processing provenance | Never replaces or mutates its source images |

An AcquisitionRun can produce frames for multiple tiles. A Tile can receive frames from multiple runs, which supports reacquisition of an existing field without changing the Tile identity. Every referenced Tile must belong to the Slide referenced by its AcquisitionRun. A frame cannot link a run from one slide to a tile from another.

A frame is an individual capture, not synonymous with a tile. Exposure brackets, Z planes, camera channels and repeat captures are represented as capture metadata and explicit relationships under the run/tile context; they must not be collapsed into a fabricated fixed-dimensional array.

The run obtains its Sample context through its Slide.SampleId. Do not duplicate SampleId on AcquisitionRun as an independently editable relationship. A denormalized sample identity snapshot is permitted only as immutable audit/export metadata and must be validated against the slide relationship when created.

The Slide.SampleId relationship is established when the Slide is created. Correcting a source association is an explicit audited domain operation, not a silent folder move or metadata rewrite. A Sample must not be physically deleted while Slides reference it. Archival/retention is distinct from destructive deletion; sample deletion must never cascade-delete slide packages or acquired image data.

The Sample record does not maintain an authoritative SlideIds collection. Queries such as “slides for sample” are resolved from Slide.SampleId. Any reverse index or workspace collection is a rebuildable projection, not a second authority.

## 12. Image documents, annotations and measurements

Live and saved-image viewers are separate front-end workflows.

**Live viewer:** camera-oriented, receives high-rate frames and supports temporary inspection/freeze/capture.

**Saved-image viewer:** document-oriented, opens stored images without a connected microscope, supports navigation, annotations, measurements and derived-image viewing.

The Core owns document and measurement semantics. Presentation owns drawing and interaction.

Annotations are stored as structured geometry in source-image coordinates:
- Point, line, polyline, rectangle, ellipse, polygon, freehand and text.
- Stable annotation ID and source image/frame identity.
- Semantic type/classification and style.
- Visibility and revision.
- Creation/update provenance.

Measurements preserve geometry, units, calibration revision, calculated result and uncertainty/provenance. Recalibration does not silently rewrite historical measurement meaning.

Rendering reconstructs original pixels plus the persisted annotation document. Original image pixels are never modified to bake in overlays.

DICOM serialization of annotations/measurements is handled through appropriate standards-based representations later; not every annotation belongs inside the WSI pixel object.

## 13. Persistence and DICOM

Core defines semantic persistence contracts for sample records, slide records, tiles, acquisition plans/runs, calibration references, image payloads and annotation documents. Infrastructure implements these contracts. The contracts express domain operations and consistency requirements; they do not expose filesystem paths or vendor storage APIs to Core.

### 13.1 Persistence ownership

- The Sample record is authoritative for sample identity, SampleTypeId and source metadata.
- The Slide record is authoritative for SlideId, SampleId, SlideTypeId and the versioned preparation snapshot.
- Tile metadata is owned by its Slide.
- Acquisition plans and runs are persisted in the context of their Slide. The run records the actual MicroscopeId and the plan it executed.
- ImageFrame metadata is associated with one AcquisitionRun and one Tile. The image payload itself is stored as an immutable object and referenced by a stable storage reference plus integrity metadata.
- Derived image payloads and provenance are separate from original frames. Reprocessing creates new derived identities rather than overwriting originals.
- Microscope calibration revisions remain in the microscope/configuration calibration store. Acquisitions reference the exact revisions used; slide folders do not become owners of calibration history.
- Workspace catalogs, recent-item lists and Sample-to-Slide reverse lookups are rebuildable indexes. They are not authoritative domain records.

The Sample and Slide packages are independently addressable. A Slide package retains its SampleId relationship even if moved to another workspace. If the Sample record is unavailable, the relationship is unresolved, not silently replaced with a new Sample. A slide can still be inspected as a package, but the application must report that its source Sample record is unavailable.

### 13.2 Initial local-first workspace layout

~~~text
Workspace/
├── samples/
│   └── {SampleId}/
│       └── sample.json
├── slides/
│   └── {SlideId}/
│       ├── slide.json
│       ├── configuration/
│       │   └── revisions/
│       ├── tiles/
│       │   └── {TileId}/
│       │       └── tile.json
│       ├── acquisition/
│       │   ├── plans/
│       │   ├── runs/
│       │   └── recovery/
│       ├── images/
│       │   ├── original/
│       │   └── derived/
│       ├── annotations/
│       ├── thumbnails/
│       └── history/
└── catalog/
    └── indexes (rebuildable)
~~~

This is a logical storage layout, not a Core API or a requirement that every adapter use directories. Braced ID directory names are conceptual storage keys, not permission to concatenate an opaque ID directly into a filesystem path. The storage adapter must deterministically encode IDs into safe portable path segments or maintain an explicit ID-to-storage-key mapping; it must prevent traversal, separator interpretation and collisions. The original ID remains in metadata and is not rewritten by path encoding. Image payload filenames/extensions and physical storage layout are also adapter decisions. Metadata records use stable IDs and relative payload references so a Slide package can be moved without changing domain identity.

A Sample directory does not contain authoritative copies of its Slides. Slide packages are separate so that a slide can be moved, backed up or restored independently; Slide.SampleId preserves its source relationship. The catalog index can be rebuilt by scanning authoritative records or by an adapter-specific index rebuild.

### 13.3 Persistence and recovery invariants

- Validate that SampleId exists before committing a new Slide.
- Validate that TileId belongs to the run's Slide before accepting a frame record.
- Persist an AcquisitionRun identity and recoverable initial state before dispatching hardware commands.
- Record a frame as available only after its immutable payload has been durably stored and its reference/integrity metadata has been committed.
- Preserve incomplete runs, missing frames and recovery checkpoints. Never infer success from the existence of a folder or plan.
- Make retries idempotent using stable run/frame identities; duplicate delivery must not create duplicate logical frames.
- Do not cascade-delete slide packages, frames or derived data when a Sample is archived or its metadata is corrected.
- Keep original image payloads immutable. Retention/deletion is an explicit policy operation with reference checks, not a side effect of editing Sample or Slide metadata.

A slide remains portable and can be reopened without a connected microscope. DICOM is an interoperability/storage adapter, not the internal domain model. The Core must not force unpositioned fields into physical WSI coordinates. DICOM WSI representation is selected only when its spatial and pixel semantics can be represented truthfully. Exposure brackets must not be misrepresented as Z planes. Different optical scales may require distinct suitable image objects/series. Interoperability constraints are resolved by the DICOM adapter and documented, not hidden in the internal model.

## 14. Runtime, concurrency and resource ownership

Each physical microscope has one active controlling runtime at a time.

- Multiple microscope runtimes may exist in one application process.
- A host-level resource registry prevents two runtimes from claiming the same exclusive physical device.
- Multiple front ends should share one application/service host if they need concurrent access to the same physical microscope.
- Core operation/event handling is serialized per runtime so state transitions have one logical owner.
- Adapter callbacks enqueue normalized events; they do not mutate Core state directly on device threads.
- Conflicting operations declare resource claims and are serialized or rejected.
- Independent operations may execute concurrently only when their dependencies and resource claims allow it.
- Existing camera acquisition workers, bounded queues, frame leases and latest-frame mailbox remain owned by the camera implementation and are not replaced by the Core operation event mechanism.
- Live image payload delivery uses a separate frame-stream contract, not the ordinary operation event channel.

## 15. Intended repository organization

This is the target layout to be reached incrementally. It is not a claim that all directories already exist.

~~~text
MICROSCOPE_CPP/
├── Libraries/
│   ├── Microscope.Core/
│   │   ├── include/
│   │   ├── src/
│   │   └── tests/
│   └── Microscope.Application/
│       ├── include/
│       ├── src/
│       └── tests/
│
├── Camera/
│   ├── Common/
│   └── Amscope/
│
├── Adapters/
│   ├── Stage/
│   ├── Illumination/
│   └── OpticalControl/
│
├── Infrastructure/
│   ├── JsonDefinitions/
│   ├── LocalStorage/
│   └── Dicom/
│
├── Definitions/
│   ├── Microscopes/
│   ├── Deployments/
│   └── Schemas/
│
├── Applications/
│   ├── OptikaB293.macOS/
│   ├── Microscope.CLI/
│   └── Microscope.Service/
│
├── Bridges/
│   └── Microscope.Application.ObjCXX/
│
└── docs/
~~~

Do not move the current native application wholesale or create empty speculative modules. Introduce each target when its first real contract and implementation are ready.

The C++ baseline remains C++17 for the initial migration, matching the current application C++/Objective-C++ target. Do not introduce a newer language baseline or a JSON dependency merely for convenience. JSON parsing remains isolated in Application/definition infrastructure; Core consumes typed values.

## 16. Existing implementation preservation and migration

The current branch contains:
- Application/OptikaB293 with the existing SwiftUI/AppKit presentation and Swift application state/coordinators.
- AmscopeNativeCameraBridge and NativeCameraController connecting the current application to the reusable AmScope camera driver.
- Libraries/Microscope.Core and Libraries/Microscope.Application as separate C++17 static-library targets with standalone CMake configuration.
- Initial Core strong-ID, Result/Status, diagnostic, operation-state and feedback metadata contracts.
- Initial Application runtime/operation handle and projection contracts.
- Focused Core/Application contract-test sources registered with CTest; they are not evidence of successful compilation or execution until validated.
- Current Swift microscope configuration and resolution policy code, which remains transitional rather than the final ownership boundary.
- The camera acquisition worker, frame pool, lease-based zero-copy presentation path, latest-frame mailbox and two-slide preview performance constraints, which remain implementation invariants.

The two C++ libraries are not yet the complete microscope engine or application runtime. No equipment/configuration model, full operation executor, JSON definition loader, adapter factory registry, storage implementation or device workflow migration is claimed here.

Migration rules:

1. Preserve the current camera acquisition and preview path.
2. Do not rewrite the camera driver to make it aware of Microscope.Application.
3. Move microscope-domain policy and behavior into Core in focused vertical slices.
4. Continue migrating composition/use-case ownership into the existing Microscope.Application C++ library.
5. Keep the current macOS target as one frontend. Do not make SwiftUI/AppKit dependencies part of either principal C++ library.
6. Replace transitional Swift microscope logic only after equivalent Core behavior and tests exist.
7. Keep the application usable during migration; do not create duplicate competing sources of truth.
8. Do not claim the target architecture is implemented until the relevant code has migrated and its acceptance criteria have been verified.

## 17. Implementation phases and acceptance

### Phase 0 — Architecture baseline
- Add this architecture specification and the implementation plan.
- Mark the earlier native-app architecture document as implementation-specific/historical where it conflicts with this platform target.
- Preserve current code and camera pipeline.

**Exit:** one authoritative platform architecture and a traceable migration plan.

### Phase 1 — C++ library foundations and public contracts
- Create independent CMake targets for Microscope.Core and Microscope.Application.
- Establish C++17 public API conventions, strong IDs, units, typed Result/Status and Diagnostic semantics.
- Define common feedback correlation, provenance, ingress and operation-handle contracts.
- Establish the capability-specific adapter-port convention; add real device command ports only with the capability that first consumes them.
- Define the minimal Application runtime lifecycle/API boundary without adding JSON loaders or the factory registry yet.
- Keep the first Core public API independent of Camera::Common, UI, OS APIs, vendor SDKs, JSON and Application.
- Confirm dependency direction with build-graph inspection.

**Exit:** both libraries build independently; Application depends on Core and not vice versa; initial public contracts compile and have focused type/validation/feedback-metadata tests. Full operation-state execution and simulated-device lifecycle tests remain Phase 4 acceptance.

### Phase 2 — Core equipment and optical model
- Define microscope, component identity, objectives, cameras, light paths, illumination, condenser, stage and axes.
- Model shared optical components and connected optical routes.
- Define equipment capability and constraint representation.

**Exit:** a representative microscope definition maps to a valid typed Core model without per-microscope behavioral subclasses.

### Phase 3 — Declarative configuration and transition planning
- Define profiles, contribution rules, conflict detection and dependency relationships.
- Implement pure configuration resolver and validator.
- Generate an ordered dependency plan from current and desired configurations.

**Exit:** objective/contrast/light-path/camera combinations resolve from data; invalid or unavailable combinations are diagnosed; no configuration behavior resides in objective/filter classes or UI.

### Phase 4 — Core operation runtime and feedback
- Define operation, step and dependency-plan contracts.
- Add the first real capability-specific typed command ports and normalized typed feedback payloads.
- Implement correlation, event ordering, serialized state reduction, timeouts, cancellation and partial-failure handling.
- Execute through simulated implementations of the real capability ports first.
- Verify that adapter acceptance, device acknowledgement, hardware action completion and Core verification remain separate.

**Exit:** tests cover accepted/rejected commands, delayed feedback, duplicate/stale/late feedback, missing readback, manual confirmation, failure, timeout, cancellation and dependent-step ordering.

### Phase 5 — JSON definitions, deployments and Application composition
- Define/version microscope and deployment JSON schemas.
- Implement Application-owned definition catalog/loader and typed conversion.
- Implement explicit adapter factory registration and resource claiming for the already-defined capability ports.
- Create/open/close multiple microscope runtimes.

**Exit:** valid JSON creates a Core runtime; invalid schema, invalid equipment topology and unavailable adapters produce distinct diagnostics.

### Phase 6 — Calibration
- Implement nominal optical estimation and measured ruler calibration.
- Define optical calibration keys and reuse/invalidation.
- Implement linear/rotary axis calibration and evidence quality.
- Store immutable revisions via calibration persistence ports.

**Exit:** matching optical configurations reuse calibration revisions; changed geometry-affecting configurations do not silently reuse incompatible values; no per-slide stage registration.

### Phase 7 — Slide lifecycle and persistence
- Define slide types/configuration snapshots and slide identity.
- Implement local slide-folder lifecycle through storage ports.
- Add Core active-slide-session enforcement.
- Define tile and image-field identity and optional position evidence.

**Exit:** acquisition is rejected without an active slide; a slide can reopen without hardware; unpositioned fields remain valid.

### Phase 8 — Acquisition workflow and camera adapter integration
- Define plans, runs, frames, exposure brackets and Z planes.
- Connect Core acquisition operations to the existing camera pipeline through an adapter.
- Preserve bounded frame delivery and lease ownership.
- Add recovery and partial-run semantics.

**Exit:** plan and actual run differ correctly; incomplete captures are recoverable; the existing camera preview path has no additional queues or pixel copies.

### Phase 9 — Image documents, annotations and processing provenance
- Implement source-image coordinates and persistent annotation documents.
- Implement calibrated measurement records.
- Add derived-image source/provenance model for HDR/focus-stack results.

**Exit:** saved documents reconstruct source pixels plus annotation overlays; measurements preserve calibration provenance; source pixels remain immutable.

### Phase 10 — Frontend migration and additional hosts
- Implement the Objective-C++ bridge to Microscope.Application.
- Move microscope configuration/workflow ownership from transitional Swift models into Core/Application.
- Keep SwiftUI/AppKit as presentation.
- Add CLI and/or service host only when there is a concrete consumer.

**Exit:** macOS uses the same C++ Core/Application behavior intended for other front ends; the presentation contains no microscope policy.

## 18. Acceptance rules

A phase is not complete because files or folders have been created. It is complete when:

- Ownership is correct.
- The dependency direction is respected.
- The public contract is usable by a real consumer.
- Pure behavior has deterministic tests.
- Asynchronous behavior has event/feedback lifecycle tests.
- Failure and recovery paths are represented.
- The current camera pipeline and user-visible behavior are preserved where not intentionally changed.
- No duplicate source of truth has been introduced.

Build, unit tests, integration tests, visual checks and hardware acceptance are separate evidence categories. Source inspection or documentation does not imply they passed.

## 19. Glossary

- **MicroscopeDefinition:** typed description of the physical equipment and its relationships.
- **MicroscopeDeployment:** host-specific binding from logical equipment endpoints to adapter implementations.
- **MicroscopeRuntime:** Core-owned running instance of one microscope.
- **Adapter:** infrastructure implementation that translates Core ports into physical device/storage operations and normalizes their results.
- **Operation:** Core-owned asynchronous domain workflow with steps, dependencies, feedback and terminal outcome.
- **CommandDispatched:** Core fact that a port invocation was made.
- **DeviceActionCompleted:** adapter-reported completion of one hardware action; not proof that the overall operation succeeded.
- **DeviceStateObserved:** adapter-reported state evidence with source and timestamp.
- **StepVerified:** Core conclusion that a step's required completion predicate is satisfied.
- **EffectiveConfiguration:** configuration established by observations at the assurance level supported by the instrument.
- **OpticalCalibration:** mapping from delivered image coordinates to specimen-space dimensions for a geometry-affecting optical configuration.
- **MotionCalibration:** mapping from an axis command/feedback coordinate to physical axis displacement.
- **Sample:** collected source specimen/material, independent of microscope and slide preparation.
- **SampleType:** taxonomy for source material/category.
- **Slide:** physical preparation made from one sample in the baseline model.
- **SlideType:** taxonomy for preparation/mounting method or category.
- **Tile:** persistent slide-owned image-field identity; physical position is optional and multiple acquisition runs may contribute frames to one tile.
- **AcquisitionPlan:** immutable intended acquisition specification associated with one slide.
- **AcquisitionRun:** actual execution record for one slide and one microscope.
- **ImageFrame:** immutable original capture associated with one run and one tile.
- **SlideSession:** Core-recognized active slide context required for acquisition.
- **Frontend:** a client of Microscope.Application; it is not an owner of microscope behavior.
