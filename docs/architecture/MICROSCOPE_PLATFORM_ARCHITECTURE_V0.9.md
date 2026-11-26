# Microscope Platform Architecture — v0.9

**Status:** Authoritative target architecture for OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Date:** 2026-11-25  
**Scope:** Microscope equipment/control, optical configuration, operation lifecycle, calibration, stage positioning, acquisition execution, physical slide records, SampleType imaging/preparation requirements, persistence and frontend independence. Patient and laboratory information management are outside scope.

This document is authoritative for microscope-domain semantics and the C++ Core/Application boundary. The product-level long-lived object graph, stateful OOP ownership, native application composition, subsystem lifecycles, concurrency and complete application implementation order are defined in [OPTIKA B293 Application System Architecture v1](OPTIKA_B293_APPLICATION_SYSTEM_ARCHITECTURE_V1.md). The initial C++17 Microscope.Core and Microscope.Application static-library targets and foundational contracts now exist in the repository. They are an incremental foundation: equipment/configuration workflows, full operation execution, JSON loading, adapter composition, persistence and physical hardware integration remain incomplete. The existing native application and camera pipeline remain the implementation baseline during migration.

## 1. Architectural decisions

The platform consists of two principal C++ libraries, infrastructure implementations and independent front ends.

1. **Microscope.Core** is the instrument engine. It owns installed-equipment semantics, optical topology, operation modes, configuration resolution, operation planning/execution, feedback evaluation, calibration, stage motion/positioning, SampleType technical requirements, physical slide details, acquisition and image-capture semantics. It does not own patient, source-sample or laboratory workflow records.
2. **Microscope.Application** is a separate C++ application library. It loads microscope JSON definitions and deployment data, registers and composes adapters, creates and manages Core runtimes, coordinates application-level workflows and exposes a frontend-neutral API.
3. Hardware and storage adapters implement contracts declared at the appropriate Core boundary. They perform external I/O and report observations; they do not own microscope policy.
4. Front ends consume Microscope.Application. They do not implement microscope behavior or call device SDKs.
5. Each physical microscope is described by equipment/deployment data. Runtime state, calibration, SampleType definitions, physical slide details, stage registrations and acquisition/image records are persisted separately.
6. Microscope operations are asynchronous, ordered, correlated and feedback-driven. A request being accepted is not equivalent to a physical action completing or a microscope configuration being verified.
7. Optical and motion calibration are independent. Optical calibration starts from equipment specifications and can be refined with a microscope ruler slide. It is reused for matching optical configurations, not repeated for every slide.
8. SampleType is a reusable, versioned technical requirement profile. A Slide is an independent physical-slide record that directly references one SampleType revision and stores its own actual details/preparation. There is no Sample instance or SampleId between them. A slide-targeted acquisition may reference a Slide; calibration or direct imaging need not invent one.
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
- A slide's historical SampleType revision, actual preparation details, stage registration, acquisition configuration and calibration evidence remain interpretable after later changes.
- The Core/Application model must not define Patient, Sample, SampleId, Specimen-as-a-parent-entity, SlideType, diagnosis or clinical accession records. An optional OriginLabId is an opaque external reference only; it does not create a Lab entity or foreign-key relationship.
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

The installed imaging path may include tube lenses, intermediate relay optics and camera/reduction adapters. These are component-scoped OpticalRelaySpecification values with typed roles and optional positive factors. OpticalMagnificationResolver composes the objective's nominal magnification with factors on the selected Imaging path only; unknown factors stay unknown, and display zoom/software resizing is excluded.

## 5. Equipment model

### 5.1 Microscope composition

A microscope definition describes the installed instrument:

~~~text
MicroscopeEquipmentDefinition
├── Identity
├── EquipmentComponents
│   ├── Optical elements
│   │   ├── Objectives and relay optics
│   │   ├── Condenser and condenser elements
│   │   ├── Filters, phase, polarization and DIC elements
│   │   └── Beam splitters and selectors
│   ├── Imaging devices
│   │   └── Camera component + CameraSpecification
│   ├── Illumination devices
│   │   └── Source component + IlluminationSpecification
│   └── Mechanical assemblies
│       └── Stage and axes
├── OpticalConnections
│   └── Physical component-port adjacency
├── OpticalPaths
│   └── Ordered directed routes through component ports
└── LightPaths
    └── Named arrangements grouping compatible OpticalPaths
~~~

These are logical model boundaries, not separate libraries or mandatory class hierarchies.

### 5.2 Optical topology

The topology has two distinct levels. An **OpticalPath** is one ordered, directed route through installed components. A **LightPath** is the selectable microscope arrangement that groups the route or routes used together for an observation/acquisition. This distinction matters when illumination and imaging travel through different routes, share components, or branch at a beam splitter. A LightPath is not a second physical copy of its routes or devices.

- A physical OpticalConnection is an undirected adjacency between two ports on different components. Endpoint storage order does not encode propagation direction.
- Ordered OpticalPathStep values determine propagation direction through each component. Entry/exit port capabilities are validated for that direction.
- One physical connection can be reused by different paths traversing the same adjacency in opposite directions, provided the involved ports support each traversal.
- A component shared by multiple paths has one EquipmentComponentId and may participate in multiple paths.
- A beam splitter or selector may create alternative paths using distinct physical ports for the branches.
- Cameras attach to appropriate output ports/paths, not merely to an objective.
- OpticalElement components may declare typed installed functions such as phase plate, phase annulus, polarizer, analyzer, DIC prism, excitation/emission filter, dichroic mirror or darkfield stop.
- A function capability may carry an OpticalCompatibilityId so physically mating elements can be matched without embedding behavior in their classes. Duplicate function/compatibility pairs on one component are rejected.
- A ConfigurationProfile's OpticalFunctionRequirement may constrain the OpticalPathRole on which an installed function must occur. For example, a phase annulus is required on a selected Illumination route while its compatibility-matched phase plate is required on a selected Imaging route. A component elsewhere in the microscope or on the wrong selected route cannot satisfy that requirement.
- The topology describes equipment; it is not a visual node editor, executable behavior graph or configuration resolver.
- Configuration profiles select valid routes and component states in Phase 3.

The model must not assume that all microscopes have the same optical ordering or component inventory.

### 5.2.1 LightPath ownership and camera endpoints

The C++ object model must make the camera-to-path relationship structural, not a UI convention or a loose compatibility list.

- An imaging camera is an installed `EquipmentComponent` whose kind is `ImagingDevice`. It owns optical input port(s). Its sensor/readout capability is described by exactly one `CameraSpecification` keyed by that same `EquipmentComponentId`; the specification is not a second camera identity.
- An imaging `OpticalPath` has an explicit route role and ends at the camera component. The final `OpticalPathStep` names that camera component and its input port. The preceding route step's output port is joined to that input by an `OpticalConnection`. The equipment-definition validator verifies component ownership, port direction, and physical continuity.
- A selectable `LightPath` references one or more route IDs. Its camera endpoint(s) are derived by inspecting its imaging routes; it must not maintain a separately editable camera list that can drift out of sync with the topology.
- For an ordinary single-camera acquisition, the chosen LightPath resolves to exactly one connected camera. The camera's supported `CameraModeId` values are then used to resolve its output mode. A camera that is installed, enumerated by a driver, or present on another LightPath is not a candidate.
- A LightPath may expose multiple imaging endpoints only when the equipment definition explicitly describes the branches (for example, a physical image splitter/beam splitter and two connected camera routes). This is a multi-camera arrangement, not an arbitrary choice among cameras. The acquisition request must state whether it intends one endpoint or a supported multi-camera capture; simultaneous operation additionally requires the relevant trigger/synchronization capabilities.
- Illumination and imaging routes may be distinct and may share installed components. Phase contrast can therefore require a phase annulus on the condenser/illumination route and a matched phase plate on the imaging route. Technique requirements are evaluated over the routes grouped by the selected LightPath, not over every installed component in the microscope.
- A LightPath is scoped to one microscope equipment-constitution revision. Its route references must resolve within that same definition.

The C++ topology now encodes these ownership relationships:

~~~text
MicroscopeEquipmentDefinition
├── EquipmentComponents
│   ├── EquipmentComponent(camera-1, ImagingDevice)
│   │   └── OpticalPort(camera-1.input)
│   ├── EquipmentComponent(objective-1, OpticalElement)
│   └── EquipmentComponent(beam-splitter-1, OpticalElement + BeamSplitter)
├── CameraSpecifications
│   └── CameraSpecification(componentId = camera-1)
├── OpticalConnections
│   └── objective-1.output ↔ camera-1.input
├── OpticalPaths
│   └── OpticalPath(role = Imaging)
│       └── ... → OpticalPathStep(camera-1, entry = camera-1.input)
└── LightPaths
    └── LightPath("Brightfield")
        └── OpticalPathId(imaging-route-1)
~~~

The camera is not a child object stored separately inside `LightPath`. The route's final `OpticalPathStep` references the same `EquipmentComponentId` as its `ImagingDevice` and `CameraSpecification`; its entry port is physically joined to the preceding route component by an `OpticalConnection`. `MicroscopeEquipmentDefinition::cameraEndpointIds(lightPathId)` derives the ordered, unique camera endpoint IDs by traversing the LightPath's Imaging routes.

The Core source now includes `OpticalPathRole` (`Imaging`, `Illumination`, `Other`), the `LightPath` aggregate, route-reference validation, imaging-route terminal-camera and CameraSpecification validation, and multi-camera LightPath validation requiring imaging routes to share a declared `BeamSplitter` input and use distinct outputs. The topology tests cover connected camera derivation, exclusion of an unrelated installed camera, an imaging route with a non-camera terminal, LightPaths without imaging routes, and multi-camera branch validation. Those tests have been authored but were not run in this update.

The camera relationship is already implemented through the LightPath topology and a partial configuration bridge. `LightPathCameraResolver` derives eligible camera endpoints and output modes from the selected LightPath; `LightPathConfigurationRequestFactory` derives the route set and binds the chosen endpoint to the exact OperationMode and equipment revisions; `ConfigurationResolver` revalidates that binding, route set and camera endpoint. The remaining gap is the canonical public request and whole-configuration candidate resolver: `ConfigurationRequest` still carries mandatory explicit camera/path and objective/condenser/illumination identities, and the general resolver still validates those supplied component choices rather than discovering complete valid configurations. This is a migration/integration gap, not a missing camera-to-LightPath relationship.

The model deliberately avoids a direct `Objective.Camera` relationship. Objectives and cameras are connected through the actual optical route and its intermediate optical components. This supports multiple objectives, relay optics, splitters, multiple camera ports and microscope-specific topologies without assuming a universal arrangement.

### 5.3 Objectives and magnification

ObjectiveSpecification records attach to the installed physical EquipmentComponentId; they do not create a second identity or per-objective behavior class. They include nominal magnification, numerical aperture, working distance, chromatic/field correction, immersion medium and optional design coverglass thickness.

The physical model must derive specimen-to-detector magnification from the objective and magnification-affecting optics on the selected imaging route. The current C++ `ObjectiveSpecification` stores the objective's nominal magnification, but the equipment model does not yet expose typed magnification factors for tube lenses, system optics or camera relays. Consequently, total effective magnification is not yet computable from current Core data; missing factors must remain unknown rather than being assumed to be 1×. Confocal systems also need factor location: optics before a spinning disk can change the specimen-side pinhole projection, while a camera relay after the disk changes detector sampling but not the back-projected pinhole.

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
- Its physical optical input port(s), represented by the parent `EquipmentComponent` and connected through `OpticalConnection` values.
- The imaging route(s) and selectable `LightPath` that terminate at this camera component. This membership is derived from the topology, not stored as an independent camera-to-path compatibility list.

Do not assume sensor pitch alone maps directly to delivered pixels or that horizontal and vertical mapping ratios are equal. A camera is eligible for a requested acquisition only when it is the terminal imaging device of a route included in the selected LightPath.

The Core `LightPathCameraResolver` consumes an OperationMode and selected LightPath. It derives candidate cameras exclusively from Imaging route terminal devices and only accepts manual camera selection among those endpoints. It also resolves the connected camera's declared output mode. A manually selected endpoint with poor or unknown suitability remains selected with warning diagnostics; a camera outside the selected LightPath is unsupported. If several cameras are connected, Core may recommend a unique task-suitable candidate, using global shutter only after task-specific sensor metrics; an equal ranking requires user selection.

This resolver validates the microscope's declared equipment constitution. It does not establish that a physical camera session is currently connected, open or operable. Application must verify the live device capability/session before acquisition dispatch. The selected camera and camera-mode identity plus all suitability warnings must be retained in the resolved configuration and acquisition snapshot.

The adapter supplies actual device capabilities and observations. The Core validates them against the definition and requested configuration.

#### Camera model catalog and fluorescence suitability

The repository's `Camera/Common` project owns the backend-neutral discovery contract and the reusable camera model catalog:

- `CameraDeviceInfo` describes a currently enumerated device and the facts available from its backend.
- `CameraModelProfile` describes a reusable manufacturer/model/variant, including stable model-level capabilities and sensor-characteristic data.
- `CameraModelCatalog` indexes those profiles by normalized manufacturer, model and optional hardware variant. It does not use serial number or a transient SDK enumeration ID as the model key.
- A backend enriches discovery only with facts it can establish. Application merges a matching persisted model profile into the live device information and translates the resulting facts into the Core `CameraSpecification`.

Keep dynamic feature descriptors/readbacks separate from static model characteristics. A feature advertised by a connected camera is not necessarily a scientific sensor specification. The AmScope vendor descriptor currently exposes facts including global-shutter, monochrome and sensor-technology flags. It does not establish QE curves, read noise, dark current or full-well capacity. Those values must come from a manufacturer datasheet, verified profile or measurement and retain their provenance. Unknown data must remain unknown; it must not be inferred from preview dimensions, model family guesses or missing feature flags.

Fluorescence suitability is an advisory assessment, not a camera-identity validator. The normal hard execution constraints remain: the selected camera must be the terminal ImagingDevice of the selected LightPath imaging route, must have a declared supported output mode, and must be operable by its current adapter/session. Once those conditions hold, a manually assigned camera remains selectable even when its fluorescence profile is less suitable. Return warnings and the reasons; do not silently replace the manual selection or reject it solely because a preference was missed.

The assessor evaluates sensor data against the detected emission band. Core now models a fluorophore's relative spectral photon-flux density and bounded filter-curve interpolation. When the emission spectrum, selected dichroic and emission-filter curves are supplied, camera QE is weighted by emission × dichroic transmission × emission-filter transmission and integrated over the union of all curve sample knots. The curves are treated as piecewise linear; three-point Gauss–Legendre quadrature integrates their degree-four product on each interval. Every input must cover the entire requested band; no extrapolation is allowed. The resulting weighted QE is a detector-side spectral estimate, not a complete microscope signal prediction: excitation efficiency, collection-path throughput, exposure, fluorophore quantum yield/bleaching, sensor readout mapping and noise still contribute to actual performance. When spectral inputs are absent, the unweighted band mean remains an explicitly warned coarse fallback, not evidence of a complete estimate.

Candidate ranking is separate from manual selection and from hard route validation. Global shutter is a recommendation preference, not an unconditional fluorescence requirement. Recommend a global-shutter camera when two candidates are otherwise equivalently suitable, or when the declared acquisition requirement makes temporal simultaneity/motion distortion decisive. Do not demote a rolling-shutter scientific camera merely because its shutter type is rolling if its sensitivity/noise performance is materially better for the acquisition. Unknown shutter type is not evidence of rolling shutter. A user selection that misses a suitability preference produces a warning that can be acknowledged/overridden; the selected camera identity is retained in the configuration/acquisition snapshot.

### 5.5 Illumination and condenser

IlluminationSpecification attaches to an installed IlluminationDevice component and contains one or more independently identified channels. A channel may declare a spectral band and intensity-control kind; continuous control requires a finite range with explicit units. One source can therefore expose multiple channels with different spectra and control semantics.

CondenserSpecification attaches to an installed OpticalElement component and declares design, immersion medium, aperture NA range and optional working distance. The minimum aperture NA may be zero; it must not exceed the positive maximum.

Illumination and condenser data remain independent of objective classes. Configuration profiles define compatibility and selection relationships between objective, optical path, contrast method, condenser and illumination system in Phase 3.

#### Source spectra, filter sets and illumination state

An illumination source profile must identify its technology (for example LED, halogen/tungsten, xenon arc, mercury arc, metal-halide arc or laser), independently controlled channels, device control semantics and, where available, a wavelength-dependent spectral power distribution. A channel's relative spectral power curve describes spectral shape; it is not an absolute specimen irradiance measurement.

Filters are installed optical components with role-specific spectral characteristics:

- Excitation filter: transmission spectrum on the illumination/excitation route.
- Dichroic: both reflection and transmission spectra, associated with the relevant illumination and detection directions.
- Emission filter: transmission spectrum on the imaging/detection route.
- Neutral-density filter: wavelength-dependent attenuation used to reduce delivered light without changing the illumination source identity.
- Other optical filters: modeled by their actual role and measured/manufacturer response rather than a generic on/off label.

A filter's role in its `OpticalFilterSpecification` must agree with the installed component's optical-function capability. Compatibility identity links components that belong to the same designed filter cube/set. The filter must also occur on the relevant route in the selected LightPath; an installed filter elsewhere in the microscope cannot satisfy a mode requirement.

For fluorescence, evaluate the excitation side and detection side separately. Excitation power at the specimen depends on the source spectrum, excitation-filter transmission, dichroic reflection in the excitation direction and the selected optical route. Detected signal depends on the fluorophore emission spectrum, collection optics, dichroic transmission, emission-filter transmission and camera QE. Do not infer fluorescence camera suitability from the lamp's nominal color or from a single center wavelength.

A mode-specific illumination target must describe a calibrated physical quantity at a declared measurement plane, not an unqualified LED percentage. The default reference for specimen illumination is irradiance at the specimen plane (for example µW/mm²); the stored calibration also records the source's hardware drive unit/value. For fluorescence workflows, include exposure duration and, when required by the task, cumulative light dose/photobleaching limits. For transmitted brightfield and darkfield, the calibration must reflect the selected objective/condenser/stop/filter geometry and aperture state.

The intensity of a 4× brightfield mode, a 40× brightfield mode and a 4× darkfield mode is not a universal magnification-derived constant. The user can set a different target for each operation mode. A 4× brightfield mode may intentionally use a lower specimen-plane irradiance than the 40× and 4× darkfield modes. Persist those independently calibrated targets; do not assume equal drive percentages produce equal specimen illumination across different routes or optical configurations.

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

The target model separates reusable TechniqueDefinition values from editable OperationMode values. A technique describes one optical function/constraint contribution; an operation mode composes one or more techniques with objective requirements and desired operating settings. The current ConfigurationProfile is an earlier implementation baseline to evolve toward this model. These definitions are declarative data, not executable behavior attached to objective/filter classes.

A profile can specify:

- Applicability/selection conditions and allowed installed component identities.
- Required optical functions on selected components, including optional compatibility identities and minimum distinct-component counts.
- Required components and component states.
- One or more required light paths.
- Illumination and condenser settings.
- Camera presets and exposure policies.
- Dependencies, exclusions and compatibility constraints.
- Whether a value is mandatory, recommended or optional.

Examples of declarative function requirements:
- Phase contrast: a phase plate in the objective rear focal plane and a condenser annulus in the condenser front focal plane must be conjugate, geometrically matched for the objective's pupil/NA, and correctly centered. A shared `OpticalCompatibilityId` is useful evidence but is not sufficient by itself.
- DIC: the selected optical train must contain the compatible polarizer/analyzer and the required Wollaston/Nomarski prism arrangement, with the relevant prism positions, shear/orientation and strain-free optical constraints represented where the instrument requires them.
- Fluorescence: a profile can require excitation filter, emission filter and dichroic mirror functions with one shared compatibility identity.

A request may name exact installed components when the user wants manual control, but those selections are optional constraints rather than mandatory inputs. When identities are omitted, the resolver discovers candidates from the installed equipment and the optical routes. Technique requirements are evaluated over the components actually traversed by the candidate route set. A candidate phase arrangement, for example, must include a compatible phase plate and phase annulus in their required optical positions; DIC must include its compatible polarizer, analyzer and required prisms. A component being installed somewhere in the microscope is not sufficient: it must belong to the candidate route that realizes the requested technique.

### 6.2 Independent configuration dimensions

Objective requirements, selected techniques, the selected LightPath and detector output are related inputs to one configuration-resolution problem. The LightPath groups the OpticalPaths that operate together. Each imaging route terminates at an installed imaging-device component with a matching CameraSpecification, and its final camera input port is physically connected by the route's port adjacency. The camera identity is derived from that endpoint; the request does not independently select an unrelated camera. A manually supplied camera identity is only an optional assertion/constraint and must equal an endpoint derived from the selected LightPath. A single-camera LightPath resolves to one camera. A declared multi-camera LightPath resolves to its connected camera set only when the requested acquisition mode and physical splitter/trigger topology support it.

Examples:
- Selecting a 4× brightfield requirement resolves a compatible objective, illumination route, and the camera connected to the selected imaging route.
- Selecting phase contrast resolves the compatible phase plate/annulus arrangement and condenser path; a missing or mismatched pair makes that candidate invalid.
- Phase contrast and polarization may be composed only when their combined optical arrangement is physically supported: required elements must occupy their proper conjugate/pupil planes, the polarizer/analyzer state must not invalidate the phase arrangement, and all technique-specific settings must be jointly satisfiable. The resolver must not infer compatibility merely because both functions are present.
- Changing the selected imaging route can change the available camera and output modes, the total optical magnification, and the applicable optical calibration.

#### Current C++ implementation boundary

The following is the source-level state on the current v0.9 branch; it distinguishes implemented behavior from the target resolver contract:

- **LightPath/camera ownership is implemented.** LightPath references route IDs; each Imaging route terminates at an ImagingDevice component with a CameraSpecification on the same component identity. cameraEndpointIds() derives the connected endpoints. There is no independent camera list inside LightPath, and there should not be one.
- **Camera resolution is partially integrated.** LightPathCameraResolver derives camera/output-mode candidates and supports manual selection only among endpoints of the selected LightPath. LightPathConfigurationRequestFactory derives the route set and revision-binds that selection. ConfigurationResolver validates the binding. The canonical public request is not yet LightPath-first.
- **Whole-configuration resolution remains selection-driven.** ConfigurationRequest still requires exact objective, condenser, camera, camera mode, illumination device/channel and route identities. ConfigurationResolver validates those choices against a profile and equipment definition; it does not yet enumerate full objective/condenser/filter/illumination/route alternatives or return a complete discovered configuration.
- **OperationMode persistence already exists, but its semantics are still narrow.** The current revisioned OperationMode stores one ContrastMethod and per-LightPath calibrated irradiance settings. MicroscopeConfigurationSqliteStore persists its revisions and calibration references. It does not yet persist a composable technique set or required/preferred objective binding.
- **SampleType requirements exist, but the current source model violates the target ownership boundary.** `SampleType` already owns `MicroscopyRequirements`, but `SlidePreparationRequirements` is currently owned by a separate `SlideType`. The source also defines a `Sample` instance and makes `Slide` reference `SampleId` plus `SlideTypeId`. `SampleMicroscopyRequirementAssessor` currently accepts a `Sample` and a separate `Specimen`; this is not the target contract. The migration must remove `Sample`/`SampleId` and `SlideType`/`SlideTypeId`, move reusable preparation requirements into `SampleType`, put actual preparation facts directly on `Slide`, and assess `Slide + exact SampleType revision + resolved microscope configuration`. The current `MicroscopyRequirements` vector also rejects more than one Required technique; composable technique semantics must be migrated consistently.
- **Equipment constitution persistence remains outstanding.** The Core equipment aggregate is revisioned, and camera model catalog/binding plus mode/calibration persistence exist, but the full per-microscope component/port/connection/path/specification/LightPath constitution does not yet have its own durable store.

Do not describe the above partial pieces as a complete automatic resolver. The next coherent change must migrate OperationMode, SampleType requirements, Slide details, the slide requirement assessor, configuration request/resolution and transition planning together. Remove Sample/SampleId and SlideType/SlideTypeId from the microscope model; preserve optional OriginLabId only as an external reference. Extend the existing SQLite mode store rather than creating a competing OperationMode repository.

The implementation must not assume a universal hardcoded filter sequence for every microscope.

Profile settings are desired targets, not hardware state. The optional condenser aperture target must be inside the selected condenser's declared aperture-NA range. An illumination intensity target carries an explicit unit and must be supported by the selected channel's continuous control range with the exact same unit. An exposure preset must be inside the selected camera's declared exposure-time range; a preset cannot be accepted when the camera declares no exposure-time capability range.

ExposurePreset is distinct from ExposurePolicy. A profile may provide an initial exposure preset, but the request's Manual, Automatic or ConstrainedAutomatic policy remains an independent choice. Retaining an initial preset in ResolvedConfiguration does not issue an exposure command or override the policy. Phase 3D planning and Phase 4 execution decide whether/how the preset is applied.

### 6.3 Pure configuration pipeline

~~~text
Operation request + equipment-constitution revision
       + operation-mode revision + optional imaging intent
       + optional LightPath constraint
       + optional explicit component constraints
                         |
                         v
           Pure candidate configuration resolver
                         |
             +-----------+-----------+
             |           |           |
             v           v           v
          Resolved   Selection   Unsupported
          candidate  required    + diagnostics
             |
             v
       Valid alternatives + selection rationale
                         |
                         v
            Pure transition requirement planner
                         |
                         v
   Full desired-state dependency DAG / execution layers
                         |
                         v
          Phase 4 runtime reconciliation against
          verified effective state + execution
~~~

The resolver is independent of current runtime state and persistence. It evaluates equipment constitution, technique composition, mode constraints, optional imaging intent and explicit selections, then returns complete candidate configurations and any selection requirement. Phase 3D's transition planner conservatively emits the full desired requirement set from the chosen ResolvedConfiguration; it does not diff against current observations. Phase 4 runtime may reconcile this plan against a verified effective-configuration snapshot. Unknown or unverified observed values must not be treated as effective state.

Composition must be conflict-aware. Unrestricted last-write-wins merging is prohibited. Contradictory profile contributions produce explicit validation diagnostics.

The implemented Phase 3D plan uses typed requirement nodes for:
- Acquisition must be inactive.
- Selected optical path set, objective, condenser and additional optical components.
- Camera output mode and illumination channel.
- Optional condenser aperture and illumination intensity targets.
- Exposure policy and optional initial exposure preset.
- Final desired-configuration verification.

Edges mean prerequisite → dependent. Acquisition inactivity is a root prerequisite for every requirement; final verification is downstream of all requirements. Objective/path/component constraints order dependent camera and illumination requirements; independent selections remain in the same topological layer. The planner returns deterministic execution layers and rejects duplicate identities/edges, unknown references and cycles. ExposurePolicyMustBeHonored is separate from the optional InitialExposurePresetMustBeConsidered requirement.

The DAG is a dependency description, not an execution concurrency directive. A shared topological layer indicates no declared dependency between two requirements; Phase 4 still owns resource arbitration and adapter concurrency. The planner conservatively emits the full desired requirement set and does not remove nodes based on observations or assumed defaults.

These nodes remain desired-state facts, not commands. The plan does not assume selectors are motorized, does not define vendor operations, retries or execution state, and does not claim any step has completed. Those concerns belong to Phase 4. The detailed Core contract is specified in section 15.7 of MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md.


### 6.4 Editable operation modes, candidate resolution and objective sampling

An operation mode is a named, editable, revisioned data definition, not a C++ subclass for each combination such as 20× phase contrast. It describes the intended objective constraints, a set of selected techniques, optional equipment preferences, and supported operating targets. A mode may compose more than one technique. Each technique contributes declarative requirements, compatibility identities, exclusions, and optional settings. The resolver combines these contributions and rejects contradictions instead of applying last-write-wins behavior.

The user retains both workflows:

- Manual selection: the request can specify an exact objective, path, condenser, optical element, illumination channel or camera output mode. Every explicit identity is validated against the equipment constitution and route. An explicit selection is a constraint; it is never silently replaced by a different component.
- Automatic qualification: omitted identities are discovered from the installed constitution and the selected mode/technique requirements. The user may save preferred or required component identities in a microscope-specific mode binding.

A microscope-specific binding distinguishes a required component from a preferred component. A required identity is a hard constraint. A preferred identity ranks otherwise valid candidates but does not make an invalid route valid. This allows an operation mode to be portable across microscope models while still letting the user pin, for example, the preferred 20× objective on one particular microscope. If a saved required identity is no longer installed, resolution fails with a specific diagnostic. If a preferred identity is unavailable, the resolver may consider other valid candidates and must disclose that the preference could not be honored.

Resolution is candidate-based, not first-match-based:

1. Resolve the requested microscope equipment-constitution revision and operation-mode revision supplied in memory by Application.
2. Merge the mode's objective constraints and technique contributions with the selected Slide's exact SampleType requirement revision and explicit user selections. For calibration/direct imaging without a Slide, use only the mode and explicit technical request.
3. Enumerate complete route/component combinations. Validate that every required optical function is present, correctly positioned and compatibility-matched on the candidate paths.
4. Derive the camera from the imaging path endpoint. Enumerate only that camera's supported output modes. An unrelated camera elsewhere in the microscope is not eligible.
5. Reject candidates that violate hard constraints. Rank remaining candidates only by microscope-specific mode preferences and explicit technical preferences in the request. A SampleType may declare preferred techniques or objective constraints, but it must not contain microscope-specific component identities.
6. Return a structured outcome and the complete candidate set with the reasons/ranking used:
   - Resolved: one candidate is the unique winner under the declared hard constraints and preferences.
   - SelectionRequired: multiple valid candidates remain equally preferred; return the complete alternatives so the caller can ask the user to choose.
   - InputRequired: an essential criterion is absent and cannot be inferred safely (for example, no objective NA requirement or no applicable imaging intent). Return the exact input fields needed. A null magnification alone is not an input error because the minimum-adequate-magnification rule handles it.
   - Unsupported: no candidate satisfies the hard requirements; return diagnostics that explain why.
   Never choose the first item in a vector or rely on component insertion order.

A multi-microscope host evaluates the same high-level request against each target microscope's own equipment-constitution revision. Component IDs are microscope-scoped and are never assumed to be portable between instruments. If no target microscope is specified, the host may return capable microscope alternatives or request a target; it must not reuse a component identity from another instrument. The request's objective constraints (including NA and optional magnification) remain semantic inputs across instruments.

The selected SampleType may contribute reusable technical requirements and soft preferences—for example, minimum objective NA, allowed techniques or a preferred NA range. Those values are versioned in the SampleType definition. The resolver evaluates them as data; it does not infer a profile from patient, diagnosis, accession or an external workflow label.

#### Objective constraints and sampling adequacy

The objective requirement combines a minimum numerical aperture with an optional requested nominal magnification. If magnification is specified, it constrains the candidate objective/system magnification. If it is omitted, the resolver may choose the lowest installed objective magnification that satisfies the hard NA requirement, the selected imaging technique's optical-resolution model, and the camera-sampling condition. It must not choose an objective solely because it fits mechanically or has sufficient NA.

An objective candidate must also satisfy declared physical compatibility constraints relevant to the slide and optical train: immersion medium, working distance/clearance, coverglass design thickness when known, chromatic and field correction, compatible tube lens/relay optics, and usable field number versus the active camera sensor area. A SampleType preference cannot override these hard constraints.

A higher NA can improve the diffraction-limited lateral resolution, but it does not automatically make an image better for every specimen. Higher NA also changes depth of field, working distance, immersion/correction requirements, and the magnification needed to sample the finer optical detail. The condenser is part of the resolution model for transmitted-light imaging; it must not be ignored. The exact resolution criterion therefore belongs to the selected imaging modality/technique, not to a universal formula attached to every objective.

For the supported widefield cases, use the following Rayleigh lateral-resolution models, with all wavelength and distance quantities expressed in consistent units:

- **Transmitted (diascopic) widefield brightfield:** (r_{lat} = 1.22\lambda_{img}/(NA_{obj}+NA_{cond})). Use the effective image-forming/detected wavelength (\lambda_{img}) defined by the acquisition's spectral policy, and the condenser NA actually delivered by the selected aperture state. Do not substitute the condenser's maximum capability for its selected effective NA. Nikon's calculator uses 546 nm as a representative diascopic wavelength; this is a stated reference assumption, not a universal value for broadband visible-light illumination.
- **Reflected/episcopic widefield reflectance:** (r_{lat} = 0.61\lambda_{img}/NA_{obj}) under the stated widefield Rayleigh approximation, because the objective supplies both illumination and collection cones. Here (\lambda_{img}) is the relevant detected image-forming wavelength under the declared reflectance model.
- **Epi-fluorescence:** distinguish a scalar estimate from a complete fluorescence model. Nikon's calculator uses the detected emission wavelength in its simplified widefield Rayleigh estimate for episcopic/epifluorescence. That can be retained as a clearly labelled approximation. A physically fuller fluorescence image-formation model must account for both excitation and emission PSFs/transfer functions, their spectral bands, and the illumination/collection pupils. Until the selected model is implemented and its assumptions are recorded, do not present the scalar estimate as a complete fluorescence-resolution result.

For phase contrast in transmitted light, the condenser annulus defines an annular illumination pupil. Its inner/outer pupil geometry and effective illumination NA must come from the selected annulus/condenser state, and the annulus must be conjugate to the objective phase ring. A condenser's maximum aperture NA alone cannot establish this match. Darkfield likewise needs annular-pupil geometry: the direct illumination cone must lie outside the objective's collection cone while remaining inside the condenser's usable pupil. DIC requires its own prism/polarization transfer model; the presence of a polarizer, analyzer and two prisms alone does not establish the required shear or orientation. Unsupported technique models return unknown/insufficient evidence rather than reusing a brightfield equation.

- **Confocal:** do not reuse the widefield formula blindly. Resolution and optical sectioning depend on the confocal modality, excitation wavelength, pinhole/Airy-unit setting and where magnification occurs relative to the pinhole. For spinning-disk systems, system magnification before the disk affects the back-projected pinhole, while camera relay magnification after the disk affects detector sampling. A modality-specific model is required before the resolver can claim confocal sampling or optical-section adequacy.

These are optical-resolution estimates under the named model and assumptions, not guarantees of real-world image quality. Aberrations, misalignment, specimen preparation, refractive-index mismatch, focus, objective correction and detector noise can reduce achieved performance. The stored model and returned rationale must identify which criterion and reference wavelength(s) were used. For fluorescence, retain excitation and emission wavelength/band information separately. For broadband illumination/detection, the mode must define its reference-wavelength policy; the resolver must not treat an entire spectrum as one exact wavelength.

For a camera-based widefield route, define:

- (r_{lat}): the selected modality model's Rayleigh lateral separation in the specimen plane.
- (a_x,a_y): physical sensor pixel pitch in X and Y.
- (s_x,s_y): effective physical sensor-pixel pitches represented by one delivered image pixel, including the active readout/binning/decimation/downscaling mapping. Derive this from the selected CameraOutputMode's real sensor-region-to-delivered-dimension mapping and the declared hardware binning. Do not multiply two metadata values when they describe the same binning operation. Clamp digital upscaling to no improvement: (s_x,s_y ge 1).
- (M_{eff}): total specimen-to-sensor image-forming magnification of the selected imaging route. It includes the objective and all relevant system/tube-lens/camera-relay magnification factors in that route. Do not include display zoom or software image enlargement.

The effective object-space pixel pitches are:

(p_{obj,x} = a_x s_x/M_{eff}), and (p_{obj,y} = a_y s_y/M_{eff}).

The sampling rate in each axis is the Rayleigh separation divided by object-space pixel pitch. Under the default policy (k=2.3), both axes must satisfy:

(r_{lat}/p_{obj,x} \ge 2.3), and (r_{lat}/p_{obj,y} \ge 2.3).

Equivalently, the minimum total image-forming magnification is:

(M_{eff} \ge \max(2.3 a_x s_x/r_{lat},\;2.3 a_y s_y/r_{lat})).

For reflected widefield reflectance this becomes, per axis, (M_{eff} \ge 2.3 a s NA_{obj}/(0.61\lambda_{img})). For transmitted widefield brightfield it becomes (M_{eff} \ge 2.3 a s (NA_{obj}+NA_{cond})/(1.22\lambda_{img})). Epi-fluorescence must use its declared excitation/emission modality model rather than either shortcut. The resolver chooses the lowest available objective/relay combination that satisfies the selected model, both sensor axes, the requested NA and all other hard constraints.

The 2.3 value is a sampling-policy factor, not a new Rayleigh constant and not the camera's total pixel count. Nikon's microscopy calculator describes 2.3–4 samples per optical resolution distance as a commonly recommended high-resolution range; Hamamatsu recommends at least two and typically 2.5–3 pixels for high-resolution microscopy. The system default may be 2.3 because that is the selected project policy, but it should remain an explicit imaging requirement/policy rather than being confused with the physical Rayleigh criterion.

Sensor pixel **pitch** and effective readout mapping determine sampling interval. Active pixel **count** determines the available sensor dimensions and therefore field of view/image dimensions; by itself it does not make a fixed-pitch pixel sample finer. Approximate specimen field of view is (FOV_x=N_x p_{obj,x}), (FOV_y=N_y p_{obj,y}), where (N_x,N_y) are the delivered active image dimensions. A megapixel count alone is therefore insufficient to qualify resolution.

For the current CameraOutputMode data, derive the conservative sensor-pixel-per-delivered-pixel factor independently for X and Y from the sensor ROI, binning and delivered dimensions. The derivation must reflect actual hardware readout semantics. If the camera performs an undocumented scaling, demosaicing, super-resolution or other transformation, represent that capability explicitly or report sampling adequacy as unknown. Interpolation/upscaling cannot create independent optical samples.

The selected LightPath must be resolved before this calculation because it determines the actual camera, camera output modes and all image-forming relay components. The relay optics on a different path or an unconnected camera must never be used to qualify the candidate. If the reference wavelength, camera pixel pitch/mapping, or a magnification-affecting path component is unknown, report sampling adequacy as unknown/insufficient evidence rather than manufacturing a pass.

SampleType preferences may rank candidates only after hard optical and detector constraints pass. For example, a SampleType may prefer an NA range or a compatible technique; it must not name a microscope-specific objective ID. The user can override a soft preference, while a required microscope-specific OperationMode binding remains a hard constraint.

#### Camera suitability and mode-specific LightPath calibration

The C++ Core now defines the following initial value contracts:

- `CameraSensorCharacteristics` on `CameraSpecification`, including optional technology, shutter type, color mode, QE samples, read noise, dark current, full-well capacity, maximum frame rate and provenance.
- `CameraSuitabilityAssessor`, which returns warnings and an advisory ranking for fluorescence requirements without rejecting a physically connected, driver-operable camera solely for performance preference mismatches.
- `CameraSuitabilityAssessor` now uses detected-spectrum-weighted QE when the fluorescence requirements carry an emission spectrum, correctly typed dichroic and emission-filter response curves. It reports incomplete joint spectral coverage as unknown. The earlier flat band mean is retained only as a warned fallback when those spectral inputs are absent.
- `OpticalFilterSpecification`, attached to an installed equipment component and containing ordered spectral transmission samples, optional reflection samples, bounded linear interpolation methods and a compatibility identity.
- `FluorescenceEmissionSpectrum`, a validated relative spectral photon-flux distribution for a fluorophore/task emission profile.
- `SpectralPowerDistribution` on an illumination channel, describing wavelength-dependent source output.
- `LightPathCalibration`, a measured drive-to-specimen-irradiance curve bound to a microscope, equipment revision, operation-mode revision, LightPath, source channel and configured optical component identities.
- `LightPathModeSetting`, the operation mode's desired specimen-plane irradiance and exact calibration revision. It derives a device drive value only when its identity/revision binding matches and the target lies inside the measured calibration interval.

Persistence status is now partial and explicit. Application has SQLite stores for the shared camera model catalog and for immutable LightPath calibration/revisioned OperationMode records, with transaction and optimistic revision checks. The equipment-constitution store, installed-camera-to-catalog binding persistence and acquisition/configuration snapshot persistence remain outstanding. Application owns durable storage, revision creation, transaction boundaries and in-memory residency. The camera model catalog is reusable repository-level model data; the future per-microscope equipment store must reference installed camera components to stable model profiles. Per-microscope OperationMode records already own their LightPath mode settings and reference calibration revisions.

The initial calibration contract is intentionally conservative but not yet a complete physical calibration key. Before hardware calibration is implemented, replace or supplement the component-ID list with a canonical configuration fingerprint covering every state that can affect delivered specimen irradiance: selected routes and route revision; source/channel; objective; condenser/annulus/stop; excitation/neutral-density/dichroic/emission filters; aperture, iris and selector positions; relevant relay/beam-splitter state; and other adjustable optical elements. It must distinguish components that share identity but have different settings. The calibration record must also capture measurement plane, measurement instrument identity/calibration, units, date, uncertainty, environment/measurement conditions, and source-drive limits.

Calibration curves must be monotonic over the supported drive interval for the current interpolation model. If the real source/optical system is non-monotonic or has hysteresis, use separate direction/state curves or a richer model; do not force a single monotonic interpolator over physically different operating branches. Do not extrapolate outside measured calibration points. A filter/objective/stop/aperture/configuration change that alters the optical transfer must invalidate the old fingerprint binding, while an unrelated edit should not invalidate a calibration merely because a global revision number changed if the fingerprint is unchanged and the revision policy explicitly permits reuse.

The selected mode's irradiance target is a desired state. It does not prove the source has reached that value. Runtime must dispatch the derived source setting, obtain available device feedback and/or calibrated measurement evidence, and report the effective state and residual/uncertainty. If the source is fixed or has no intensity control, the mode may store a valid fixed measured output, but the resolver/runtime must not invent a controllable drive.

#### Persistence and revision semantics

Microscope constitution, camera model profiles and operation modes are durable, editable application data, but their persistence adapters are at different implementation stages.

The shared camera model catalog is now persisted by the Application-owned SQLite `CameraModelCatalogSqliteStore`. Profiles are keyed by normalized manufacturer/model/optional variant and retain model capabilities, sensor characteristics, QE samples, profile/catalog revisions and evidence references. Live discovery merges only exact catalog matches; incomplete model identity returns candidate profiles rather than silently choosing one.

OperationMode is an immutable revisioned Core aggregate. The Application-owned SQLite `MicroscopeConfigurationSqliteStore` persists append-only LightPath calibration revisions and versioned OperationMode records. A mode owns its per-LightPath irradiance settings; each saved setting is checked against a persisted calibration with matching microscope, equipment revision, mode identity/revision, LightPath, source/channel and component identities. Updates use optimistic revision checks and preserve historical mode/calibration records.

The Application-owned SQLite `CameraComponentModelBindingSqliteStore` persists the stable model-profile binding for each installed camera component, scoped to a microscope and exact equipment-definition revision. A binding records manufacturer/model/variant and profile revision, not a transient SDK handle. It validates the component is an installed ImagingDevice with a CameraSpecification and that the exact profile revision exists in the loaded shared catalog. Bound discovery fills missing runtime identity from this saved binding, but refuses conflicting live identity or a stale profile revision. The resulting Core camera projection preserves equipment-owned geometry/output modes and identifies the installed component it belongs to.

The microscope equipment constitution must record installed component identities, specifications, ports, connections, optical paths, filter specifications and illumination-source/channel specifications. `MicroscopeEquipmentDefinition` now carries an explicit immutable revision (revision one is the compatibility default), and `LightPathCameraResolver`, `LightPathConfigurationRequestFactory` and `ConfigurationResolver` reject a mode/request whose equipment revision differs from the loaded constitution. **The durable SQLite serialization/store is not yet implemented.** The current camera component binding store is scoped to an equipment revision supplied by the caller, but cannot yet prove that this revision was loaded from an authoritative durable equipment record. The next persistence slice must add the complete equipment-constitution store. Historical camera model profile-version retention is also required so an old equipment revision can reconstruct the exact profile revision it referenced after the shared catalog advances.

Application loads the available durable camera catalog, equipment and mode/calibration revisions into owner-retained in-memory objects. Core resolution receives typed values and remains free of persistence I/O. Editing a mode creates a new durable revision at an explicit save boundary. Editing equipment likewise must create a new revision once its store is implemented; a historical acquisition/configuration snapshot must keep the exact revisions it used. A saved mode edit must not mutate an already-running resolution or silently rewrite historical acquisition configuration.

Persisted mode definitions are not proof of effective hardware state. A resolved desired configuration remains separate from the observations required to verify that the microscope physically reached it.

### 6.5 Commercial microscope-software comparison

The model follows documented concepts from established microscope-control software while remaining vendor-neutral:

| Commercial software | Documented behavior | Architectural implication |
|---|---|---|
| Nikon NIS-Elements | Device Manager defines hardware configurations and light paths between camera and illumination devices. NIS describes a light path as the devices used together; an optical configuration specifies settings for devices in a light path and is associated with one light path. Acquisition instructions explicitly select a light path that includes a camera. | Treat the selectable LightPath as a device/route arrangement. Its camera endpoint is derived from that arrangement; camera settings belong to the selected path configuration, not to a global, unrelated camera list. |
| ZEISS ZEN | Smart Setup combines the configured microscope hardware with selected dyes and/or contrast methods, then proposes acquisition setups; it does not propose an unsupported combination when the current hardware cannot satisfy it. | Treat mode/technique selection as a constrained proposal problem over the actual instrument constitution. Return alternatives and explain unsupported combinations rather than fabricating a profile match. |
| Evident cellSens | The published compatibility table documents simultaneous multicolor acquisition with two specified identical-camera models or an image splitter, and lists supported W-View Gemini, Dual View DV2 and QuadView QV2 splitter hardware. | Multi-camera acquisition must be modeled as a real optical splitter topology plus the required acquisition/trigger capabilities; two camera IDs alone do not establish a usable simultaneous arrangement. |
| Leica LAS X | Leica documents software configuration of objectives and filter cubes and saving/recalling acquisition and configuration settings for reproducible experiments. | Keep microscope-specific editable modes separate from the immutable configuration/acquisition snapshot used to reproduce a run. |

These products have different hardware support, licensing and model details; the comparison uses their documented behavior as design evidence, not as a claim that their internal software architectures are identical. RP should preserve the useful workflow semantics without importing vendor-specific classes or assuming every microscope has the same motorized components.

Primary references:
- [Nikon NIS-Elements Device Manager — hardware configurations, Light Paths and Optical Configurations](https://www.nisoftware.net/NikonSaleApplication/Help/Docs-AR/eng_ar/visual.device.manager.html)
- [Nikon NIS-Elements Experiment Acquisition — select a light path that includes a camera](https://www.nisoftware.net/NikonSaleApplication/Help/Docs-D/eng_d/experiment.acquisition.html)
- [ZEISS ZEN Smart Setup — proposals based on microscope hardware and selected dyes/contrast methods](https://knowledge.zeiss.com/rms/en/zen-core/toolkits-modules/acquisition-toolkits/multi-channel-acquisition/smart-setup)
- [ZEISS optical light paths — image-forming and illumination paths, conjugate planes, condenser annuli and objective back focal plane](https://www.zeiss.com/microscopy/en/resources/insights-hub/foundational-knowledge/optical-light-paths-optimal-imaging.html)
- [Evident cellSens — supported cameras, image splitters and simultaneous multicolor requirements](https://evidentscientific.com/en/products/software/cellsens)
- [Leica LAS X — configuration of objectives/filter cubes and saving/recalling acquisition settings](https://www.leica-microsystems.com/products/microscope-software/p/leica-las-x-ls/)

Primary physics and sampling references:
- [Nikon Microscopy Resolution Calculator — objective/system/relay magnification, diascopic/episcopic Rayleigh resolution, confocal pinhole response and camera sampling](https://www.microscope.healthcare.nikon.com/microtools/resolution-calculator/)
- [Nikon MicroscopyU — phase-contrast objective, conjugate phase plate and condenser annulus requirements](https://www.microscopyu.com/microscopy-basics/specialized-microscope-objectives)
- [Hamamatsu — sensor pixel pitch, active pixel count, field of view and recommended camera sampling](https://camera.hamamatsu.com/jp/en/learn/technical_information/thechnical_guide/dissecting_camera/dissecting_camera02.html)
- [Leica Microsystems — digital microscopy resolution, Nyquist sampling and sensor/display limits](https://www.leica-microsystems.com/science-lab/industrial/understanding-clearly-the-magnification-of-microscopy/)

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

Ingress submission outcomes are Enqueued, QueueFull, WrongRuntimeScope and Closed. WrongRuntimeScope rejects a receipt whose runtime incarnation differs and does not consume an ingress sequence. Enqueued transfers ownership of the feedback to Core; it does not mean that an adapter accepted a hardware command. On QueueFull, the ingress itself must latch feedback loss, mark affected device/runtime state unknown and wake the serialized runtime through a control path independent of the full queue before returning. The implemented typed BoundedFeedbackInbox uses a fixed-capacity ring allocated at creation; it assigns sequence numbers only with successful FIFO insertion and retains the loss latch until the runtime acknowledges reconciliation. Its runtime wake path is a non-throwing IFeedbackRuntimeSignal with distinct notifications for successful enqueue, feedback loss and ingress closure. The serialized owner can therefore wake on accepted feedback without allowing the callback to run state-reduction code.

RuntimeDeviceSessionBinding matches the full active MicroscopeId/RuntimeId/RuntimeGeneration/DeviceId/DeviceSessionGeneration tuple. ActiveOperationStepBinding matches OperationId/StepId and rejects a supplied superseded CommandId; action-completion facts that are step-correlated without CommandId remain permitted by the feedback contract. These are freshness gates for the serialized runtime, not authorization tokens.

SerializedOperationRuntime<TPayload> is the Core single-writer operation-state owner for one runtime incarnation. Application binds it to one worker thread and owns its lifetime. All state-changing operation/step methods enforce that thread affinity; adapter threads receive only IFeedbackIngress<TPayload>. The runtime validates the active device session, then the step's dispatch-time DeviceId/DeviceSessionGeneration and OperationId/StepId/CommandId before reducing feedback. The validated FeedbackReceipt is returned to the owner with its typed capability payload so the owner can apply the relevant capability-state reducer on the same thread.

Application supplies RuntimeWakeSignal as the IFeedbackRuntimeSignal implementation and runs SerializedRuntimeWorker::run on an Application-owned dedicated thread. The worker binds the Core runtime to that thread, snapshots the wake generation before draining, drains feedback receipts in FIFO order until the inbox is empty, then invokes the host's typed control-request drain. It waits against the generation captured before draining; a feedback or control request arriving during the drain therefore makes the wait return immediately instead of being lost. The worker does not create the thread, call device drivers or introduce a universal command bus. The host owns those lifecycle and capability-specific responsibilities. The loop exits only after ingress is closed and all accepted receipts have been drained.

StagePositionFeedbackReducer is the Application capability reducer for the dual-slide X/Y plane. It applies AxisPositionObservation only when Core classified the feedback as current-session AppliedToOperation or UncorrelatedDeviceFeedback. Stale/invalid feedback, non-position payloads and axes outside the in-plane X/Y pair do not update StagePositionState. A latched feedback-loss condition invalidates both in-plane machine references; preserved pre-loss receipts cannot restore them. Ordinary position telemetry does not establish a machine reference or count as verified homing.

Replacing or closing a device session retires active steps dispatched on that generation and fails or partially completes their operation; feedback from a different currently open device cannot satisfy the old step. A DeviceActionCompleted fact advances a step to AwaitingObservation, never to Verified. A subsequent device-state observation may move the step to Verifying. For an open-loop axis, Core may instead derive a position estimate from the completed command and its calibrated physical error contribution; after the Application evidence policy accepts that bounded estimate, the owner invokes the explicit VerificationStarted control transition from AwaitingObservation. This is not device feedback and must never be reported as a measured position. Both paths converge on the same Core step-verification predicate. The operation enters Verifying only when its steps are Verified. Queue loss blocks further command dispatch and verification, and the loss latch can only be acknowledged after the inbox is drained and the owner has reconciled device state. The Core runtime does not own the worker thread, vendor driver calls, or subscriber notification delivery; those remain Application responsibilities. It cannot rely on a later adapter callback to report the loss or wait for queue capacity to recover. Correlated loss blocks verification of affected work; uncorrelated telemetry loss makes that device's observed state uncertain. The condition is cleared only after explicit reconciliation/readback establishes the required state. Closed means that runtime ingress has been disposed.

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

### 10.3 Motion calibration: physical axis movement and controller ownership

Motion calibration is independent from optical calibration and is scoped to a physical axis and its relevant driver/controller configuration.

The public Core motion contract uses the physical coordinate unit declared by the installed axis. It does not expose motor-step counts, pulse timing or vendor protocol values.

- A STEP/DIR driver owns physical-unit-to-step conversion and deterministic pulse generation in its execution layer.
- An intelligent-controller driver sends engineering coordinates and leaves configured steps-per-unit conversion and trajectory execution to the controller.
- Exactly one component in the active driver/controller chain owns physical-unit-to-controller-unit conversion. Core must not apply a second controller scaling factor.

A driver position payload is an AxisPositionObservation: physical position, StagePositionEvidence (ControllerReported, EncoderMeasured or ExternalMeasured), and uncertainty in the declared axis unit. FeedbackEnvelope separately carries ObservationSource and runtime/device/operation correlation. CommandDerivedEstimate is not accepted as driver measurement feedback; Core derives it from completed movement and the motion error contribution.

Machine-reference establishment is a separate HomeReferenceEstablished feedback fact. Its StageHomeReferenceObservation payload carries a complete, non-empty set of AxisPositionObservation values at the established reference; duplicate axes are rejected. The fact must be correlated to the exact active HomeAxes operation step and CommandId and must come from HardwareReadback, DeviceEvent or SdkStatus provenance, not a calibration-derived estimate. The adapter owns the physical reference procedure (switch, encoder index, hard stop or controller-native routine) and reports only its normalized result; Core does not prescribe the mechanism. The host compares the complete axis set with the original HomeAxes request, applies the fact only after the matching operation step is Verified, and then updates the X/Y StagePositionState. DeviceActionCompleted or an Idle observation alone never establishes the machine reference.


Core motion calibration retains physical measurement evidence: observed displacement, fitted physical-axis behavior, residuals, accuracy, repeatability/precision and uncertainty. Controller steps-per-unit settings and command-derived counts are not proof of physical accuracy.

If position feedback is absent, Core records command-derived position as an estimate, not a measured position. Uncertainty remains explicit and may trigger a homing/recovery policy; homing does not correct scale error, backlash or slide movement.

No mechanical transmission construction details are required by the Core model.

### 10.4 Stage XY working surface and user-authored slide registration

The OPTIKA B293 has one horizontal motorized stage plane. The plane is modeled by the installed stage's two in-plane linear axes. Microscope focus Z is a separate axis with its own travel range, reference and position lifecycle. It is not a slide-selection axis and is not part of a slide rectangle.

The new working surface is an initially empty XY canvas. The user supplies its machine-coordinate origin and positive X/Y travel extents. The far boundary is derived from origin plus extent and checked against the configured stage-axis operating ranges. The canvas origin is not automatically the origin of any slide.

Each physical slide is added independently through constrained CAD geometry. The user supplies:
- The fixed mechanical stop-point endpoint in stage coordinates; this becomes the slide-local origin.
- An initial opposite-corner handle that determines positive/negative direction on the two axes.
- Whether constrained slide length is assigned to StageX or StageY.
- Constrained length and width.

The dimensions solve the final opposite corner while the stop-point remains fixed. The model retains the initial handle, solved corner and all four stage-coordinate corners. Geometry is axis-aligned in the stage XY plane; it is not an affine calibration fitted from four independent sensor measurements.

The number of slide rectangles is variable. A new canvas contains zero slide rectangles. There are no implicit left/right slots, fixed-ten slots, pre-populated slide dimensions or inferred slide placements. A slide must fit wholly inside the canvas. Positive-area overlap between distinct slide rectangles is rejected; exact edge/corner contact is allowed. A mechanical clearance margin is introduced only when its physical value and owner are explicitly specified.

A slide's registration has a stable SlideId and a unique revision identity. Replacing geometry or moving/reseating a slide requires a new revision; reusing the same revision for changed geometry is rejected. A navigation target captures SlideId, slide-local XY and the exact registration revision. A queued target is rejected if that registration has changed before execution. Selecting a physical slide changes the coordinate frame used for a navigation request; it does not itself move the stage.

For a constrained slide rectangle, slide-local point (u,v) maps into machine stage coordinates through the rectangle's orthogonal XY basis:

    p_stage = stopPoint + axisX * u + axisY * v

The inverse applies the transpose of the same orthonormal basis. The local transform is a 2D mapping. It has no slide-local Z, thickness, surface-height or stop-point uncertainty field. A selected slide determines the XY reference frame only.

StagePosition3D is the single machine-coordinate authority for X, Y and focus Z. Slide-local position is derived from machine X/Y and the selected slide's current registration; it is not maintained as a competing position state. XY projection does not require focus Z to be homed. Z observations or movement do not alter slide-local XY.

StageNavigationPlanner selects direct XY movement only when both in-plane axis references are usable and their accumulated uncertainty remains within the configured per-axis policy. If either X or Y requires homing, both are homed because together they define the XY machine coordinate. The original revision-bound slide-local target is restored only after the exact correlated X/Y home fact and operation step are verified. A changed registration revision rejects restoration. Homing does not repair a slide that was moved or reseated.

The focus-Z reference procedure is configured separately. With a physical limit switch, the matching limit observation establishes the configured reference. Without a switch, a FullRangeSweep may be used only when the focus mechanism declares ContinuousKnobRotationWithoutClash. Without an independent encoder/external boundary observation, the completed sweep establishes a calibrated estimate, not measured position.

The working surface and slide registrations are runtime/document state owned by the persistent stage subsystem and saved through the application persistence boundary. They are not static microscope-equipment specification and are not camera-preview pane identities.
 
### 10.5 Slide/acquisition calibration history

Calibration records are owned by the microscope/configuration calibration stores, not duplicated inside every slide.

Each acquisition and physical measurement preserves the calibration revision and relevant values used. A slide is therefore interpretable historically without owning another copy of the calibration system.

## 11. SampleType, physical slides and acquisition

### 11.1 SampleType is a reusable technical requirement profile

`SampleType` is intentionally part of the microscope application. It is a versioned definition used to express the microscopy and slide-preparation requirements associated with a kind of material. It is not an individual collected sample and it is not a patient/laboratory record.

A SampleType contains:
- Stable `SampleTypeId`, revision, name and description.
- A broad material class where relevant (biological, botanical, geological, material-science, calibration artifact or other).
- `MicroscopyRequirements`: required, preferred, supported or prohibited imaging techniques; objective NA/magnification constraints; camera performance requirements; condenser constraints; illumination/spectral/irradiance requirements.
- Optional `SlidePreparationRequirements`: allowed preparation methods and fixation states, physical thickness ranges, coverglass ranges, mounting-medium requirements and required label/contrast-agent metadata.

The requirement profile is reusable and independent of microscope-specific component IDs. It may state that a technique is required or that an objective must meet a minimum NA; it must not pin a camera, objective component ID, filter ID or other identity belonging to one microscope. The same SampleType can be evaluated against different microscope equipment definitions.

A SampleType catalog stores versioned definitions. A new revision does not silently rewrite existing slides or acquisition history. The exact revision used is explicit.

The model has no Sample entity and no SampleId. Do not add a parent sample record merely to connect a Slide to its type.

### 11.2 Slide is an independent physical record

A `Slide` is a physical prepared carrier/slide and an imaging target. It directly references one SampleType revision. The relationship is:

~~~text
SampleType (definition, revisioned)
    1
    |
    | 0..N
    v
Slide (physical record)
    ├── SlideId / revision
    ├── SampleTypeId / SampleTypeRevision
    ├── label/name
    ├── physical SlideGeometry
    ├── actual SlidePreparation details
    └── optional OriginLab reference
~~~

A Slide contains:
- Stable `SlideId` and record revision.
- Required `SampleTypeId` and exact `SampleTypeRevision`.
- Operator-facing name/label.
- Physical geometry (dimensions and any explicitly measured/declared physical properties needed by the stage or optics).
- Actual preparation/details for this physical slide: preparation method, fixation state where applicable, measured section and coverglass thickness, mounting-medium facts and relevant fluorescence/chromogenic label spectral metadata.
- Optional origin-laboratory identifier, described below.

There is no separate `Sample`, `Specimen` parent record or `SlideType` taxonomy. Reusable allowed-preparation constraints belong to SampleType; actual preparation facts belong to Slide. A Slide's facts are assessed against the exact SampleType revision it references. Unknown physical values remain absent; they must not be filled from SampleType defaults.

A SlideType-like preparation template must not be introduced as a second taxonomy unless a concrete requirement later demonstrates that it is semantically independent of SampleType. The current requirement model places the reusable preparation envelope on SampleType.

### 11.3 Optional origin-laboratory reference

A Slide may carry an optional `OriginLabId` when an upstream workflow supplies one. It is an opaque external identifier, not a microscope-owned Lab entity or a foreign key. The microscope application:
- Preserves and returns the supplied value unchanged.
- Does not resolve, validate against, synchronize or manage the originating laboratory.
- Does not require the value for slide creation or acquisition.
- Does not use it as SlideId, SampleTypeId or an authorization identity.

If the external lab's IDs are only unique within its own system, store the issuing system/namespace together with OriginLabId so references from different systems cannot collide. Patient identity, clinical accession and source-sample identity are not stored by this application.

### 11.4 Physical slide geometry and stage registration

The physical slide geometry describes the carrier. Its stage placement/registration is separate mechanical state: a measured mapping between slide-local coordinates and the stage-machine coordinate frame, with calibration revision, correspondences, residuals and uncertainty.

Stage navigation uses SlideId plus the exact registration revision. It does not use SampleTypeId to calculate coordinates. Changing SampleType requirements does not alter physical geometry or registration. Replacing/reseating/redrawing a slide invalidates the affected registration and queued targets according to the stage lifecycle.

The working surface may contain zero or any number of user-defined slide placements. The workspace origin is not a slide origin. Each placement remains independently registered. A Slide record may exist without a loaded/registered stage placement; stage-local navigation requires valid registration evidence.

### 11.5 Acquisition plan and run

An `AcquisitionPlan` describes intended microscope work:
- AcquisitionPlanId and immutable plan revision.
- Requested OperationMode/configuration and technical acquisition requirements.
- Optional SlideId and exact Slide revision when the plan targets a registered physical slide.
- The exact SampleTypeId/revision used for requirement assessment when a SampleType profile is applicable.
- Field targets in the relevant slide-local or stage-machine coordinate frame, plus requested Z positions, focus sequence, exposure brackets, camera output modes and capture policy where supported.

A plan is not owned by a Patient, Sample, SampleId, accession or laboratory workflow. Calibration-target imaging and direct camera capture may be represented without a Slide. Slide-targeted navigation requires a valid Slide and registration, but a universal “active patient/sample/slide session” is not an acquisition precondition.

An `AcquisitionRun` records one actual execution on one physical Microscope:
- AcquisitionRunId, optional AcquisitionPlanId and actual MicroscopeId.
- Optional SlideId and the exact Slide revision when slide-targeted.
- The SampleTypeId/revision actually used for technical assessment, if any.
- Optional OriginLabId snapshot when supplied on the selected Slide and needed for external correlation/audit.
- Runtime/operation correlation and start/end timestamps.
- Outcome/status, including incomplete and partially completed outcomes.
- Requested/resolved/effective microscope configuration and calibration revisions.
- Device observations, actual camera values and readback provenance.
- Produced acquisition fields and ImageFrameIds, completed/missing/failed steps and recovery checkpoints.

SampleType requirements guide technical suitability only. The microscope application does not infer or store the upstream patient/sample/laboratory records associated with an acquisition. The calling workflow correlates its own records with the returned AcquisitionRunId.

### 11.6 Acquisition fields and image frames

An `AcquisitionField` represents a field of view or scan target within one AcquisitionRun. It may have a stable AcquisitionFieldId, optional display label/order, optional slide-local or stage-machine target, focus/region geometry and calibration references. A field's order is not a physical coordinate. Unpositioned fields remain representable.

Each original `ImageFrame` has:
- ImageFrameId, AcquisitionRunId and optional AcquisitionFieldId.
- Immutable pixel-payload reference and integrity metadata.
- Capture timestamp, delivered pixel dimensions and pixel format.
- Source camera identity and readout/ROI/binning context.
- Actual exposure and other camera settings when supported/reported.
- Relevant optical configuration and calibration references.
- Position/focus observations when available, preserving source and uncertainty.

Z planes, exposure brackets and camera channels are explicit capture dimensions, not inferred from array position. HDR combines exposures for the same field and focal plane; it must not merge different fields or Z planes. Focus stacking is a separate operation across focal planes. Original frames remain immutable. Incomplete runs remain recoverable and marked incomplete.

### 11.7 Identity and ownership invariants

Microscope-owned identities include MicroscopeId, SampleTypeId, SlideId, AcquisitionPlanId, AcquisitionRunId, AcquisitionFieldId and ImageFrameId. PatientId and SampleId are not defined by this application. SlideTypeId is not defined because preparation requirements are part of SampleType.

The authoritative relationships are:
- One SampleType revision can be referenced by many Slides.
- Every Slide references exactly one SampleTypeId and exact SampleType revision.
- A Slide's optional OriginLabId is an external reference only; no Lab entity is persisted.
- An AcquisitionPlan/Run may reference a Slide when the acquisition targets that physical carrier; the relationship is optional for calibration and direct imaging.
- An ImageFrame belongs to exactly one AcquisitionRun and optionally one AcquisitionField belonging to that same run.

Do not create authoritative reverse SlideIds collections on SampleType; query Slides by SampleTypeId. Renaming a Slide, moving a package or changing workspace display order does not change its identity. Changes to SampleType are revisioned and do not silently mutate existing Slide references or completed acquisition history.

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

## 13. Persistence and imaging-data storage

Microscope persistence covers instrument constitution, deployment bindings, SampleType definitions and revisions, physical Slide records, stage registrations, operation modes, calibration, acquisition plans/runs, image payload references and image documents. It does not persist patient or individual source-sample entities.

### 13.1 Persistence ownership

- The SampleType catalog is authoritative for reusable, revisioned technical requirements. Each Slide pins the exact SampleType revision used for its preparation/assessment.
- The Slide record is authoritative for SlideId, SampleTypeId/revision, operator label, physical geometry, actual preparation details and optional external OriginLabId reference.
- Stage workspace placement/registration records are authoritative only for mechanical dimensions, coordinate transforms and measured registration evidence. They do not own SampleType requirements.
- OperationMode revisions and LightPath calibration records are authoritative for saved microscope operating intent and measured optical output calibration.
- Acquisition plans/runs record requested and actual microscope execution. A run identifies the actual MicroscopeId, optional Slide revision, SampleType revision used for assessment and exact configuration/calibration revisions.
- AcquisitionField metadata belongs to its run. ImageFrame metadata belongs to one run and optionally one field. Payloads are immutable objects referenced by stable storage references plus integrity metadata.
- Derived-image payloads and provenance are separate from original frames. Reprocessing creates new derived identities rather than overwriting originals.
- Workspace catalogs and recent-item lists are rebuildable indexes, not authoritative domain records.
- Patient, Sample and laboratory workflow records remain owned by the calling external system. That system correlates its own records with AcquisitionRunId.

### 13.1.1 In-memory materialization and residency

The persistence adapter stores and retrieves durable microscope/application records. It is not consulted for every domain-object read. The Application layer decides when persistent aggregates are loaded, materializes them as validated in-memory objects, and retains them for the duration of active use. Core operates on those objects and their supplied values; it does not perform storage I/O.

The loading and residency rules are:

- Load equipment definitions, SampleType definitions/revisions, operation modes, calibration, stage workspace, acquisition or image documents when first required; do not eagerly load the complete acquisition/image workspace at startup.
- Materialize the aggregate and relationship metadata required for that use. Load related records in bounded batches where practical. Accessing an already materialized relationship must not perform hidden storage I/O.
- Within one active workspace/runtime scope, keep one owner-retained in-memory instance for each active persistent identity. Consumers access it through the owning object's API or receive read-only projections; only the designated owner mutates it.
- Keep an opened object resident while selected, referenced by another active object or used by an in-flight workflow. Release it after it is no longer in use and all dirty state has been committed or explicitly retained for recovery.
- Perform mutations against the in-memory object. Persist them at an explicit save/transaction boundary or a domain checkpoint required for recovery. Do not reread the record after each mutation or before each subsequent operation.
- A failed persistence write leaves the in-memory object and its dirty state intact and visible as unsaved. It must not be discarded or overwritten by reloading an older durable version.
- Detect revision conflicts at commit and require explicit reconciliation. Do not silently refresh an active object from storage.
- Large immutable image payloads may remain file-backed and be decoded through bounded tile/frame buffers. The active image document and its metadata remain ordinary in-memory objects.

This is a lifecycle rule, not a mandate to add a global cache, generic unit-of-work framework or database-aware lazy-loading layer. The Application owner for a microscope runtime, stage workspace, acquisition or image document retains the objects it has activated and shares their references with consumers. Startup loads equipment/deployment definitions, current SampleType definitions, configuration and the catalog/index information needed to present available imaging records; larger acquisition/image aggregates load on demand.

A persisted stage position is not physical position evidence after restart. Stage axes must re-establish their references/observations through their hardware lifecycle.

### 13.2 Initial local-first imaging workspace layout

~~~text
Workspace/
├── microscope/
│   ├── equipment/
│   ├── sample-types/
│   │   └── {SampleTypeId}/
│   │       └── revisions/
│   │           └── {Revision}/
│   ├── operation-modes/
│   ├── calibration/
│   └── stage-workspaces/
│       └── {WorkspaceId}/
│           └── placements/
│               └── {SlideId}/
├── slides/
│   └── {SlideId}/
│       └── slide.json
├── acquisitions/
│   └── {AcquisitionRunId}/
│       ├── plan/
│       ├── run.json
│       ├── fields/
│       ├── images/
│       │   ├── original/
│       │   └── derived/
│       ├── recovery/
│       └── history/
├── image-documents/
│   └── {ImageDocumentId}/
│       ├── annotations/
│       ├── measurements/
│       └── thumbnails/
└── catalog/
    └── indexes (rebuildable)
~~~

This is a logical storage layout, not a Core API or a requirement that every adapter use directories. Braced ID directory names are conceptual storage keys, not permission to concatenate an opaque ID directly into a filesystem path. The storage adapter must deterministically encode IDs into safe portable path segments or maintain an explicit ID-to-storage-key mapping; it must prevent traversal, separator interpretation and collisions. The original ID remains in metadata and is not rewritten by path encoding. Image payload filenames/extensions and physical storage layout are adapter decisions.

There is no `samples/` or `patients/` package. The optional OriginLabId is a value on Slide, not a path, folder, foreign key or authority to fetch external laboratory records. External workflows retain their own patient/source records and correlate them to AcquisitionRunId.

### 13.3 Persistence and recovery invariants

- Validate that a Slide's SampleTypeId and exact SampleTypeRevision resolve before accepting a slide record or using its requirements for acquisition qualification.
- Validate that a SlideId used for slide-local navigation refers to a current registered mechanical placement in the loaded stage workspace.
- Validate that an AcquisitionFieldId belongs to the run before accepting a frame record.
- Persist an AcquisitionRun identity and recoverable initial state before dispatching hardware commands.
- Record a frame as available only after its immutable payload has been durably stored and its reference/integrity metadata has been committed.
- Preserve incomplete runs, missing frames and recovery checkpoints. Never infer success from the existence of a folder or plan.
- Make retries idempotent using stable run/frame identities; duplicate delivery must not create duplicate logical frames.
- Do not cascade-delete acquisition/image data as a side effect of changing a SampleType revision, Slide details, stage layout or equipment definition.
- Keep original image payloads immutable. Retention/deletion is an explicit imaging-data policy operation with reference checks.

DICOM is an interoperability/storage adapter, not the internal domain model. The Core must not force unpositioned fields into physical whole-slide coordinates. DICOM WSI representation is selected only when its spatial and pixel semantics can be represented truthfully. Exposure brackets must not be misrepresented as Z planes. Different optical scales may require distinct suitable image objects/series. Interoperability constraints are resolved by the DICOM adapter and documented, not hidden in the internal model.

## 14. Runtime, concurrency and resource ownership

Each physical microscope has one active controlling runtime at a time.

- Multiple microscope runtimes may exist in one application process.
- A host-level resource registry prevents two runtimes from claiming the same exclusive physical device.
- Multiple front ends should share one application/service host if they need concurrent access to the same physical microscope.
- Core operation/event handling is serialized per runtime so state transitions have one logical owner.
- Adapter callbacks enqueue normalized events; they do not mutate Core state directly on device threads.
- Conflicting operations declare resource claims and are serialized or rejected.
- The stage has one runtime machine-position owner. Slide-local position is derived through the selected slide's valid registration and is not maintained as a competing position authority.
- Position evidence distinguishes encoder/external measurement, controller-reported state and command-derived estimates. Each StageAxisPositionState is scoped to the installed stage EquipmentComponentId and StageAxisId, retains the machine-reference status and a conservative accumulated error bound. A controller-reported coordinate does not erase an open-loop error budget; independent encoder/external measurement may reset it to the measurement uncertainty. Unknown/unbounded error requires homing. StageNavigationPlanner homes both in-plane axes when either threshold is exceeded and restores the saved slide-local target only when the captured registration revision is still current.
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

**Exit:** matching optical configurations reuse calibration revisions; changed geometry-affecting configurations do not silently reuse incompatible values; slide-to-stage registration is modeled separately from axis motion calibration.

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
- **SampleType:** versioned, reusable imaging and slide-preparation requirement profile; not an individual sample record.
- **Slide:** independent physical-slide record referencing one SampleType revision and storing its own details/preparation.
- **OriginLabId:** optional opaque external identifier supplied by an originating laboratory; no Lab entity is owned by the microscope application.
- **AcquisitionField:** optional field-of-view/scan-target identity within one AcquisitionRun.
- **AcquisitionPlan:** immutable intended microscope-work specification; a physical Slide reference is optional and explicit.
- **AcquisitionRun:** actual execution record for one microscope, with optional Slide and SampleType revision references when applicable.
- **ImageFrame:** immutable original capture associated with one run and optional acquisition field.
- **Frontend:** a client of Microscope.Application; it is not an owner of microscope behavior.
