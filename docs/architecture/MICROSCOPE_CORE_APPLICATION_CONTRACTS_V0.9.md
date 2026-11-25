# Microscope Core and Application Contracts — v0.9

**Architecture reference:** MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Status:** Phase 1 contract specification; not an implementation claim.  
**Language baseline:** C++17.

## 1. Purpose

This document fixes the smallest public contract surface required to begin the two-library implementation.

- **Microscope.Core** owns microscope-domain meaning, validation, planning, operation state and feedback evaluation.
- **Microscope.Application** loads definitions/deployments, composes adapters, manages Core runtimes and exposes use cases to front ends.
- Hardware/storage adapters implement ports declared by the owning contract layer.
- Front ends submit application intents and render application projections.

This does not specify every future subsystem. Equipment-specific command types, persisted record schemas, calibration algorithms and acquisition payload details are introduced with the phases that consume them.

The first implementation uses C++17. Microscope.Core must not acquire Qt, Swift/Objective-C++, JSON, vendor SDK, DICOM or filesystem dependencies.

## 2. Ownership and dependency rules

| Concern | Owner | Boundary |
|---|---|---|
| Strong identities, result/diagnostic conventions and domain units | Core | Stable C++ value contracts |
| Equipment meaning and configuration rules | Core | Typed values and pure functions |
| Operation/step state and completion predicates | Core | Serialized runtime and pure state transitions |
| Hardware command semantics | Core | Capability-specific command types and ports |
| SDK/protocol translation and physical I/O | Adapter | Implements Core ports |
| Normalized hardware feedback | Adapter produces; Core evaluates | Correlated envelope and typed payload |
| JSON parsing and schema migration | Application / JSON infrastructure | Converts versioned input to typed Core values |
| Adapter factories and endpoint bindings | Application composition | Explicit registration and construction |
| Runtime lifecycle and frontend API | Application | Owns Core runtime lifetime and projections |
| UI and transient interaction | Front end | No microscope policy or SDK access |

Core must not depend on Application. Application may depend on Core. Adapter implementations depend on the Core port they implement and the relevant SDK/driver. The executable/host links Application and selected concrete adapters.

Do not introduce a generic plugin framework, service locator, universal message bus, universal workflow engine or generic string-based command API.

## 3. Public C++ type conventions

### 3.1 Strong identifiers

Use distinct strong types for at least:

- MicroscopeId, RuntimeId and RuntimeGeneration
- DeviceId and DeviceSessionGeneration
- SampleId, SlideId and TileId
- AcquisitionPlanId, AcquisitionRunId, ImageFrameId and DerivedImageId
- OperationId, StepId and CommandId
- A calibration identity/revision type when Phase 6 defines its lifecycle

An identifier is opaque. It is not a label, display index, path, serial number or timestamp. Its textual value must be non-empty, contain at least one non-whitespace character and contain no embedded NUL byte; internal spaces and path-like characters are otherwise not interpreted by Core. Strong types must not implicitly convert to each other or to an untyped string. Serialization uses a stable textual representation without making its format part of domain meaning.

Persisted IDs remain stable across renames, folder moves, exports and imports. The boundary that creates an entity supplies its ID: Application/storage creation workflows supply IDs for persisted entities, while future Core runtime/operation creation receives an injected identity source when those workflows are implemented. Core does not generate UUIDs or depend on a UUID library. The supplied source must provide collision-resistant values in the relevant identity namespace, and persistence rejects collisions rather than overwriting records. The ID value is not a filesystem path segment: storage adapters must encode/map opaque IDs to safe portable storage keys and preserve the original ID in metadata.

In Phase 1, StrongId::create validates a supplied non-empty value; it is not an identity generator. No generator implementation or generator port is added until a real entity/runtime creation use case consumes it.

DeviceId is the logical endpoint from the microscope definition. A vendor enumeration index or transient SDK handle is not a DeviceId.

### 3.2 Value and ownership semantics

- Small domain facts are value types; long-lived runtime ownership is explicit and RAII-based.
- Polymorphic public interfaces have virtual destructors.
- Use std::optional<T> for genuine absence; do not use undocumented zero/empty/sentinel values.
- Use std::variant for closed alternatives with distinct meanings.
- std::string_view is only for synchronous, non-retained input. Queued/retained work owns its strings and payloads.
- Public types expose no vendor handles, Qt objects, Swift objects or filesystem paths.
- Prefer concrete value types over inheritance hierarchies. Runtime polymorphism belongs at actual adapter-port and factory boundaries.

### 3.3 Units and numeric values

A physical quantity must not be an unexplained double in a public domain contract.

Use named quantity types or an explicit value-plus-unit representation with a documented canonical unit:

| Quantity | Initial canonical unit |
|---|---|
| Length / displacement | micrometre (µm) |
| Angle | radian (rad) |
| Duration | microsecond (µs) |
| Image coordinate | pixel (px), with defined axis and origin |
| Pixel count / dimensions | integer pixel count |

Serialized records carry explicit unit identifiers or a schema-defined unit. Conversion occurs at named boundaries. Do not create a general dimensional-analysis framework until actual operations require one.

These are representation conventions, not a requirement to add unused quantity wrappers in Phase 1. Introduce a concrete named quantity value with the first domain model that consumes it; do not create a broad generic unit system in advance.

Validate numeric finiteness and applicable ranges. Resolution, accuracy, precision, repeatability and uncertainty remain distinct. Conversion does not imply measurement accuracy. Device-native units remain in adapter translation code unless explicitly modeled as a reported domain observation.

## 4. Result, error and diagnostic conventions

Normal contract failures use typed results, not Boolean return values or exceptions for routine control flow.

Use a C++17 Result<T, E> convention based on a discriminated success/error value, with an explicit no-value specialization or Status<E> for operations that return no value. Result and Status types are marked [[nodiscard]] so callers are warned when an outcome is discarded. E is specific to the boundary/use case; do not collapse failures into one global string error.

A failure contains a stable machine-readable code, human-readable message, relevant entity/operation/device context and optional field/path context. Retryability is included only when the owner can justify it.

Adapter failures may preserve an opaque backend code and backend context. Core must not reinterpret a vendor native code as a cross-vendor semantic code.

A Diagnostic is separate from failure. Result reports whether the API operation produced its return value; DiagnosticSeverity reports the seriousness of a condition in that returned value/report. A successful validation operation may therefore return error-severity diagnostics describing an invalid model: the evaluation completed successfully, but its subject may not be acceptable. Consumers must not infer Result success/failure from the presence or severity of diagnostics. Diagnostic creation validates a known severity, a non-empty code containing no ASCII whitespace or NUL, a message containing non-whitespace characters and no NUL, and supplied context values containing at least one non-whitespace character and no NUL; absent context uses nullopt. User-facing text is not the machine-readable code.

Infrastructure/host boundaries may catch exceptions and translate them to typed failures. Exceptions must not escape device callbacks, cross a C ABI or represent ordinary validation, unavailable capability, timeout or rejected commands.

## 5. Operation and feedback contracts

### 5.1 Distinct facts and ownership

| Fact | Produced by | Meaning |
|---|---|---|
| OperationRequested | Application/front end | A domain intent was submitted |
| CommandDispatched | Core runtime | Core invoked a specific adapter port |
| AdapterAccepted | Adapter submission result, recorded by Core | Adapter accepted responsibility for handling the command |
| DeviceAcknowledged | Adapter, based on SDK/device evidence | Device/SDK acknowledged the command, when available |
| DeviceProgress | Adapter | Device/SDK reported progress |
| DeviceActionCompleted | Adapter | One physical action is reported complete |
| DeviceStateObserved | Adapter | A state value was read/reported, with provenance |
| StepVerified | Core | Evidence satisfies the step completion predicate |
| OperationSucceeded | Core | All required steps met required completion/assurance conditions |
| OperationFailed | Adapter or Core, with origin identified | Device/adapter or domain execution failed |
| TimeoutElapsed | Core runtime | Core deadline elapsed without sufficient evidence |

CommandDispatched is a Core fact, not adapter feedback. The port's immediate submission result distinguishes adapter acceptance from immediate rejection. Adapter acceptance means only that the adapter has assumed responsibility. Device acknowledgement is a separate later fact and may not exist for every device.

An SDK call returning or a selector stopping does not by itself prove the requested physical state. DeviceActionCompleted is not StepVerified.

### 5.2 Correlation and timestamps

Every adapter feedback item carries a runtime/device scope:

- MicroscopeId
- RuntimeId and RuntimeGeneration
- logical DeviceId
- DeviceSessionGeneration for the current open/reconnected session of that logical device

Core creates a new DeviceSessionGeneration whenever a logical device session is opened/reopened within a runtime and supplies it to the adapter with the session binding. The adapter must associate each callback with the exact session generation that produced it; it must not stamp an old callback with the currently active generation. Feedback from an earlier device-session generation is stale even when RuntimeGeneration is unchanged.

Feedback related to an active operation additionally carries one OperationCorrelation containing OperationId, StepId and optional CommandId. OperationId and StepId are an indivisible pair; a command correlation cannot exist without them. This avoids representing half-correlated work.

Unsolicited telemetry has the runtime/device scope but no OperationCorrelation. It cannot satisfy an active operation's completion predicate.

All adapter feedback also carries a normalized fact category and typed payload, source/provenance, optional adapter/device source sequence, and optional device timestamp with clock domain/timebase. Adapter/device source sequence is scoped to DeviceSessionGeneration; zero may be a valid first sequence and absence means the source does not report one. When supplied, the adapter preserves ordering for that session; if a backend counter wraps or loses ordering continuity, it starts a new device-session generation rather than reusing ambiguous values. Core FeedbackReceipt uses a distinct positive FeedbackIngressSequence type scoped to MicroscopeId, RuntimeId and RuntimeGeneration. It starts at one and orders feedback only within that exact runtime incarnation; its comparison operation rejects sequences from different scopes. The runtime assigns the sequence atomically with successful FIFO enqueue, in the same synchronization order as queue insertion, so concurrent adapter callbacks cannot create a sequence order that differs from processing order. QueueFull and Closed attempts do not produce a receipt. The sequence never wraps or reuses a value; on exhaustion the runtime ingress is retired and a new runtime generation is established before further feedback can be accepted. The device source and Core ingress sequence types are not implicitly convertible and must never be compared or substituted for each other.

FeedbackMetadata is constructed through a validator. DeviceAcknowledged requires OperationId/StepId correlation, CommandId, and DeviceEvent or SdkStatus provenance. DeviceActionCompleted requires OperationId/StepId correlation and HardwareReadback, DeviceEvent or SdkStatus provenance. OperatorConfirmation and CalibratedEstimate cannot be promoted into device acknowledgement/action-completion facts. They remain valid provenance for DeviceStateObserved. AdapterFailure requires the explicit AdapterReport provenance and cannot be labeled as a sensor readback. DeviceProgress and AdapterFailure may be correlated to an operation or may be unsolicited device-scoped facts when their semantics permit it. The factory returns typed errors for missing correlation or incompatible provenance.

FeedbackReceipt is created through a factory that verifies its FeedbackIngressSequence has the same MicroscopeId, RuntimeId and RuntimeGeneration as the contained feedback envelope. A receipt with mismatched scope is rejected, and the resulting receipt exposes read-only feedback/receipt metadata rather than a public aggregate constructor.

The initial fact categories are DeviceAcknowledged, DeviceProgress, DeviceActionCompleted, DeviceStateObserved and AdapterFailure. They describe the kind of fact, not its provenance. For example, DeviceStateObserved may be based on a hardware readback or SDK status. CommandDispatched and TimeoutElapsed remain Core runtime facts and are not adapter feedback kinds.

A present device timestamp is created with an explicit positive ticks-per-second value; zero is rejected. A supplied clock-domain label must satisfy the same non-empty/non-whitespace/no-NUL identifier rule; an unidentified clock domain is represented by nullopt. When the adapter cannot provide a valid timebase, it omits the timestamp rather than writing zero as an unknown sentinel.

When feedback enters Core, the runtime wraps it in Core-owned FeedbackReceipt metadata containing a monotonic steady-clock receipt time and a Core ingress sequence that starts at one and increases within the runtime generation. These are distinct from adapter source sequence and device timestamp. The steady-clock receipt value is runtime-local for ordering/deadline calculations and is not a durable wall-clock timestamp; a persisted audit timestamp, where required, is captured separately. Device timestamps are never compared directly with the Core monotonic clock without a documented clock mapping.

Feedback from an old runtime generation, old device-session generation, or a different/superseded operation, step or command is stale for that work. It may be retained diagnostically but must not mutate current operation state.

### 5.3 Typed feedback and provenance

The common correlation/metadata envelope is separate from the payload. Each TPayload is a concrete Core value defined for the capability and fact semantics that produce it; its fields preserve the actual observation/result rather than a generic success flag. FeedbackEnvelope is created through a factory and exposes read-only metadata/payload accessors; validated metadata cannot be replaced and payload data cannot be mutated after construction.

Do not use bare bool/string values, std::any, JSON objects, arbitrary string maps or vendor SDK structures as the feedback contract. Do not invent a universal device-state value bag. Add payload types only with a real capability port and preserve their provenance and units.

Observation provenance distinguishes hardware readback/encoder, device event, SDK status, operator confirmation, calibrated estimate, adapter report and unknown. Operator confirmation is not sensor verification. A calibrated estimate is not a measured position.

### 5.4 Capability-specific ports

Ports accept capability-specific Core command types. Examples introduced in their owning phases include objective selection, optical selector movement, illumination configuration, stage motion and camera acquisition.

Each port receives a typed command with correlation IDs, returns a typed immediate submission result (accepted responsibility or rejected), and emits normalized asynchronous feedback through the runtime-provided ingress. It does not mutate Core state. Vendor-specific details remain inside the adapter unless a normalized domain fact exists.

The feedback ingress has a different outcome: Enqueued means that Core accepted ownership of the feedback message; QueueFull and Closed mean it did not. Enqueued is not the AdapterAccepted command fact and must never be used to mark a hardware command as accepted.

On QueueFull, the Core ingress implementation itself atomically latches feedback loss and marks the affected device/runtime state as unknown before returning. It also wakes the serialized runtime through an out-of-band control path independent of the saturated feedback queue; setting a flag without waking the runtime is insufficient. It cannot rely on the adapter to submit a later overflow notification, because the queue may remain full or the device may emit no further callback. A correlated loss blocks verification of affected work; an uncorrelated device telemetry loss marks that device's observed state uncertain. Core may clear the loss condition only after an explicit reconciliation/readback establishes the required state. No operation may succeed while required feedback loss remains unresolved. Closed means the target runtime ingress has been disposed; late callbacks cannot reopen it.

Do not create a universal IDevice.Execute(DeviceCommand) port merely to avoid defining real capabilities. Factor shared mechanics only when real ports have identical semantics and lifecycle requirements.

The stage-motion port is capability-specific. Its initial command vocabulary is MoveAxesTo, MoveAxesBy, HomeAxes and StopAxes. Values use the coordinate unit declared by the referenced StageAxisSpecification; controller-native steps, pulse timing, GRBL command strings, Zaber device units and vendor SDK structures do not cross the Core boundary.

A multi-axis MoveAxesTo/MoveAxesBy value represents one coordinated movement request. The adapter must preserve coordination or reject it; silently issuing sequential movements changes the request. HomeAxes delegates the controller-specific reference procedure to the driver. StopAxes requests a controlled stop and is not a safety-rated emergency-stop path.

A STEP/DIR driver performs calibrated physical-unit-to-step conversion and deterministic pulse execution. An intelligent-controller driver sends engineering coordinates and delegates configured axis scaling/trajectory execution to the controller. The active chain has exactly one authority for controller-coordinate conversion. Core retains independent physical motion calibration and position evidence.

The OPTIKA B293 dual-slide carrier is declared against one StageSpecification and two distinct linear axes in the horizontal stage plane. It has exactly Left and Right slots. Each occupied physical Slide has its own SlideGeometry and four-corner SlideRegistration. The four measured correspondences are fitted to an affine 2D transform, and maximum residual is checked against an explicit acceptance threshold. Both registrations map into the same stage machine-coordinate plane; slide-local coordinates are never inferred from tile order or the other slide's registration.

Carrier occupancy and registration are runtime state, separate from static equipment definition and persistent Slide identity. Each SlideRegistration has a CalibrationRevisionId. A SlideNavigationTarget captures SlideId, slide-local point and registration revision; command generation rejects a changed revision. Re-homing the stage does not restore a slide registration if the physical slide moved in its slot.

StageNavigationPlanner evaluates the per-axis accumulated error against a StageHomingPolicy expressed in micrometers. When both references are valid and within policy it returns a coordinated X/Y MoveAxesTo. If either axis has an invalid reference, unknown error bound or threshold exceedance, it returns HomeAxes for both in-plane axes while preserving the original target. After verified homing, resolveAfterHoming checks fresh position/reference evidence, revalidates the saved registration revision and generates the new absolute machine-coordinate command. The target is not stored as a pre-home machine coordinate.


### Stage position ownership and accumulated error

StageAxisPositionState is the Core per-axis runtime value for machine position. It distinguishes an unusable/unreferenced coordinate from a position usable in the current machine frame, and retains the evidence kind and ObservationSource.

Commanded movement updates the estimated coordinate only while the machine reference and starting position are valid. Its additional modeled error bound is accumulated conservatively. If either the previous or additional bound is unknown, the resulting bound remains unknown.

A ControllerReported position observation updates the reported coordinate but cannot reduce an existing open-loop error bound: controller status may report its own command coordinate rather than independently measured physical motion. EncoderMeasured or ExternalMeasured observations can replace the accumulated bound with their supplied measurement uncertainty. Such observations do not establish a machine reference by themselves; only a verified homing/reference operation does that.

The re-home policy compares the accumulated bound with a threshold in micrometers, converting to each axis's declared unit at the boundary. An invalid reference, unavailable position, unknown error bound or threshold exceedance requires homing. A homing result supplies a new referenced machine position and homing uncertainty. It does not update or repair the two slide registrations. Position estimates for a selected slide are rejected when the current stage coordinate is outside that slide's registered geometry.



### 5.5 Runtime state and notifications

Core owns operation and step state. Initial operation states:

Accepted, Running, AwaitingOperator, Verifying, Succeeded, Failed, PartiallyCompleted, CancellationRequested, Cancelled.

Initial step states:

Pending, Dispatched, AdapterAccepted, DeviceAcknowledged, Executing, AwaitingOperator, AwaitingObservation, Verifying, Verified, Failed, TimedOut, Skipped.

A device need not emit every intermediate state. Missing acknowledgement/progress must not be fabricated.

The runtime-level API is expected to expose an operation handle containing OperationId, an immutable operation snapshot/query, an asynchronous cancellation request and subscription to immutable operation events/snapshots.

Phase 1 defines only the value contracts needed for this boundary: OperationSnapshot in Core and RuntimeHandle, OperationHandle, RuntimeSnapshot and OperationView in Application. OperationSnapshot is created through a Core factory that validates its known status and positive revision; it carries MicroscopeId, RuntimeId, RuntimeGeneration and OperationId and exposes read-only accessors. Its revision starts at one and strictly increases for each published change within that exact operation identity. RuntimeSnapshot is created through an Application factory that validates its lifecycle and positive revision and exposes read-only accessors. Its revision starts at one and strictly increases for each published lifecycle change within that exact RuntimeHandle. Revisions are freshness tokens, not timestamps, and are not comparable across different operation/runtime identities. Neither counter may wrap or reuse a revision; exhaustion prevents further snapshot publication for that identity and must be surfaced as a runtime/operation fault rather than silently wrapping. OperationView contains the Core snapshot rather than duplicating status/revision fields. Its public factory checks the full composite identity and returns a typed mismatch error; inconsistent views cannot be constructed through the public API. RuntimeHandle/OperationHandle are locator values, not authorization tokens; every Application operation revalidates the handle against the active runtime generation. It does not yet implement the runtime manager, query service, cancellation path, subscription token or callback executor. Those require the Phase 4 runtime lifecycle and serialized-delivery implementation; subscription lifetime and delivery context must be defined together rather than guessed in Phase 1.

A cancellation request is not proof that hardware stopped. Late feedback cannot convert terminal failure/cancellation into success. Core reconciles physical state before a dependent operation requiring a known safe state.

### 5.6 Threading and callbacks

Each Core runtime has one serialized logical owner for state reduction.

Adapter callbacks may run on SDK/device threads. They only copy/normalize data and submit it to runtime feedback ingress; they do not call reducers, mutate Core state or invoke front ends directly.

Feedback ingress is thread-safe. Queue saturation/loss is surfaced as a lost-feedback/unknown-state condition; it cannot be silently discarded while an operation can still be marked successful. Runtime processing serializes accepted feedback and rejects stale correlation/generation.

Application subscribers receive immutable notifications on an application-controlled delivery context, never a hardware callback thread. Notification callbacks cannot re-enter Core state mutation.

Live image-frame delivery is a separate high-rate data path, not the ordinary operation-feedback channel.

## 6. Microscope.Application boundary

Application is the composition/frontend boundary, not a second microscope engine.

Initial responsibilities:
- Load versioned microscope definitions and deployment bindings.
- Ask Core to validate typed equipment definitions.
- Resolve registered adapter factories for logical endpoints.
- Claim exclusive hardware resources before runtime activation.
- Create/open/query/close/dispose Core runtimes.
- Expose domain-specific commands/queries and operation handles.
- Publish immutable runtime and operation projections.
- Coordinate persistence, workspace catalogs and slide-folder selection without transferring policy into UI.

The public API is use-case-oriented. It does not expose a generic string command executor, public service locator, raw adapter factories to front ends or mutable Core internals.

A RuntimeId is bound to one physical MicroscopeId. One physical microscope has at most one active controlling runtime in a host. A conflicting second open fails with a typed resource-conflict result.

The host registers concrete factories explicitly during composition. The registry is internal to Application composition, not a dynamic plugin platform.

A front end may hold an operation handle and query/subscribe to its state. It does not own the operation, adapter or device session.

## 7. Existing camera contract compatibility

The repository already contains Camera::Common: camera::Frame, FramePtr, camera discovery/capability values, feature descriptors/access and FrameStreamMailbox. These types remain owned by the camera capability.

Do not copy or rename them into Microscope.Core. The first Core public API must not depend on AmScope, Qt, GenTL or vendor structures.

camera::FramePtr is an immutable in-memory frame lease used by the camera pipeline. Domain ImageFrame is a persisted acquisition record with stable identity, run/tile references, capture metadata and an immutable payload reference. They are not interchangeable. The camera adapter/integration boundary maps delivered frame leases into acquisition/persistence contracts without changing the existing bounded frame pool, lease ownership, latest-frame mailbox or preview path.

A direct Core dependency on Camera::Common is not assumed. Add one only if a concrete public contract requires it and the dependency remains backend-neutral and acyclic.

## 8. Initial target/header boundary

The first Phase 1 source slice should be small:

~~~text
Libraries/
├── Microscope.Core/
│   ├── CMakeLists.txt
│   ├── include/microscope/core/
│   │   ├── ids/
│   │   ├── result/
│   │   └── runtime/
│   └── src/
└── Microscope.Application/
    ├── CMakeLists.txt
    ├── include/microscope/application/
    │   ├── runtime/
    │   └── composition/
    └── src/
~~~

This is a target boundary, not a demand to create empty directories. Add only headers/implementation units used by a real consumer.

Initial public headers cover strong identifiers, typed result/diagnostics, runtime/operation correlation, feedback ingress and serialized state boundary, plus the minimal Application runtime lifecycle/operation-handle facade.

Capability-specific command/payload headers are added only when a real capability uses them. Do not stub later equipment specifications, calibration, Sample/Slide persistence or image documents before their phase consumes them. Phase 2A now contains the first concrete optical-topology model.

## 9. Phase 1 exclusions

Do not add:
- Generic event bus or event-sourcing store.
- Generic workflow/process engine.
- Universal hardware command interface.
- General dependency-injection/service-locator framework.
- Custom dimensional-analysis library.
- JSON parser or JSON types in Core.
- Core-owned image buffer pool or replacement frame stream.
- Placeholder classes for every future subsystem.
- Qt/Swift/UI dependencies in Core or Application.
- New camera lifecycle abstraction that collapses backend differences.

## 10. Phase 1 review and acceptance gate

The Phase 1 foundation gate has passed. Its accepted evidence is:

1. Strong identifiers cannot be mixed accidentally and remain stable across storage moves.
2. Normal failures are typed; diagnostics are separate from failure.
3. CommandDispatched, AdapterAccepted, DeviceAcknowledged, DeviceActionCompleted and StepVerified are distinct facts with distinct owners.
4. Operation feedback is correlated to runtime, operation, step, command and device.
5. Adapter callbacks cannot mutate Core state directly.
6. Missing readback or lost feedback cannot produce verified success.
7. Capability ports are typed; no universal command bus exists.
8. Application owns composition/runtime lifetime but does not implement microscope policy.
9. Camera common types and the existing frame path are reused without duplication/replacement.
10. Core has no JSON, UI, operating-system or vendor SDK dependency.

**Phase 1 source exit:** satisfied by the cross-platform CI matrix. Core and Application configure/build independently and in the root platform-only composition; their registered contract tests pass on Ubuntu GCC, macOS Clang and Windows MSVC. This does not satisfy Phase 4 operation-executor/simulated-device acceptance or hardware acceptance.

## 11. Phase 2A equipment and optical-topology contract

The first equipment model is a typed Core value model, not a general-purpose graph framework.

### 11.1 Physical component identity

- Every installed physical part has one EquipmentComponentId within its microscope definition.
- EquipmentComponentKind distinguishes OpticalElement, ImagingDevice, IlluminationDevice, MechanicalAssembly and Other. It is classification data, not a behavioral subclass hierarchy.
- Specialized equipment records introduced in later Phase 2 slices (Objective, Camera, IlluminationSource, Condenser, Stage) refer to the same EquipmentComponentId. They must not create a second identity for the same physical unit.
- An EquipmentComponent owns its optical ports. OpticalPortId values are unique within one MicroscopeEquipmentDefinition.
- OpticalElement requires at least one input-capable and one output-capable port. ImagingDevice requires an input-capable port. IlluminationDevice requires an output-capable port. MechanicalAssembly has no optical ports. Other remains extensible without inferred behavior.

### 11.2 Physical connections

OpticalConnection is an undirected physical adjacency between two ports on different components. Endpoint storage order does not define light propagation direction; OpticalPathStep order does.

- A physical port participates in at most one OpticalConnection. A component that branches light exposes separate physical ports for its branches.
- A connection is indexed in both endpoint orders. One physical adjacency can therefore support multiple ordered paths that traverse it in opposite directions, provided each path step's entry/exit port capabilities allow that traversal.
- Port Input/Output/Bidirectional capability constrains use in an ordered path; it does not define which endpoint of the physical connection is first.
- Internal passage through an optical component is not represented as a self-connection. It is represented by a path step pairing that component's entry and exit ports.

### 11.3 Ordered paths and shared hardware

An OpticalPath has a stable OpticalPathId and an ordered sequence of OpticalPathStep values. Each step references one EquipmentComponentId and its entry/exit ports.

- The first step may omit its entry port; every later step requires one.
- The last step may omit its exit port; every earlier step requires one.
- Where two steps are consecutive, the previous exit and next entry must be joined by the corresponding directed OpticalConnection.
- Entry/exit ports must belong to the component named by their step and have compatible directions.
- Shared optical elements are stored once in the component collection. Multiple paths reference the same EquipmentComponentId; a beam splitter uses different output ports for different branches.
- This representation is equipment topology and ordered light propagation, not an editor graph, command graph, configuration profile or executable behavior model.

### 11.4 Aggregate validation

MicroscopeEquipmentDefinition is the immutable validation boundary for one microscope's installed component, connection, path and specialized-specification collections. Its factory rejects an empty component collection, duplicate component/port/connection/path identities, foreign connection/path references, invalid port ownership/direction, a port connected more than once, same-component physical connections, incomplete interior path steps and missing route connections. It also validates all Objective/Camera/Condenser/Illumination/Stage specification references and component kinds, rejects duplicate specialization records, and rejects one OpticalElement being declared as both an objective and a condenser.

JSON parsing/schema migration remains in Microscope.Application (Phase 5). The Core aggregate accepts typed values and validates domain relationships; it does not parse JSON, access storage or discover hardware.

## 12. Phase 2B objective and camera specification contract

### 12.1 Objective specification

ObjectiveSpecification is attached to the installed EquipmentComponentId. It does not mint a separate objective identity and does not add per-objective behavior classes.

The initial specification stores:
- Positive nominal magnification.
- Positive numerical aperture.
- Positive working distance in micrometers.
- Chromatic correction classification (achromatic, semi-apochromatic, apochromatic, other or unknown).
- Field correction classification (plan, non-plan or unknown).
- Immersion medium (air, water, oil, glycerol, silicone, other or unknown).
- Optional positive design coverglass thickness in micrometers.

Magnification, numerical aperture and lengths are distinct domain-specific numeric types. Values must be finite and positive. Numerical aperture is not capped at 1.0; values above one are valid for high-NA immersion objectives. The specification records known/unknown characteristics without pretending a missing specification is zero.

### 12.2 Camera sensor and output modes

CameraSpecification is attached to the installed imaging EquipmentComponentId. It stores:
- Active sensor width/height in sensor pixels.
- Independent positive X/Y sensor pixel pitch in micrometers.
- One or more unique CameraOutputMode values.

A CameraOutputMode stores its mode ID, sensor ROI origin and extents, horizontal/vertical binning factors, delivered image dimensions, pixel layout and stable pixel-format code.

The model distinguishes:
- Sensor coordinates and ROI dimensions.
- Hardware binning factors.
- Delivered image dimensions.

The effective geometric mapping is explicit:

    sensorPixelsPerDeliveredPixelX = ROI_width / delivered_width
    sensorPixelsPerDeliveredPixelY = ROI_height / delivered_height

X and Y ratios are independent. This captures the net mapping for binning, decimation, crop and resampling; it does not infer that the physical binning factor alone explains every output dimension. Sensor pixel pitch is also independent in X and Y. Nominal object-space scale can therefore use the corresponding axis-specific pitch and mapping before dividing by effective optical magnification in the calibration/resolution phase.

The active sensor ROI must be contained within the declared active sensor dimensions. Output mode IDs must be unique per camera. Pixel dimensions and binning factors are nonzero. Pixel format codes are non-empty, NUL-free and whitespace-free.

### 12.3 Aggregate ownership and validation

MicroscopeEquipmentDefinition optionally owns objective and camera specification records alongside physical components, connections and paths. Each specialized record must reference an installed component of the correct broad kind:
- ObjectiveSpecification → an installed OpticalElement.
- CameraSpecification → an installed ImagingDevice.

At most one objective specification, camera specification, condenser specification, illumination specification and stage specification may be attached to a physical component in the corresponding role. An objective and condenser are mutually exclusive roles for one OpticalElement. A specification referencing an uninstalled component or a component of the wrong broad kind is rejected. This is data association, not an executable component subclass hierarchy.

These are equipment-definition facts only. Actual camera capabilities, active output mode, current exposure, gain, online/offline status and readback observations remain runtime/adapter data. JSON loading remains an Application responsibility in Phase 5.

## 13. Phase 2C illumination and condenser specification contract

### 13.1 Illumination source and channels

IlluminationSpecification is attached to the installed IlluminationDevice EquipmentComponentId. One physical source may expose multiple independently identified IlluminationChannel values.

A source specification stores a technology classification (LED, halogen, xenon arc, mercury arc, laser, other or unknown) and at least one channel. Each channel stores:
- Stable IlluminationChannelId, unique within the source.
- Optional SpectralBand with positive minimum and maximum wavelengths in nanometers; absent means that the static definition does not specify a band.
- IntensityControlKind: Fixed, Continuous, DeviceDefined or Unknown.
- For Continuous control, a required IlluminationIntensityRange with minimum/maximum and an explicit unit (percent, milliamperes, milliwatts, relative or device-defined).

A continuous range must contain finite, non-negative values with maximum greater than minimum. Units are part of the record; values from different units are not compared. A non-continuous control kind cannot carry a continuous range. A continuous channel cannot omit its range.

This model supports multi-channel sources without assuming that every channel shares the same spectrum or control range. Actual online status, selected channel, current intensity, device readback and adapter-specific command semantics remain runtime data.

### 13.2 Condenser specification

CondenserSpecification is attached to an installed OpticalElement component. It stores:
- Minimum and maximum aperture numerical aperture.
- Design classification (Abbe, achromatic, achromat-aplanatic, other or unknown).
- Immersion medium (air, water, oil, other or unknown).
- Optional working distance in micrometers.

The minimum aperture NA is non-negative (zero is valid for a fully closed aperture); the maximum is positive. Minimum cannot exceed maximum. The specification does not select a phase ring, DIC prism, polarizer, analyzer or illumination state. Those are independent installed components and their compatibility/selection belongs to Phase 3 declarative configuration.

### 13.3 Aggregate binding

MicroscopeEquipmentDefinition may carry condenser and illumination specifications. A condenser specification must reference an installed OpticalElement; an illumination specification must reference an installed IlluminationDevice. Each component has at most one record of each corresponding specialization. Unknown component references and kind mismatches are rejected.

No current control state, observed aperture position, measured brightness or device discovery is stored in the equipment definition. JSON loading remains an Application responsibility in Phase 5.

## 14. Phase 2D stage and axis specification contract

### 14.1 Stage and axis identity

StageSpecification is attached to one installed MechanicalAssembly EquipmentComponentId. A stage contains one or more AxisSpecification records. Each axis has a StageAxisId unique within that stage; the stage's installed EquipmentComponentId provides the enclosing physical identity.

AxisMotionType distinguishes Linear, Rotary, Other and Unknown. AxisCoordinateUnit distinguishes micrometers, millimeters, degrees, radians, device-defined and unknown. Known linear axes accept linear units; known rotary axes accept angular units. Device-defined/unknown values are preserved without false unit inference.

### 14.2 Operating range and performance

Every axis declares an increasing finite operating range and a positive nominal resolution. The range may be signed, which is necessary for centered/travel axes and rotary axes.

The following metrics are independent:
- Nominal resolution: command/readout increment.
- Accuracy: optional maximum absolute error, with test-condition description.
- Repeatability/precision: optional value, explicit metric (maximum deviation, standard deviation, RMS or unknown) and test-condition description.
- Measurement uncertainty: optional value with test-condition description.

All numerical values use the axis's declared coordinate unit. Non-finite values are rejected; range and resolution invariants are validated. Accuracy is never inferred from resolution or calculated by multiplying component specifications. If accuracy, repeatability or uncertainty is not established, the field remains absent.

### 14.3 Position feedback capability and location

AxisPositionFeedbackCapability describes the installed measurement capability: none, encoder, linear scale, device-reported value, external sensor, other or unknown. Optional AxisMeasurementLocation describes where the measurement is made: motor, axis, specimen, external, other or unknown.

A no-feedback axis cannot claim a measurement location. A feedback capability is not proof that the current position is known, calibrated or accurate. Current positions, commands, readbacks, calibrated estimates and unknown state belong to runtime observations and calibration behavior, not static equipment data.

The StageSpecification records capability and declared performance only. It contains no mechanical transmission design details.

## 15. Phase 3A declarative configuration profile and resolver contract

### 15.1 Configuration profile

ConfigurationProfile is immutable, identified by ConfigurationProfileId and contains declarative compatibility data:
- One known ContrastMethod.
- Non-empty allowed sets for objective component IDs, condenser component IDs, optical path IDs, camera component IDs and illumination component IDs.
- Optional required path component IDs, all of which must occur on at least one selected path.

Profile catalogs reject duplicate profile IDs. Profile data contains identities and constraints only. It contains no C++ class names, SDK calls, executable callbacks or UI-specific values.

### 15.2 Configuration request

ConfigurationRequest is an immutable value that selects:
- Profile and contrast method.
- One installed objective component and one installed condenser component.
- One or more unique installed OpticalPathIds.
- One installed camera component and one output CameraModeId.
- One installed illumination component and one IlluminationChannelId.
- An explicit ExposurePolicy: Manual, Automatic or ConstrainedAutomatic.

A request with no path or duplicate paths is invalid. Unknown contrast or exposure-policy enum values are rejected. Unknown is not treated as a user-selected exposure policy.

### 15.3 Pure resolution semantics

ConfigurationResolver receives only typed Core values: MicroscopeEquipmentDefinition, ConfigurationProfileCatalog and ConfigurationRequest. It has no hardware, storage, JSON, UI, clock or mutable runtime dependency.

The resolver checks:
- The requested profile exists and the contrast method matches.
- Each selected component/path identity is allowed by the profile.
- Each selected path exists in the installed equipment definition.
- Objective, condenser, camera and illumination specifications exist for the selected installed components.
- The selected CameraModeId exists on the selected camera.
- The selected IlluminationChannelId exists on the selected illumination source.
- Objective, condenser, camera and illumination components occur on at least one of the selected paths.
- Every profile-required path component occurs on at least one selected path.

Multiple paths are intentional. For example, illumination/condenser can be on one selected branch while objective/camera are on another. The resolver checks component membership against the union of the selected paths; it does not require every selected component to be present on every path.

The result is either a microscope-scoped immutable ResolvedConfiguration or a set of stable, typed Error diagnostics. A successful result means only that the requested target selection is compatible with the profile and installed static equipment definition. It does not mean that any component is currently selected, that any hardware command was sent, or that device state was verified.

### 15.4 Separation from later configuration phases

The resolver does not compare with current hardware/runtime state and does not generate commands. Transition planning in Phase 3D may compare this desired value with an explicitly supplied verified effective-configuration snapshot and produce a value dependency plan. Unknown or merely observed state is not a verified effective configuration. Execution, feedback, timeouts, cancellation and recovery remain Phase 4.

ExposurePolicy is distinct from ExposurePreset. A profile may provide an initial exposure preset while the request independently selects Manual, Automatic or ConstrainedAutomatic. Retaining the preset in the resolved desired value does not itself issue a command or override the request policy.

JSON schema parsing and profile loading remain in Microscope.Application in Phase 5. The Core resolver receives already-constructed typed profile and equipment values.

### 15.5 Contrast-specific optical function requirements

Installed OpticalElement components may declare one or more OpticalFunctionCapability values:
- PhasePlate, PhaseAnnulus.
- Polarizer, Analyzer, DifferentialInterferencePrism.
- ExcitationFilter, EmissionFilter, DichroicMirror.
- DarkfieldStop or Other.

A capability may carry an OpticalCompatibilityId. This is a stable matching identity, not a component class or executable rule. It supports exact pairing such as a phase plate and matching condenser phase annulus. A component cannot declare the same function/compatibility pair more than once. Non-OpticalElement components cannot declare optical functions.

A ConfigurationProfile may declare OpticalFunctionRequirement values. Each requirement identifies a function, optional required compatibility identity and minimum number of distinct selected components. Requirements with the same function and compatibility identity are duplicates and rejected.

ConfigurationRequest may include selected additional optical component IDs. They must be unique and must not duplicate the primary objective or condenser selections. The resolver verifies that each selected additional component:
- Exists in the installed definition.
- Is an OpticalElement.
- Occurs on at least one selected optical path.
- Satisfies at least one optical-function requirement in the selected profile.

The resolver evaluates profile requirements across the set of selected optical elements consisting of the selected objective, selected condenser and selected additional optical components. Each physical component counts at most once toward a given requirement, even if it is referenced through several selected paths. Optional compatibility IDs must match exactly.

Examples:
- Phase contrast can require one PhasePlate and one PhaseAnnulus with the same OpticalCompatibilityId. The objective and condenser selections may satisfy these requirements directly.
- DIC can require a Polarizer, an Analyzer and at least two DifferentialInterferencePrism components sharing a DIC compatibility identity. The request explicitly selects the additional components.
- Fluorescence profiles can similarly require matching excitation/emission filters and dichroic mirror components.

The resolver returns the diagnostic code configuration.optical_function.requirement_not_satisfied when a required function/count is not met. It rejects additional component selections that are not allowed by any optical-function requirement. These checks remain pure data validation; no component selection command is issued.

### 15.6 Profile targets and microscope-scoped desired configuration

ConfigurationProfile may supply three optional target values:
- Condenser aperture target as a non-negative numerical aperture.
- Illumination intensity target as a finite non-negative value paired with an explicit IlluminationIntensityUnit.
- Initial ExposurePreset as a positive exposure time in microseconds.

The resolver validates each supplied target against the selected installed device specification:
- Condenser aperture target must be within the selected CondenserSpecification minimum/maximum aperture NA.
- Illumination intensity target requires a selected channel with Continuous control and a declared intensity range. The target unit must exactly match the channel range unit, and its value must lie inside the range.
- ExposurePreset requires the selected CameraSpecification to declare an ExposureTimeRange, and the preset time must lie inside the inclusive range.

Missing capability data is not treated as an unlimited range. The resolver emits a stable Error diagnostic and does not return a resolved value when a supplied target cannot be validated.

A successful result is ResolvedConfiguration, which carries:
- The exact MicroscopeId against which resolution was performed.
- The immutable ConfigurationRequest, including its independent ExposurePolicy.
- The optional condenser aperture target.
- The optional illumination intensity target and unit.
- The optional initial exposure preset.

The profile target values are desired configuration data, not current values, observations, effective state or commands. The presence of an ExposurePreset does not override Manual/Automatic/ConstrainedAutomatic policy. Phase 3D describes the transition requirements; Phase 4 decides execution and completion using adapter feedback.

### 15.7 Transition-plan and dependency-DAG contract

The transition planner consumes only a successful ResolvedConfiguration. It is a pure Core transformation and returns a microscope-scoped ConfigurationTransitionPlan. It does not accept current hardware observations, infer effective state, issue commands or mutate runtime state.

Each ConfigurationTransitionStep has a strong ConfigurationTransitionStepId and one typed ConfigurationTransitionRequirement. The current requirement vocabulary is:
- AcquisitionMustBeInactive.
- OpticalPathSetMustMatch.
- ObjectiveSelectionMustMatch and CondenserSelectionMustMatch.
- AdditionalOpticalComponentSelectionMustMatch for each explicitly selected additional optical element.
- CameraOutputModeMustMatch and IlluminationChannelMustMatch.
- Optional CondenserApertureMustMatch and IlluminationIntensityMustMatch targets.
- ExposurePolicyMustBeHonored.
- Optional InitialExposurePresetMustBeConsidered.
- DesiredConfigurationMustBeVerified.

ExposurePolicyMustBeHonored is always separate from InitialExposurePresetMustBeConsidered. The policy requirement is present even when the profile has no initial preset. When a preset exists, it is considered only after camera output mode and exposure policy prerequisites. The planner does not silently convert Manual, Automatic or ConstrainedAutomatic into a hardware action.

ConfigurationTransitionDependency is a directed edge from prerequisiteStepId to dependentStepId. ConfigurationTransitionPlan validates:
- Unique step IDs and exactly one acquisition-inactive precondition and final-verification requirement.
- Every dependency references existing steps and is neither self-referential nor duplicated.
- The dependency graph is acyclic.
- AcquisitionMustBeInactive is a direct prerequisite of every other step.
- Every other step is a direct prerequisite of DesiredConfigurationMustBeVerified.

The plan computes deterministic topological execution layers. Step IDs within a ready layer are ordered deterministically. A shared layer means the requirements have no declared dependency edge between them; it does not authorize concurrent device access. Phase 4 runtime owns resource arbitration, adapter concurrency and actual execution policy.

The planner emits the full desired requirement set conservatively. It does not omit a requirement based on an observation or an assumed default. Phase 4 may reconcile the plan against a verified effective-configuration snapshot; unknown or unverified observations are not an effective state. No plan node is a vendor SDK command, and a plan's presence does not mean any requirement was applied or verified.

