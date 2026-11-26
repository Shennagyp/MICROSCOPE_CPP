# OPTIKA B293 Application System Architecture — v1

**Status:** Architecture baseline for the complete stateful OPTIKA B293 application. This document defines the product-level object model and ownership boundaries; it does not claim that every target subsystem is implemented or hardware-integrated.

**Target:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Implementation baseline:** Native macOS application using SwiftUI, AppKit, Objective-C++, C++17 and the existing AmScope camera capability.

## 1. Purpose and authority

This document describes the application as one integrated microscope workstation. It establishes the long-lived object graph, subsystem responsibilities, state ownership, concurrency boundaries, persistence boundaries and implementation order.

It is deliberately an object-oriented application with functional calculations. It is **not** a stateless REST API, a collection of request handlers, a Redux-style application framework, or a set of per-command objects that reconstruct the microscope state every time a user acts.

Use the following authority hierarchy:

1. **This document** governs the complete OPTIKA B293 application: application object ownership, composition, state lifecycle, capability interactions, concurrency and UI boundaries.
2. [Microscope Platform Architecture v0.9](MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md) governs the microscope-domain model, equipment/configuration semantics, operation and feedback facts, calibration, acquisition records and persistence invariants.
3. [Stage Working Surface and Axis Reference v2](STAGE_WORKING_SURFACE_AND_AXIS_REFERENCE_V2.md) governs the physical stage XY canvas, slide rectangles, axis position and focus-Z referencing.
4. [OPTIKA B293 Dual-Slide Application Architecture](OPTIKA_B293_DUALSLIDE_APPLICATION_ARCHITECTURE.md) remains the implementation-specific reference for the existing native camera/preview path and its performance/lifetime constraints.

When details overlap, use the document that owns that concern. These documents are complementary; none should be interpreted as permission to duplicate state or add another framework.

## 2. Product model

OPTIKA B293 is a microscope workstation. It coordinates independent capabilities:

- Camera connection, camera configuration and acquisition.
- Microscope optics, illumination, condenser and optical calibration.
- Motorized stage coordinates, slide registration, homing and movement.
- Physical slide/sample organization and image-field acquisition.
- Image documents, measurements, annotations and derived-image processing.
- Workspace navigation, capture gallery and persistence.

The camera is one capability. It does not own the microscope, physical slides, stage, optical configuration, measurements or acquisition workflow.

Likewise, the stage does not own camera acquisition, and the UI does not own either device. A microscope-level workflow can coordinate capabilities, but no capability should depend on another capability's implementation.

### 2.1 Distinctions that must remain explicit

| Concept | Meaning | Must not be confused with |
|---|---|---|
| Microscope | One installed physical instrument and its runtime | An application window |
| Equipment definition | Relatively static description of installed hardware and capabilities | Current device state |
| Device session | One connected/usable lifetime of one hardware device | The whole microscope runtime |
| Microscope configuration | Requested/selected optical and operating configuration | Verified physical state |
| Stage working surface | User-defined machine-coordinate XY workspace and slide registrations | A camera image or preview pane |
| Physical Slide | A physical preparation with stable identity and its own registration | A presentation pane |
| Slide document | Image/source data and its annotations/measurements | Physical stage occupancy |
| Workspace | Current UI organization, selection and layout | Authoritative slide or hardware state |
| Acquisition plan | What the user intends to acquire | An acquisition run |
| Acquisition run | What the instrument actually attempted and recorded | A saved preview screenshot |
| Calibration | A versioned relationship supported by evidence | A nominal specification or a guessed constant |

A physical stage may contain a variable number of user-defined slide rectangles. The current two-pane preview layout is a presentation arrangement and does not define the number of physical slides, slide identities or stage locations. The UI must not use `Slide 1`/`Slide 2` pane identities as the authoritative physical `SlideId` model.

## 3. Application lifetime and object graph

### 3.1 Lifetime root

`OptikaB293ApplicationDelegate` retains one `OptikaB293Composition` for the application lifetime. The composition root constructs the long-lived subsystem objects and passes explicit references to their consumers.

The composition root is the only place that should know the concrete implementations required to assemble the application. It is not a service locator and does not need a general dependency-injection framework.

The target ownership graph is:

~~~text
NSApplication
└── OptikaB293ApplicationDelegate
    └── OptikaB293Composition                 (application lifetime)
        ├── MicroscopeRuntime                 (one per controlled instrument)
        │   ├── EquipmentDefinition           (read-only)
        │   ├── OpticalConfiguration          (long-lived owner)
        │   ├── OpticalCalibration             (revisioned records)
        │   ├── CameraCapability               (persistent controller/model)
        │   │   └── Camera device session      (opened/closed by lifecycle)
        │   ├── StageCapability                (persistent stage owner)
        │   │   ├── StageWorkingSurface2D      (current layout snapshot)
        │   │   ├── StagePosition3D            (current XYZ evidence)
        │   │   ├── Axis reference state       (per axis)
        │   │   └── Stage driver adapter       (long-lived dependency)
        │   ├── Microscope workflows           (configuration/acquisition/etc.)
        │   └── Active operation state         (owned by its workflow/runtime)
        ├── WorkspaceModel                     (UI selection/layout state)
        ├── Slide/Document workspace           (opened user data)
        ├── CaptureGalleryStore                (capture records/files)
        └── Application coordinators           (cross-capability policy)
~~~

This diagram expresses ownership, not a requirement to create one class for every box. A box becomes a class only when it has meaningful state, identity, behavior or resource lifetime. Do not create forwarding wrappers that own nothing.

### 3.2 One microscope runtime per physical instrument

A `MicroscopeRuntime` is the long-lived in-process owner for one installed microscope while the application controls it. The application may own multiple microscope runtimes in the future, but each physical device may be claimed by only one controlling runtime at a time.

The runtime and its capabilities persist across user actions. Selecting a slide, moving the stage, changing an objective or capturing an image calls methods on existing objects. It does not reconstruct the microscope, stage controller, camera controller, feedback store or driver.

The runtime is not a stateless API façade. It retains current configuration, observations, active device capabilities, ongoing operations, calibration references and lifecycle state. It provides controlled access to its subsystems and publishes read-only state to presentation.

Do not introduce an additional `Session`, `Manager`, `Coordinator` or `Context` class merely to wrap `MicroscopeRuntime`. Add an object only when it owns real state or a resource lifecycle that is not already owned elsewhere.

### 3.3 Capability ownership

The microscope runtime retains capability objects; each capability owns its own device-specific lifecycle and state.

- **Camera capability:** owns its camera controller, open device session, acquisition lifecycle, feature state and frame delivery contract.
- **Stage capability:** owns the stage's current working-surface layout, machine position, per-axis references, accumulated position uncertainty, active navigation and stage command lifecycle.
- **Optics capability:** owns the selected/desired optical configuration, objective-specific exposure profiles and calibration references.
- **Acquisition workflow:** coordinates camera, stage, optics and the active physical slide when a planned capture sequence requires more than one capability.
- **Slide/document workspace:** owns opened slide records and image-document state. It does not own the camera or stage device session.

Capability objects may expose narrow interfaces, but they retain their state. A caller may submit a command and observe its result; it does not replace the capability's state owner.

## 4. Object-oriented state with a functional core

Object-oriented design and functional programming have different jobs.

### 4.1 Stateful objects own

- Identity and lifetime of the microscope and its devices.
- Hardware handles, controller instances, worker threads and queues.
- Current camera connection/acquisition lifecycle.
- Current stage canvas, position, homing/reference state and active movement.
- Current microscope configuration and active workflow.
- Open slide documents, acquisition runs and persistence handles.
- The authoritative current state and its transition lifecycle.

These objects expose explicit methods. Their internal mutable state is changed only by the designated owner.

### 4.2 Pure functions calculate

Pure functions and value types are used for deterministic behavior that does not need to own a lifecycle:

- Stage geometry, four-corner slide solving, coordinate transforms, bounds and overlap tests.
- Navigation-plan selection and axis-unit conversion.
- Optical magnification, pixel-to-specimen transforms and sampling policy.
- Configuration validation and compatibility resolution.
- Image-document viewport transforms and overlay geometry.
- Measurement distances, fit/residual calculations and uncertainty propagation.
- Operation transition decisions and completion predicates, where a value transition makes the rule easier to verify.

A pure function takes explicit values and returns a value or typed error. It does not access a device, mutate an application object, persist data, publish UI state or schedule work.

### 4.3 How a state change occurs

~~~text
UI intent
   |
   v
Existing owner object receives a typed method call
   |
   +--> pure validation/planning over its current state
   |
   +--> owner accepts/rejects the plan
   |
   +--> owner creates/advances its operation state
   |
   +--> capability adapter performs the external effect
   |
   +--> adapter submits typed feedback
   |
   +--> the same owner evaluates evidence and updates its state
   |
   v
New read-only state is published to observers
~~~

The object owns the lifecycle; pure functions calculate the result. A reducer can be useful for a real state transition, but a normal stateful object method is preferable when no reducer abstraction adds value.

Do not create a universal reducer framework, event bus, actor hierarchy, service locator or generic workflow engine for this application.

## 5. Application layers and dependency direction

~~~text
SwiftUI / AppKit presentation
          |
          v
OptikaB293 application host and composition
          |
          v
Microscope.Application (C++ stateful application/runtime integration)
          |
          v
Microscope.Core (microscope semantics, state and pure behavior)
          ^
          |
  Core-defined capability ports
          |
   +------+------+----------------+
   |             |                |
 Camera adapter  Stage adapter    Persistence adapter
   |             |                |
 AmScope driver  Stage controller Local files/records
~~~

The current Swift host is a legitimate application shell, not a temporary stateless transport. The migration target is to move reusable microscope behavior and cross-frontend application/runtime ownership into C++ Core/Application while keeping SwiftUI/AppKit responsible for the macOS experience.

Dependency rules:

- Core does not depend on Application, Swift, Objective-C++, AppKit, SwiftUI, JSON, Camera::Common, AmScope SDKs, a specific storage layout or DICOM.
- Microscope.Application depends on Core and owns in-process application composition and runtime integration.
- Capability adapters implement the ports/contracts they serve and depend on their vendor/controller APIs. They do not own microscope policy.
- The AmScope driver does not know that OPTIKA B293 exists.
- The stage driver does not depend on the camera.
- Presentation does not directly call vendor SDKs or manipulate device handles.
- The composition root connects concrete adapters to long-lived capability objects.

There is no requirement to make every feature use the C++ libraries immediately. Migration is vertical and behavior-preserving. A transitional Swift feature remains the current owner until its equivalent C++ owner is usable; do not run two competing owners for the same state.

## 6. State ownership and sources of truth

Every state item has one authoritative owner. Other components receive observations or projections; they do not keep a second mutable copy that can drift.

| State | Authoritative owner | Notes |
|---|---|---|
| Installed hardware identity/specifications | Equipment definition | Static/versioned; not actual readback |
| Device handle and connection lifecycle | Device capability/adapter | One owner per physical device session |
| Requested optical configuration | Optics configuration owner | Desired state, not proof of selector position |
| Effective optical state | Verified observations recorded by runtime | Never inferred solely from a successful SDK call |
| Optical calibration | Calibration owner/store | Revisioned; bound to geometry-affecting configuration |
| Camera feature descriptors/readbacks | Camera capability | Invalidated on session/feature generation changes |
| Camera frame bytes and leases | Camera driver/frame pipeline | Not copied into generic UI state |
| Stage XY canvas and slide registrations | Stage capability | Variable number of rectangles; initially empty |
| Stage machine XYZ position | Stage capability | One machine-coordinate authority |
| Per-axis home/reference and uncertainty | Stage capability | Independent axis lifecycle; XY and focus Z are separate |
| Active operation and step status | The workflow/capability that created it | One owner; correlation is not caller-generated state |
| Physical slide/sample state | Open in-memory domain aggregate | Loaded once by stable identity when activated; durable persistence is its backing store; independent of current stage position |
| Workspace active selection and dock state | Workspace model | UI state, not physical device state |
| Image source and overlays | Slide document | Source-pixel coordinate system |
| Capture files and gallery listing | Capture store | Current PNG capture path is distinct from acquisition runs |
| Measurement calibration snapshot | Measurement record | Retains the calibration used when the measurement was made |

Requested, dispatched, adapter-accepted, device-acknowledged, action-completed, observed and verified are different facts. Never collapse them into one Boolean such as `isReady` or `isHomed` unless the semantics are explicitly limited to the exact evidence being represented.

## 7. Subsystem architecture

### 7.1 Camera subsystem

The existing camera architecture is retained.

Ownership:
- `Amscope::Driver` / `AmcamCamera` owns the vendor handle and acquisition internals.
- `NativeCameraController` owns the native application's serialized camera command path and controller lifetime.
- `AmscopeNativeCameraBridge` is the Objective-C++ language adapter. It does not own a second camera handle.
- `NativeCameraModel` is the current Swift-facing camera session/state owner.
- `NativeCameraFeatureModel` owns generic feature descriptors, readbacks, drafts and feature-write state. It borrows the same bridge and cannot open/close the camera.
- The dual-preview AppKit view is the one frame consumer. It owns the panes and their current displayed image state.

Camera connection lifecycle and frame acquisition are separate from microscope workflow state. A feature write cannot race a close/reopen and leave stale feature values visible. Session and feature generations reject stale asynchronous responses.

The live preview path remains:
- Vendor callback enqueues event codes only.
- One acquisition worker performs the SDK pull.
- A bounded frame pool produces immutable frame leases.
- A latest-frame mailbox retains at most the latest pending frame and one coalesced notification.
- One AppKit consumer takes the latest lease and renders it.
- The frame lease remains alive for as long as the image provider needs its bytes.

Frames do not travel through general application notifications, a SwiftUI `@Published` frame property, an unbounded queue or a per-frame task.

The current two-pane UI routes one camera stream to the selected pane. The inactive pane retains its previous image; a frozen pane still consumes/releases leases without creating a new image. That routing policy is presentation/workspace state, not a camera property.

The current gallery button saves the image already displayed in the selected pane. It is not the separate native still-capture operation. The capture worker makes the explicit copy needed to release the live camera lease before PNG compression and disk I/O. Preserve this distinction until a separate still-capture workflow is deliberately exposed.

### 7.2 Optics and microscope configuration

The optics owner represents the installed optical configuration and the user's desired/current selection. It is independent of camera implementation.

The current `MicroscopeConfigurationModel` owns:
- `MicroscopeOpticalConfiguration`.
- Objective inventory and active objective.
- Camera relay/reducer configuration.
- Condenser numerical aperture and wavelength/sampling policy inputs.
- Objective-specific exposure profiles.
- Editable configuration draft and persisted configuration.

`MicroscopeResolutionCoordinator` owns the automatic-resolution preference and coordinates the pure `NativeAutomaticResolutionPolicy` with camera readback and microscope configuration changes. The policy does not open a camera or issue commands.

`ObjectiveExposureCoordinator` applies the profile for the selected objective through the camera feature owner. The required sequence of writes remains serialized by the camera feature command path.

Calibration has its own identity and lifecycle. Nominal pixel scale is an estimate from equipment values. Measured ruler-slide calibration creates a revision with the configuration key, observations, fit/residual quality and uncertainty. Changing zoom, pan, viewport size or display scale never changes specimen calibration. Existing measurements keep the calibration snapshot they used.

The current automatic-readout interpretation remains provisional until the camera's sensor/readout behavior has been independently confirmed. A passing arithmetic test is not hardware evidence.

### 7.3 Stage and physical slide workspace

The stage capability owns one physical machine coordinate system and one XY working surface.

The working surface is created empty. The user defines:
- The canvas origin in machine coordinates `(x, y)`.
- The positive X and Y travel extents.

The canvas far boundary is derived from origin plus travel extents and validated against the physical stage axis ranges. The canvas origin is not the implicit origin of any slide.

Each user-drawn slide registration contains:
- A separately supplied mechanical stop-point endpoint, which defines that slide's local origin.
- The initial opposite-corner CAD handle, which defines the direction signs.
- The length-axis assignment (StageX or StageY).
- Constrained length and width.
- The solved opposite corner and all four stage-coordinate corners.
- A unique registration revision.

The number of slide rectangles is variable. The stage does not contain a fixed left/right or fixed-ten slide slot model. Slide registrations must fit the canvas, cannot have positive-area overlap, and cannot be edited in-place under a reused revision. Edge contact is allowed unless an explicit holder-clearance rule is introduced from physical evidence.

Stage state:
- `StagePosition3D` owns machine X, Y and focus Z state.
- The canvas and slide registrations provide XY mapping only.
- Focus Z travel/reference is independent and is never encoded as slide thickness or slide-local Z.
- Each axis has its own reference state, evidence and uncertainty.
- XY navigation homes X/Y only when required; a focus operation may reference Z independently.
- A real global device/session feedback loss may invalidate the whole machine state. An XY movement fault must not invalidate an independent valid focus-Z reference.

The application-level stage owner retains the stage runtime, operation state and bounded control queue for the lifetime of the controlling microscope runtime. UI producers submit a revision-bound target value. They do not supply operation/step/command identities or mutate stage state.

The stage driver is a separate persistent adapter. It owns controller communication and real-time execution. The stage capability owns micrometer coordinates, slide transforms, position evidence, accumulated error, homing policy and navigation. The driver reports typed completion/position/reference observations; the stage owner evaluates them and updates the authoritative state.

The focus-Z full-range sweep without an endstop remains an explicit reference workflow. It depends on the configured travel envelope and the declared continuous-knob-rotation-without-clash capability. Without independent position feedback, the result is a calibrated estimate, not measured position.

### 7.4 Workspace, slides and image documents

There are three separate models:

1. **Workspace UI model:** active view, selected document/slide, dock visibility, tab selection, pane layout and other transient interaction state.
2. **Physical slide/workspace model:** stable SlideIds, user-authored stage registrations, sample/preparation associations, physical occupancy and slide selection.
3. **Slide image document:** source image identity/payload, image-space overlays, measurements, annotations, view state and provenance.

The current `NativeWorkspaceModel` owns active two-pane target selection and dock state. Its `NativeSlideID.slide1/slide2` values are current presentation slots, not the eventual persistent physical `SlideId`.

The current `NativeSlidePreviewPane` and `NativeSlideImageView` own pane/image presentation state such as freeze, pan, zoom, fit, full-resolution view, tool selection and drawing interaction. Measurement and annotation coordinates belong to source-image space. Viewport/document transforms are presentation transforms; they never rewrite source coordinates.

A measurement stores the optical calibration revision/value used when it is created. If physical calibration is unknown, pixel distance remains available while specimen-space distance remains unavailable.

The saved-image viewer must be usable without a connected microscope. The live microscope view is an acquisition-oriented workflow and uses the camera's separate frame stream.

### 7.5 Capture, acquisition and processing

Keep three concepts separate:

- **Preview capture/gallery:** the current native app's explicit save of the displayed frame to PNG. It has a file/thumbnail lifecycle and does not imply a hardware still capture.
- **Hardware still capture:** a device operation that acquires a new frame through the camera driver's still-capture API. It requires its own command, feedback and failure lifecycle if exposed.
- **Acquisition run:** a domain record of an intended/actual microscope workflow tied to a physical Slide, Tile/field, microscope, configuration and calibration context.

An acquisition workflow coordinates stage positioning, microscope configuration and camera exposure/capture. It owns the run's operation state and recovery record. It does not own the camera's native handle, the stage driver's protocol state, or the frame-pool implementation.

The image-processing subsystem receives immutable source frames/documents and explicit parameters. It does not control camera acquisition or stage movement. Derived images retain their source frame, calibration, algorithm/version and parameter provenance. Original captures remain immutable.

The initial application does not require whole-slide scanning, stitching or an image-processing framework. Add concrete processing capabilities when a real workflow needs them.

### 7.6 Persistence

Persistence is divided by the type of state being saved:

- **Equipment definition/deployment:** relatively static microscope configuration and adapter binding. The domain model consumes typed values; Core does not parse JSON.
- **User preferences:** layout, selected options and similar user-level settings.
- **Microscope configuration:** selected optical configuration and objective-specific profiles.
- **Calibration records:** immutable versioned optical/motion calibration and its observations.
- **Workspace and slide data:** stable Sample/Slide/Tile identities, slide registration revisions, source metadata and document relationships.
- **Image/capture payloads:** original PNG/frame payloads and derived artifacts with integrity/provenance metadata.
- **Runtime checkpoints:** only where needed for recovery. A saved last coordinate is not proof that the physical stage is currently at that coordinate after restart.

The current implementation persists microscope optical configuration and dock width through `UserDefaults`, and saves preview PNGs to the user's Pictures folder. It does not yet provide the complete sample/slide/acquisition package store. Do not silently treat these transitional stores as the final data architecture.

A persistence object owns file access and transaction/lifetime behavior. Domain objects own the meaning and validation of the data. UI views do not write microscope records directly.

#### 7.6.1 In-memory object loading and residency

Persistence is the durable backing store. It is not the live object-access path. Once a persistent object is loaded for use, application behavior operates on its in-memory object state; ordinary property access, navigation through an already loaded relationship, and each subsequent command must not trigger another persistence read.

The lifecycle is:

1. **Load on activation.** When a Sample, Slide, Tile, image document, acquisition record or other persistent aggregate is first opened or required by a workflow, the application loads and validates it by stable identity.
2. **Materialize the required object graph.** Load the aggregate and the related metadata needed for the active use. Resolve required relationships in the same load/batch boundary where practical. Do not implement database-backed lazy properties or issue one query per relationship/property.
3. **Reuse the active instance.** Within its active workspace/runtime scope, a stable persistent identity resolves to one owner-retained in-memory instance. Consumers access that same instance through its owning object's API or receive read-only projections; only the designated owner mutates it. They must not construct competing mutable copies of the same active aggregate.
4. **Operate in memory.** Commands, calculations, navigation and edits use the loaded object. No per-command rereads, getter-triggered I/O, polling of persistence for current object state, or N+1 loading is permitted.
5. **Persist at declared commit boundaries.** The owning object/application workflow marks changes dirty and commits them through a persistence transaction or defined checkpoint. A commit boundary may be an explicit save or a domain transition that requires durable recovery; it is not every read or every incidental property access.
6. **Release after use.** Keep the object resident while it is open, selected for an active task, referenced by another active object or used by an in-flight workflow. Release it only after it is no longer in use and its dirty state has been committed or explicitly handled. A failed save must not cause the live dirty object to be discarded or silently reloaded over.
7. **Reopen as a new active incarnation.** After a clean close/release, a later activation loads the latest durable revision once. If an external revision conflict is detected during commit, report/reconcile it explicitly; do not silently refresh an object while it is active.

This is owner-scoped object residency, not a requirement for a generic global cache or an ORM identity-map framework. The existing workspace/document/runtime owner should retain the objects it has opened and share their references with consumers. Do not load every Sample, Slide, acquisition or image document at application startup merely to avoid later reads.

Large immutable image payloads are a separate resource concern: keep their document/frame metadata as in-memory objects while active, but read or decode pixel data through file-backed storage or bounded tile/frame buffers as the image workflow requires. Do not duplicate an entire large image into general application state just to keep its document open.

Persisted hardware coordinates are recovery/history data only. On application restart they do not prove the stage's current physical position and must not be restored as verified machine state.

## 8. Commands, operations and feedback

### 8.1 State-changing commands target existing objects

Examples of application intent are:

- Select or change the active microscope configuration.
- Submit a slide-local navigation target.
- Request an axis reference procedure.
- Start or stop acquisition.
- Open a physical slide document.
- Save a capture or apply an annotation.

The command is sent to the already-existing owner. The owner validates it against current state, creates or advances operation state where asynchronous behavior is needed, and dispatches through an injected adapter.

The caller does not create the operation, mutate the device state, fabricate correlation IDs or assume completion. The owning runtime allocates correlation identities internally.

### 8.2 Evidence lifecycle

For asynchronous hardware behavior, preserve these distinct stages:

1. User intent received.
2. Command validated/planned.
3. Operation and step accepted by the owning runtime.
4. Command submitted to adapter.
5. Adapter accepted or rejected responsibility.
6. Device acknowledgement/progress, if the device provides it.
7. Device action completion.
8. Device state observation/readback.
9. Owner evaluates the completion predicate and verifies the step.
10. Operation reaches a terminal state.

A successful port call or adapter acceptance is not physical completion. `DeviceActionCompleted` is not automatically `StepVerified`. Verified state requires the evidence appropriate to that operation.

### 8.3 Feedback ownership

Hardware callbacks do not mutate application objects from arbitrary device threads. They submit typed, correlated facts into the capability's bounded ingress. The owning thread processes them, checks device/runtime/session/operation/step/command identity and provenance, then updates the persistent object.

Do not route high-frequency camera frames through this operation-feedback path. Do not add a generic event bus. Use the existing camera frame mailbox for frames and capability-specific typed feedback queues for control operations.

### 8.4 Failure and recovery

Failure does not fabricate the requested state. The owner preserves the last trustworthy observation, marks uncertain state explicitly, terminates or pauses the relevant operation and exposes a recoverable condition.

Invalidation follows ownership scope:
- A malformed/off-target XY navigation result affects the stage XY movement/reference according to its evidence; it does not silently erase an independent focus-Z reference.
- A whole-device/session loss may invalidate all affected axes and outstanding device operations.
- A camera acquisition fault changes camera acquisition state and frame availability; it does not mutate stage position.
- A persistence failure does not delete an already completed capture or original payload.

Timeout, cancellation, adapter rejection, unknown submission outcome, disconnect, stale feedback and partial completion each require explicit handling by the owner. Do not infer that hardware stopped merely because the UI stopped waiting.

## 9. Concurrency and thread ownership

Concurrency is chosen per real resource, not imposed uniformly across the application.

| Execution context | Owns / performs | Must not do |
|---|---|---|
| Swift `MainActor` | App lifecycle, Swift observable presentation models, window/workspace UI state | Blocking camera/stage I/O or per-frame processing |
| Camera command executor | Serialized camera lifecycle and feature commands | Update Swift state directly or run the AppKit renderer |
| Camera acquisition worker | SDK image pull, frame-pool and mailbox publication | UI calls, rendering, unbounded buffering |
| Stage owner thread | Stage commands, feedback reduction, position/canvas state and operation transitions | Share mutable stage state with UI or driver callbacks |
| Hardware callback threads | Copy/enqueue bounded event or feedback facts | Mutate runtime state or perform lengthy SDK/controller work |
| Image/persistence workers | Explicit image processing, encoding and file operations | Retain a live camera lease longer than the job requires or mutate device state |

Each stateful aggregate has one logical writer. Frontend and background producers pass typed values/commands. Observers receive immutable copies/snapshots or narrow read-only projections.

The stage owner and camera controller may run concurrently because they own different physical devices and state. A microscope-level workflow coordinates their results without merging their device internals or forcing camera frames through the workflow operation queue.

## 10. Application startup and shutdown

### 10.1 Startup

1. The application delegate creates and retains the composition root.
2. The composition creates the long-lived application models and subsystem owners once.
3. Static equipment definitions, deployment bindings, user configuration and the minimal catalog/index metadata required by the startup UI are loaded and validated. Individual Sample, Slide, Tile, document and acquisition objects are materialized by stable identity when first opened or required by a workflow, then retained in memory while in use.
4. Device controllers exist with a defined disconnected/closed state. Device handles are opened only through their capability lifecycle.
5. The physical stage driver and stage runtime are bound once to the selected instrument; their owner thread and feedback ingress live for the runtime lifetime.
6. The UI observes the existing objects and submits commands.

Do not create a camera/stage controller for each button press. Do not create a generic session wrapper around the objects if the composition/runtime already owns the required lifecycle.

### 10.2 Shutdown

1. Stop accepting new commands for the runtime being closed.
2. Request safe completion/cancellation/stop through each capability owner.
3. Keep device adapters and callback ingress alive until outstanding callbacks and worker activity are resolved.
4. Stop acquisition workers and stage command workers using their device-safe lifecycle.
5. Drain or explicitly retire bounded queues and reject stale generations.
6. Persist dirty domain/workspace data and recovery checkpoints where applicable.
7. Release device handles and long-lived objects only after their owners report terminal shutdown.

If the physical state is uncertain, keep the application alive or report the unresolved condition rather than silently claiming the instrument is idle.

## 11. Current implementation versus target

This section distinguishes repository facts from architecture targets.

| Area | Current repository state | Target / outstanding boundary |
|---|---|---|
| Application lifetime | `OptikaB293ApplicationDelegate` retains `OptikaB293Composition` | Preserve as the application lifetime root |
| Camera | Existing AmScope driver, native controller, Objective-C++ bridge, Swift camera/feature models and AppKit preview | Preserve its device ownership, session generations, bounded acquisition and zero-copy frame lease pipeline |
| Optics | Swift `MicroscopeConfigurationModel`, draft store, automatic resolution and exposure coordinators | Migrate domain configuration/calibration behavior to Core/Application only when equivalent behavior and evidence exist |
| Workspace | Swift two-pane active target and dock model | Keep UI layout separate from physical SlideIds and variable-count stage registrations |
| Image document | AppKit image panes, source-pixel overlay/measurement state and geometry functions | Persist source image/document/annotation identities and calibration provenance |
| Preview capture | Gallery saves currently displayed frame to PNG | Keep distinct from true still acquisition and domain AcquisitionRun |
| Stage Core | C++17 XY working surface, slide geometry, machine XYZ state, axis reference and planners | Continue with stateful owner/runtime and hardware integration |
| Stage Application | Generic navigation executor/runtime/queue code exists as an incomplete migration slice | Reconcile against this ownership model before further implementation; no physical stage adapter is currently integrated |
| C++ platform | Core and Application libraries exist as separate CMake targets; the native app CMake target now links both | The Swift/Objective-C++ composition does not yet instantiate the stage runtime or call the C++ model; real stage specifications, a narrow bridge and the physical-stage adapter remain outstanding |
| Persistence | UserDefaults for current configuration/layout preference and PNG files for gallery | Implement versioned records plus on-demand in-memory materialization, active-object residency and transactional commits; do not reread records during ordinary object use |
| Processing | No complete processing subsystem | Add concrete image processing as pure operations over immutable source data when required |
| Runtime verification | Test sources and CMake registrations exist | Compilation, test execution, hardware checks and UI acceptance remain separate evidence gates |

The existence of a class or test target—and even a CMake link dependency—does not mean the capability is instantiated or connected to the manufactured hardware. The native app CMake target now links `Microscope::Core` and `Microscope::Application`; the remaining host integration is to supply real installed-axis specifications, create and retain the stateful C++ objects through the existing composition lifetime, and connect a narrow Objective-C++ bridge to the real physical-stage adapter. Do not construct a dummy stage specification or mock driver in production composition.

## 12. Implementation order

Implementation follows the dependency graph and current real consumers. Do not add speculative abstractions or build all future infrastructure before the first vertical workflow is usable.

### A. Establish the object/lifetime contract

- Keep the existing application delegate and composition as the lifetime root.
- Define the per-microscope runtime/capability ownership and teardown contract.
- Identify the authoritative owner for each state item before moving code.
- Remove contradictions between fixed-slot navigation and variable-count physical slide layout.
- Keep camera lifecycle and preview invariants unchanged.

**Exit:** one documented owner per runtime state/resource; no per-command construction of stateful devices or controllers.

### B. Complete one stateful stage vertical slice

- Reconcile the current generic stage planner/executor/runtime/queue against the single-owner contract.
- The persistent stage owner retains its canvas, XYZ position, axis references, active navigation and identity sequence.
- The queue accepts a revision-bound target only. The stage owner allocates operation identities and owns the operation lifecycle.
- Integrate the real external driver library through one long-lived `IStageMotionPort` adapter.
- Complete direct XY movement, X/Y homing and exact correlated restoration.
- Implement safe focus-Z reference lifecycle separately, including the no-switch full-range sweep.
- Publish immutable state to the application host/UI.

**Exit:** a real slide-local target is translated to a physical XY move, feedback updates the existing stage object, and a failure cannot leave false position or operation state.

### C. Compose stage into the application lifetime

- Add the stage adapter/controller to `OptikaB293Composition`/the per-microscope runtime once.
- Bind the device identity and stage owner thread once for that runtime lifetime.
- Expose a narrow Objective-C++/Swift command and snapshot bridge.
- Keep SwiftUI as a producer/observer; do not let it own the stage driver or mutable C++ stage state.

**Exit:** the native UI controls the same long-lived stage object for the complete application runtime.

### D. Migrate optics and calibration by ownership

- Move the microscope equipment/configuration semantics to Core.
- Preserve measured calibration revisions and historical measurement provenance.
- Move the pure resolution policy and configuration validation into the appropriate domain boundary.
- Keep camera-specific settings inside the camera capability; the optics owner decides the desired values.

**Exit:** no duplicate Swift/C++ optical source of truth and no camera driver dependency on objective policy.

### E. Add physical slide/document persistence and acquisition

- Persist variable-count slide registrations and revision identities.
- Establish Sample/Slide/Tile ownership and active-slide preconditions.
- Distinguish preview captures, device stills and AcquisitionRuns.
- Make incomplete runs recoverable and original image payloads immutable.
- Link measurements/annotations to source images and calibration revisions.

**Exit:** a slide can reopen without hardware; an acquisition cannot run without a valid active slide; historical measurements remain interpretable.

### F. Add image processing and further front ends only when needed

- Add deterministic processing functions over immutable image inputs.
- Persist derived-output provenance.
- Add another host or remote transport only for a concrete user workflow; it must use the same stateful application runtime rather than duplicate microscope behavior.

**Exit:** processing and new hosts do not take over device ownership or introduce a second state authority.

## 13. Non-goals and explicit prohibitions

- No stateless REST-style reconstruction of microscope/session state for individual commands.
- No per-request camera, stage, operation-runtime or device-adapter construction.
- No extra Session/Manager/Context wrapper that merely forwards calls or duplicates lifecycle state.
- No universal service locator, dependency-injection container, plugin framework, event bus or Redux clone.
- No camera ownership of slides, stage movement, optical policy or application workspace.
- No UI ownership of physical position, device handles, calibration or acquisition completion.
- No fixed physical slide count or left/right slot identity in the new stage model.
- No coupling between slide-local XY and focus Z.
- No generic application notifications for high-rate image frames.
- No claims of device completion, measured position, physical scale or successful tests without the corresponding evidence.

## 14. Definition of architectural completion

The architecture is coherent when:

1. Every persistent state item and hardware resource has one named owner and lifetime.
2. The composition root constructs the long-lived object graph once.
3. Each physical device has one controlling owner per runtime.
4. UI actions target existing owners and do not reconstruct runtime state.
5. Functional calculations are independent and deterministic where that improves correctness.
6. Feedback is returned to the owner that can verify and apply it.
7. Camera frame streaming remains independent of low-frequency workflow events.
8. Physical slides, UI panes, image documents and acquisition runs have distinct identities.
9. Failure, cancellation, recovery and shutdown preserve physical uncertainty.
10. The existing application can be migrated in vertical slices without two competing sources of truth.
