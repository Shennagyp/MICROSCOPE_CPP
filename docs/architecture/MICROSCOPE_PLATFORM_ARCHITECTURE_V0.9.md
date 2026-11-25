# Microscope Platform Architecture — v0.9

**Status:** Authoritative target architecture for OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Date:** 2026-11-25  
**Scope:** Microscope domain model, C++ libraries, adapter integration, operation lifecycle, calibration, slides, persistence and frontend independence.

This document supersedes earlier application-level architecture descriptions wherever they conflict with the platform boundaries defined here. The existing native application and camera pipeline remain the implementation baseline during migration; this document does not claim that the target C++ libraries already exist.

## 1. Architectural decisions

The platform consists of two principal C++ libraries, infrastructure implementations and independent front ends.

1. **Microscope.Core** is the microscope engine. It owns equipment semantics, microscope behavior, configuration, operation planning and execution state, feedback evaluation, calibration, motion, acquisition, slide/image semantics and domain recovery.
2. **Microscope.Application** is a separate C++ application library. It loads microscope JSON definitions and deployment data, registers and composes adapters, creates and manages Core runtimes, coordinates application-level workflows and exposes a frontend-neutral API.
3. Hardware and storage adapters implement contracts declared at the appropriate Core boundary. They perform external I/O and report observations; they do not own microscope policy.
4. Front ends consume Microscope.Application. They do not implement microscope behavior or call device SDKs.
5. Each physical microscope is described by JSON data. Runtime state, calibration records and slide data are persisted separately.
6. Microscope operations are asynchronous, ordered, correlated and feedback-driven. A request being accepted is not equivalent to a physical action completing or a microscope configuration being verified.
7. Optical and motion calibration are independent. Optical calibration starts from equipment specifications and can be refined with a microscope ruler slide. It is reused for matching optical configurations, not repeated for every slide.
8. Slide records and tiles are persistent domain data. An acquisition cannot start without an active, valid slide session.
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

A light path is an explicit physical optical route through connected components. Its representation must preserve component identity, connection points and optical order.

- A component shared by multiple paths has one identity and may participate in multiple paths.
- A beam splitter or selector may create alternative paths.
- Cameras attach to appropriate output ports/paths, not merely to an objective.
- The topology describes equipment; it is not a visual node editor.
- Configuration profiles select valid routes and component states.

The model must not assume that all microscopes have the same optical ordering or component inventory.

### 5.3 Objectives and magnification

Objective records include the specifications needed for compatibility and optical calculations, such as nominal magnification, numerical aperture, working distance and optical correction characteristics.

Nominal effective magnification is derived from the relevant optical train, including objective and magnification-affecting relay/tube optics. It is not assumed to be simply the objective marking in every optical design.

The nominal camera object-space pixel size can be estimated from sensor pitch and effective magnification:

    S_nominal = P_sensor / M_effective

The result must account for the actual output-pixel mapping. Sensor pitch alone is not enough when binning, ROI, subsampling, demosaicing, cropping or resampling changes the relation between sensor pixels and delivered image pixels.

Nominal values are estimates until refined by a suitable reference calibration.

### 5.4 Cameras

Camera equipment data describes:

- Stable camera identity and sensor specification.
- Sensor pixel pitch and active dimensions.
- Supported output modes and pixel formats.
- Supported resolution, ROI, binning and readout behavior.
- Capabilities such as exposure, gain and other controls.
- The optical output/light path to which the camera is connected.

The adapter supplies actual device capabilities and observations. The Core validates them against the definition and requested configuration.

### 5.5 Illumination and condenser

Illumination sources, control ranges and condenser specifications are modeled independently. Configuration profiles define compatible illumination and condenser arrangements for objectives and contrast methods.

The model must not assume that illumination settings are a property of an objective class. They are configuration relationships between the objective, optical path, contrast method, condenser and illumination system.

### 5.6 Stage and axes

A stage contains a collection of axes. An axis has a motion type and unit:

- Linear: e.g. X, Y or Z displacement.
- Rotary: e.g. theta or another angular degree of freedom.

XYZ are common axis identifiers, not a hard limit on the model.

Axis specifications focus on motion capability and measured performance, not mechanical construction details. Relevant properties include:

- Motion type and coordinate unit.
- Operating range and limits.
- Nominal command/readout resolution.
- Accuracy.
- Precision/repeatability, with test conditions.
- Position feedback capability and its measurement location/provenance.
- Measurement uncertainty.
- Calibration identity and validity.

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

Every adapter feedback item must carry enough information to associate it with the pending work:

- Microscope/runtime identity.
- Operation ID.
- Step ID.
- Command ID, where the feedback relates to a dispatched command.
- Logical device ID.
- Monotonic sequence/generation information for ordering and stale-event rejection.
- Observation or event kind.
- Source/provenance.
- Timestamp of receipt; device timestamp when available.
- Typed payload or normalized diagnostic.

The Core uses these identifiers to reject duplicate, stale, unrelated or late feedback. A callback from a previous operation must not update the current operation merely because it concerns the same device.

### 7.4 Observation provenance

A state observation includes how it was obtained. Examples:

- Hardware readback/encoder.
- SDK status.
- Device event.
- Operator confirmation.
- Calibration-based estimate.
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

## 11. Slides, tiles and acquisition

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
- User-facing label or accession/reference where applicable.
- SampleTypeId.
- Collection/source metadata and timestamps where available.
- Optional descriptive metadata required by the workflow.

Sample identity and source metadata must remain independent of slide preparation, microscope identity and acquisition history. A sample can exist before any slide is prepared and may have multiple prepared slides.

### 11.3 Slide and slide type

A Slide has:
- Stable SlideId.
- SampleId identifying its source sample.
- User-facing label.
- SlideTypeId.
- Versioned preparation/configuration snapshot.
- Local slide-folder identity/location.
- Acquisition history and tile references.

Slide types are reusable preparation templates such as blood film, bone-marrow smear/aspirate preparation, kidney tissue section, liver tissue section or custom preparation types. They describe how material is prepared on the slide, not the source material taxonomy.

A slide receives a preparation/configuration snapshot when created. Updating a reusable slide type must not silently change existing slides.

### 11.4 Tile semantics and positions

A Tile is the persistent identity of an acquired field/image region within a slide. It can own references to original frames, derived images and acquisition records.

A tile may have physical position metadata when supported by the microscope. Position is optional. The system must support unpositioned fields and must not fabricate physical XYZ coordinates.

A tile's sequence/index or virtual navigation order is not a physical slide coordinate.

The architecture does not require whole-slide scanning or stitching. It supports individual fields of view such as blood-film and bone-marrow cell acquisition workflows.

### 11.5 Mandatory active slide session

No acquisition may start without a valid active slide session.

This condition is enforced by Core when handling acquisition requests, not merely by disabling a button in a frontend.

Application coordinates folder selection/opening and persistence. Core owns the active-session precondition and rejects acquisition without it.

Switching or closing a slide during acquisition requires a safe stop/completion and recovery path.

### 11.6 Acquisition plan versus run

AcquisitionPlan is the intended sequence and settings. It is immutable once execution starts.

AcquisitionRun is the actual execution record, including:
- Effective microscope configuration.
- Calibration revisions used.
- Device observations and actual camera values.
- Frame identities and timestamps.
- Stage/focus observations with provenance.
- Completed, missing and failed steps.
- Errors, interruptions and recovery status.

A plan must never be recorded as if it were the actual outcome.

### 11.7 Frames, Z and exposure

The data model must support irregular acquisition sets, not assume a dense fixed tensor.

Each original ImageFrame has identity, pixel-data reference, source geometry, capture metadata, configuration snapshot and relevant calibration snapshot.

Z planes and exposure brackets are explicit relationships. HDR combines exposures for the same field and focal plane; it must not merge different fields or different Z planes. Focus stacking is a separate operation across focal planes. Original frames remain immutable.

Incomplete runs are recoverable and marked incomplete.

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

Core defines semantic persistence ports for microscope calibration, slide records, acquisition plans/runs, image data and annotation documents. Infrastructure implements these contracts.

Initial local-first slide folder concept:

~~~text
SlideFolder/
├── slide.json
├── configuration/
├── acquisition/
│   ├── plans/
│   ├── runs/
│   └── recovery/
├── images/
│   ├── original/
│   └── derived/
├── annotations/
├── thumbnails/
└── history/
~~~

The folder should remain portable. Stable IDs and relative references must survive moving the folder. A slide can be reopened without a connected microscope.

DICOM is an interoperability/storage adapter, not the internal domain model. The Core must not force unpositioned fields into physical WSI coordinates. DICOM WSI representation is selected only when its spatial and pixel semantics can be represented truthfully. Exposure brackets must not be misrepresented as Z planes. Different optical scales may require distinct suitable image objects/series. Interoperability constraints are resolved by the DICOM adapter and documented, not hidden in the internal model.

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

The current branch contains a working native macOS application structure:
- Application/OptikaB293 currently contains SwiftUI/AppKit presentation and Swift application state/coordinators.
- AmscopeNativeCameraBridge and NativeCameraController connect the current application to the reusable AmScope camera driver.
- Current Swift microscope configuration and resolution policy code is transitional implementation, not the final ownership boundary.
- The camera acquisition worker, frame pool, lease-based zero-copy presentation path, latest-frame mailbox and two-slide preview performance constraints are existing implementation invariants.

Migration rules:

1. Preserve the current camera acquisition and preview path.
2. Do not rewrite the camera driver to make it aware of Microscope.Application.
3. Move microscope-domain policy and behavior into Core in focused vertical slices.
4. Introduce Microscope.Application as a C++ library and migrate composition/use-case ownership there.
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

### Phase 1 — C++ library foundations
- Create independent CMake targets for Microscope.Core and Microscope.Application.
- Establish C++17 public API conventions, strong IDs, units, diagnostics and Result semantics.
- Establish Core port/event contracts and Application API boundary.
- Confirm dependency direction with build-graph inspection.

**Exit:** both libraries build independently; Application depends on Core; Core has no frontend, vendor SDK or JSON dependency.

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
- Define operation, step, command, feedback and observation contracts.
- Implement correlation, event ordering, serialized state reduction, timeouts, cancellation and partial-failure handling.
- Execute through simulated adapter ports first.
- Verify that device completion and Core verification remain separate.

**Exit:** tests cover accepted/rejected commands, delayed feedback, duplicate/stale/late feedback, missing readback, manual confirmation, failure, timeout, cancellation and dependent-step ordering.

### Phase 5 — JSON definitions, deployments and Application composition
- Define/version microscope and deployment JSON schemas.
- Implement Application-owned definition catalog/loader and typed conversion.
- Implement explicit adapter factory registration and resource claiming.
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
- **Slide:** physical preparation made from one sample in the baseline model.
- **Tile:** persistent slide-owned image-field identity; physical position is optional.
- **SlideSession:** Core-recognized active slide context required for acquisition.
- **Frontend:** a client of Microscope.Application; it is not an owner of microscope behavior.
