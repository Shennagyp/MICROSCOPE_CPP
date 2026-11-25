# OPTIKA B293 Dual-Slide Application Architecture

**Status:** Target architecture for the `OPTIKA-B293-DUALSLIDE-APP` branch  
**Baseline:** `f25e5d8e6fdefea7f742fb71fa5f4ead69291626`  
**Product:** OPTIKA B293 Dual-Slide Microscope Workstation  
**Scope:** Application architecture, UI architecture, capability boundaries and migration rules

---

## 1. Product boundary

The product is a **microscope workstation**, not a camera application.

The camera is one hardware capability of the workstation. It must not own the application, the workspace, the slide model, image processing, microscope configuration, measurements, motorization or future laboratory workflows.

The application is expected to coordinate independent capabilities, initially:

- Camera acquisition and camera configuration.
- Two-slide preview and image presentation.
- Microscope optics and physical-scale calibration.
- Image processing, including future cell detection and segmentation.
- Motorized stage or other microscope motion.
- Capture, annotation, measurement and persistence.
- Future instrument and workflow capabilities without restructuring the camera subsystem.

The application may initially support one camera stream routed to one of two slide destinations. The two slide panes are separate presentation documents: each can retain its own last image, freeze state, view transform and overlays. This does **not** imply two cameras or two simultaneous acquisition streams.

The current v0.8 native camera application is a transitional implementation and migration source. It is not the target ownership model. It remains available on its existing branch and must not be deleted or destabilized as part of establishing this branch.

## 2. Architectural objective

Reduce incidental complexity and make behavior easier to understand, test and extend. The objective is not to minimize source lines mechanically. It is to reduce the number of places where a behavior is defined, the amount of state that must be understood together, and the number of components that must change for one feature.

The target should make these statements true:

1. A camera driver does not know that an OPTIKA B293 application exists.
2. A motor controller does not depend on a camera implementation.
3. Image-processing algorithms do not depend on SwiftUI, AppKit, camera vendor SDKs or UI state.
4. A view renders state and emits user intent; it does not implement hardware policy.
5. Domain calculations are ordinary deterministic functions wherever possible.
6. Side effects have explicit owners and lifecycles.
7. The application composition root decides which implementations are connected.
8. The same capability can be replaced or tested without rewriting unrelated capabilities.

## 3. Top-level repository boundaries

The repository should evolve toward sibling capabilities and an application host:

```text
/
├── Application/
│   └── OptikaB293/
│       ├── CMakeLists.txt
│       ├── Sources/
│       │   ├── App/                 # Entry point and application lifecycle
│       │   ├── Composition/         # Dependency construction and wiring
│       │   ├── Features/            # Product workflows and use cases
│       │   ├── Presentation/        # SwiftUI and AppKit presentation
│       │   ├── Adapters/            # UI/application adapters to capabilities
│       │   └── Persistence/         # Application-owned saved settings and records
│       └── Tests/
│
├── Camera/
│   ├── Common/                      # Vendor-neutral camera contracts and frame types
│   ├── Amscope/                     # AmScope SDK-backed implementation
│   ├── Qt/                          # Optional Qt adapter
│   ├── QtWidgets/                   # Optional Qt preview widgets
│   └── VendorSDK/
│
├── Motion/                          # Future motor/stage capability
├── ImageProcessing/                 # Future image-processing capability
├── Optics/                           # Optical specifications and calibration primitives
├── docs/
└── scripts/
```

This is the intended ownership direction, not a demand to create empty directories or speculative frameworks immediately. A capability is introduced when it has a real contract and an implementation or a concrete consumer.

### 3.1 Ownership rules

| Concern | Owner |
|---|---|
| Native camera session, vendor handle, acquisition worker | `Camera/Amscope` |
| Vendor-neutral camera types, descriptors and frame contracts | `Camera/Common` |
| Camera-to-application adaptation | `Application/OptikaB293/Adapters/Camera` |
| Application lifecycle and capability composition | `Application/OptikaB293/App` and `Composition` |
| Slide workflow and active live destination | Application feature/workspace state |
| Slide image, view transform, freeze and overlays | Slide presentation/document feature |
| Microscope/objective specifications and optical calculations | `Optics` or a focused application domain module until reuse is real |
| Stage axes, position, movement and homing | `Motion` |
| Image transforms, segmentation and detection | `ImageProcessing` |
| UI layout and visual interaction | `Presentation` |
| User preferences and application records | Application persistence |

A layer may depend inward on contracts. A capability must not depend outward on the product UI. In particular, `Camera` must never import or link against `Application/OptikaB293`.

## 4. Dependency direction

The intended dependency graph is:

```text
                     Application composition root
                                |
                +---------------+----------------+
                |               |                |
          Camera adapter   Motion adapter   Processing adapter
                |               |                |
          Camera contracts  Motion contracts  Processing contracts
                |               |                |
          AmScope driver    Stage driver      Algorithm implementations

Presentation -> application state/actions -> application use cases
                                      |
                                      v
                               capability ports
```

More precisely:

- Presentation depends on application-facing state and action contracts.
- Application workflows depend on capability interfaces and domain values.
- Infrastructure adapters implement those interfaces.
- Capability implementations depend on their own contracts and required platform/vendor SDKs.
- The composition root is the only place that knows the concrete implementations of all capabilities.

No dependency should point from Camera, Motion or ImageProcessing into the application or presentation layer.

Do not add a generic plugin system, service locator, message bus, dependency container framework or universal device abstraction merely to satisfy this diagram. Plain constructors and explicit protocols/interfaces are sufficient until actual scale requires more.

## 5. Functional programming with object-oriented boundaries

The target combines functional programming principles with object-oriented lifecycle management. These are complementary tools, not competing architectural styles.

### 5.1 Functional core

Prefer pure functions for behavior that can be expressed as values:

- Optical calibration and unit conversion.
- Automatic resolution policy (pure optics policy coordinated at the application boundary).
- Image coordinate and viewport transformations.
- Measurement geometry and specimen-plane distance calculation.
- Segmentation post-processing and object filtering.
- Stage coordinate transforms and motion-limit calculations.
- Validation, formatting and deterministic state transitions.

A pure function takes explicit inputs and returns an explicit result. It does not read global state, access hardware, mutate a shared model, write preferences or schedule UI work.

Example:

```swift
struct ResolutionCandidate {
    let mode: CameraReadoutMode
    let sampling: SamplingAssessment
}

func chooseResolution(
    from candidates: [ResolutionCandidate],
    target: SamplingTarget
) -> ResolutionDecision
```

The example expresses a boundary, not a commitment to these exact types. The policy must use verified readout semantics; output width and height alone must not be treated as proof of sensor sampling behavior.

### 5.2 Imperative shell and OOP

Use objects where identity, resource ownership or lifecycle is real:

- Camera session and acquisition controller.
- Motor/stage connection and motion controller.
- Frame source and frame consumer.
- File store and capture writer.
- Application coordinator and capability adapters.
- AppKit view objects that own native rendering resources.

These objects encapsulate side effects and resource lifecycles. They should delegate calculations and policy to pure functions instead of accumulating business rules.

Use protocols at genuine replacement or testing boundaries: camera source, stage controller, processing engine, settings store. Avoid one protocol for every concrete type.

### 5.3 State and event semantics

Each state value must have one clear owner and lifecycle. Prefer immutable value types for snapshots and explicit commands for mutations.

A useful feature transition shape is:

```text
(previous state, user intent) -> (next state, requested effects)
```

The transition should be deterministic. The effect executor performs camera, stage, file or other side effects and returns results as new events.

Do not create a universal Redux clone. A small reducer or pure transition function is appropriate where it simplifies a real state machine. A normal object with a few explicit methods is better where there is no useful reducer model.

Avoid:
- A single giant ObservableObject containing camera, dock, microscope, image, stage and processing state.
- UI views directly invoking vendor APIs.
- Hidden global singletons as dependencies.
- Duplicating a business rule in a view, view model and hardware adapter.
- Mutable shared state where immutable snapshots suffice.
- Excessive wrappers and one-line forwarding layers that add no policy or ownership.

## 6. Application state is not hardware state

The product workspace, a slide document and a camera session are different things.

### 6.1 Camera session and generic camera features

The camera session owns the native camera handle, connection state and acquisition
lifecycle. Application UI observes a projection of that state. It does not own the
native camera handle or issue lifecycle operations directly. The concrete
Objective-C++ bridge is private to `NativeCameraModel`; presentation receives only
a `NativeCameraFrameFeed` contract for frame notifications and one
`NativeCameraFrameSnapshot` containing the newest frame lease plus acquisition
counters. The adapter call is made once per notification. It reads the counters
and atomically transfers the existing frame lease; the value snapshot is stack
data and does not copy pixel bytes or create storage. The Core Graphics provider
retains that lease while referencing the original frame bytes. RGB and gray
device color spaces are cached for the process lifetime; the provider and
`CGImage` remain frame-specific. There is no intermediate `NSData` or pixel-buffer
copy, second frame queue, per-frame Task or additional dispatch. Both panes
receive the same counter snapshot. The latest-frame mailbox moves the acquired
`FramePtr` into its atomic slot and retains only the compare token needed to
reject stale-generation cleanup, avoiding an unnecessary shared-pointer copy.

`NativeCameraModel` owns session state: device selection, camera state,
session identity, operation-pending state and Open/Start/Stop/Close commands.

`NativeCameraFeatureModel` owns the generic feature snapshot and editor state:
descriptors, readbacks, drafts, feature errors, snapshot/write pending flags and
typed feature writes. It borrows the same bridge and receives only a read-only
`NativeCameraFeatureAccessSnapshot` from the session model. It cannot open,
start, stop or close a camera.

The session model owns the feature model and supplies its session-access snapshot
through a weak provider. Session lifecycle callbacks notify the feature model when
operations complete and reconcile the snapshot against the resulting camera state.
The application delegate requests termination close through `NativeCameraModel`;
it does not invoke the bridge directly. The session model keeps a termination gate
active while a close is queued behind an already accepted command, preventing
intervening UI commands or feature reads. The special preview-resolution mutation
also remains in the session model because the native controller may stop,
reconfigure and restart acquisition to apply it.
Generic feature state is not duplicated in the session model.

The generic feature editor excludes `AmcamResolutionIndex`. Applying that mode may
stop and restart acquisition, so it is exposed through the session owner's
acquisition-aware preview-resolution command only; the feature model also rejects
that name at its generic write boundary.

The native controller advances the feature-state generation when a feature
mutation is enqueued. Earlier queued or in-flight feature reads are then
discarded, even when the mutation ultimately fails. Therefore, every write
completion that still belongs to the active session triggers a fresh descriptor
and readback snapshot. A failed write is reported and its draft is restored, but
the rest of the feature view is also resynchronized rather than left showing
permanent "Reading…" placeholders.

Lifecycle commands advance the native session generation when they are enqueued.
This invalidates prior-session feature reads even when Start, Stop or Close fails.
After every lifecycle completion, `NativeCameraModel` refreshes feature state
when the resulting camera state still permits access; otherwise it clears the
feature snapshot. A new session also starts with an empty feature snapshot, so
descriptors and values cannot leak across camera sessions. Descriptor and read
callbacks also recheck the read-only access snapshot when delivered. This
prevents a late callback from repopulating feature data after an asynchronous
acquisition fault has revoked access without changing the session generation.

### 6.2 Slide documents

Each slide pane owns presentation/document state such as:

- Last displayed source image or retained immutable image reference.
- Freeze/hold state.
- Image-space measurements and annotations.
- Zoom, pan, fit/locked/1:1 mode.
- Display metadata associated with that image.

The slide document must not own the physical camera session.

### 6.3 Live-feed routing

The application workspace owns the selected destination for the live feed. The frame router sends each accepted frame to that destination. Inactive panes retain their previous image. Frozen panes continue to respect the existing acquisition/frame-pool contract; presentation decisions must not create a second acquisition queue or an unbounded image history.

The active destination is application routing state, not a camera property.

### 6.4 Microscope configuration

`MicroscopeConfigurationModel` owns the active `MicroscopeOpticalConfiguration`,
its persisted representation, the editable draft store and the derived generic
measurement calibration. It is not inferred from the camera brand. Camera sensor
specifications, output/readout behavior, objective magnification, numerical
aperture and relay/reducer magnification must be represented explicitly, with
provenance and validation.

An unknown calibration must remain unknown. The application must not silently
display a precise physical measurement derived from guessed hardware assumptions.

### 6.5 Automatic resolution coordination

Automatic resolution is an optics-level application workflow, not a camera feature.
Its ownership is split deliberately:

- `NativeAutomaticResolutionPolicy` is a deterministic function over microscope
  optical configuration and camera-reported readout-mode symbols.
- `MicroscopeResolutionCoordinator` owns the automatic-selection preference,
  presents the policy summary and reconciles changes when microscope configuration
  changes or the camera reports a new resolution readback.
- `NativeCameraFeatureModel` publishes generic camera feature readbacks; the
  session owner forwards the selected resolution readback event to the coordinator.
- `NativeCameraModel` performs the acquisition-aware preview-resolution mutation.
  It does not know about objectives,
  numerical aperture, stain wavelength bands or Rayleigh sampling.

The coordinator is constructed at the application composition root and receives
both state owners explicitly. The camera publishes a resolution-readback
notification; the microscope model publishes configuration changes. The
coordinator responds by evaluating the pure policy and requesting a generic
camera resolution mutation only when the selected mode differs from the readback.

The current readout-mode-to-effective-pixel interpretation in the policy remains
provisional until the selected camera's mode semantics are measured and
documented. A passing pure-policy test does not establish hardware truth.

## 7. UI architecture

The UI will be refactored, not merely moved to another directory.

### 7.1 Presentation rules

- SwiftUI describes composition, controls and low-frequency application state.
- AppKit remains appropriate for the high-rate image presentation surface and native interaction where it provides a real advantage.
- Views are small compositions of focused subviews.
- A view receives the state it needs and emits intent through explicit actions.
- Formatting and layout decisions are derived from state, not stored as duplicated mutable state.
- Views do not discover hardware, implement optical formulas, choose camera readout modes or directly persist application configuration.
- Reusable controls should be shared where behavior and semantics are actually the same, not merely because two controls look similar.

### 7.2 Preview performance is a hard boundary

The preview pipeline is already deliberately designed to avoid per-frame SwiftUI observation. Preserve these invariants throughout UI refactoring:

1. One owner of the native camera session.
2. One acquisition worker and one SDK image-pull path.
3. Immutable frame lease ownership.
4. Bounded event transfer and latest-frame-wins presentation.
5. One latest-frame mailbox and one presentation consumer, accessed by presentation through `NativeCameraFrameFeed`.
6. No unbounded main-queue work per frame.
7. No per-frame SwiftUI state publication.
8. No extra frame copies, image histories or retained leases introduced by pane composition.
9. No camera I/O blocking the UI thread.
10. Session-generation checks continue to reject stale frames.
11. Same-size live frames replace the retained `CGImage` and invalidate the image
    surface without recomputing document geometry or re-publishing unchanged
    overlay arrays. Geometry is recomputed only when dimensions, viewport, backing
    scale or user-controlled transform changes.
12. Frozen destinations still consume and release the newest frame lease, but do
    not create a `CGImage` that will be discarded. Empty overlay sets bypass
    overlay-coordinate calculations and graphics-state setup.
13. The preview exposes one FPS metric: PULL FPS, derived from the monotonic
    successful SDK-pull counter. It is acquisition pull throughput, not a
    displayed-frame rate or sensor-reported FPS value. Its tooltip is configured
    once; the label changes only when the derived pull rate changes. The periodic
    refresh only expires stale rate values and never calls the SDK.
14. Immutable frame leases cross the mailbox and Core Graphics boundary without
    copying pixel bytes. Frame-specific providers retain the source lease for the
    lifetime of the image; only small immutable color-space objects are shared.
15. The Objective-C++ lease wrapper stores its FramePtr inline; do not reintroduce
    a separately allocated holder for each displayed frame.

A UI simplification is invalid if it weakens any of these invariants.

### 7.3 Acquisition buffer and lease lifecycle

The AmScope live-preview path uses a bounded pool created when acquisition starts,
after the SDK reports the final preview dimensions and selected pixel layout.
The current pool contains four preallocated AmcamFrame slots plus one private
discard buffer. Each slot's pixel storage is allocated before the acquisition
worker begins pulling images.

For every image event, the acquisition worker takes a slot only when the pool is
its sole shared_ptr owner. Amcam_PullImageV4 writes directly into that slot's
pixel storage; the resulting immutable FramePtr is transferred to the frame
callback and then to the latest-frame mailbox. The Core Graphics provider keeps
the frame lease alive while it reads those same bytes. The preview path therefore
does not allocate a new pixel buffer or copy the pixel payload for each frame.

When all four slots are still leased, the worker still drains the SDK by pulling
the image into the private discard buffer and increments the dropped-frame
counter. It must not wait for the UI, mutate a retained frame, or add an
unbounded queue. This is deliberate latest-frame behavior: bounded memory and
continued acquisition take precedence over preserving every preview frame.

The Objective-C++ AmscopeFrameLease stores its FramePtr directly as a private
C++ instance variable. Its lifetime is extended by the Core Graphics data
provider, and ARC/Clang destroys the C++ member with the Objective-C object.
There is no separate heap allocation for a FramePtr holder per displayed
frame. This changes only lease bookkeeping; it does not change pixel storage,
provider ownership, or frame delivery.

The per-frame Core Graphics data provider and CGImage remain frame-specific.
The provider retains the immutable lease, and the view may retain an older image
while acquisition advances to newer slots. Reusing or mutating those objects
would break the image-to-buffer lifetime guarantee; pooling them would require
copying pixels into separate stable storage, which defeats the zero-copy path.

The vendor callback-to-worker handoff is a fixed-capacity 256-entry ring backed
by std::array. Enqueueing image notifications does not allocate a queue node
per frame. Overflow is explicit and faults acquisition rather than silently
dropping callbacks or allowing the event queue to grow without a bound.

Still capture is a separate, explicit one-shot path. It submits Amcam_SnapN
or Amcam_SnapR, waits for the still-image event, queries the still dimensions,
allocates one dedicated AmcamFrame, and pulls the still payload into that
allocation. It does not borrow or resize a live-preview pool slot. The live
acquisition worker remains active during the wait; vendor API calls are
serialized by the camera API mutex. The one-shot allocation is expected capture
work and must not be moved into the per-frame preview path.

The current application gallery button is intentionally a different operation:
it saves the active slide pane's already displayed CGImage. It does not issue a
new SDK still-capture request or change the camera's readout/resolution. The
captured PNG therefore represents the current preview frame and its dimensions
and pixel format, not a separately acquired high-resolution still. The backend
AmcamCamera::captureStill API is not the route used by this gallery button.

Capture runs on a background worker and is limited to one pending save. Before
PNG compression and disk I/O, the worker copies the CGImage provider data into
independent storage and releases the source CGImage. This one explicit
capture-time copy is deliberate: it returns the live frame-pool slot promptly
instead of retaining a camera lease during potentially slow encoding or file
I/O. It does not add work to the continuous acquisition or preview path. A
thumbnail-generation failure after successful PNG finalization must preserve
the saved PNG; the gallery may report the thumbnail error, but it must not
delete the user's capture.

### 7.4 Presentation decomposition

The two-slide viewer should be composed from focused units with explicit boundaries:

- Workspace composition: placement of panes, rail and tool overlays.
- Dual-slide coordinator: owns the single frame consumer and routes frames.
- Slide pane: pane-level controls and presentation status.
- Image document view: image display, scale, pan, freeze and 1:1.
- Overlay model/renderer: measurements and annotations in source-image coordinates.
- Calibration projection: converts source-pixel distance to physical units using a stable calibration snapshot.

Do not split code mechanically into many tiny files. Each extraction must establish a meaningful ownership boundary or make a pure policy independently testable.

## 8. Capability boundaries

### 8.1 Camera

Camera owns discovery, connection, vendor-specific controls, acquisition, frame transport and camera metadata. It does not own motorization, image segmentation, microscope workflow, slide identity or product UI.

The generic camera frame contract should remain vendor-neutral. Algorithms should not require AmScope-specific types.

### 8.2 Motion

Motion will own stage axes, position representation, movement commands, velocity/acceleration constraints, homing, limit handling, connection lifecycle and motion feedback.

It must not be embedded in the camera driver or camera UI. Motion and camera may be coordinated by an application workflow, but neither capability owns the other.

### 8.3 Image processing

Image processing will expose functional operations over explicit image/frame inputs and typed parameters/results. It must not depend on SwiftUI/AppKit or camera acquisition lifecycle.

The future blood-film and bone-marrow workflow is individual-cell detection, accurate segmentation and position recording at low/moderate magnification. It is not high-magnification whole-slide imaging, whole-slide scanning or stitching.

Processing results should preserve a traceable relationship to the source image, pixel coordinate system, calibration context and algorithm/configuration used.

### 8.4 Optics and calibration

Optical calculations must distinguish sensor pixel pitch, output pixel mapping, objective magnification, relay magnification and numerical aperture. Cropping, binning, subsampling and resizing are not interchangeable transformations.

Physical measurement and automatic resolution decisions must be based on the same validated source/readout semantics, not separate assumptions embedded in UI code.

## 9. Build and integration boundary

The application ownership relocation is implemented in the current
`OPTIKA-B293-DUALSLIDE-APP` branch:

- Root `CMakeLists.txt` composes the camera capability and the optional native app.
- `Camera/Amscope/CMakeLists.txt` owns the reusable driver and camera-specific
  adapters/probes; it no longer owns or compiles the OPTIKA application.
- `Application/OptikaB293/CMakeLists.txt` owns the SwiftUI/AppKit application,
  application camera adapter, feature models and presentation files.
- `OPTIKA_BUILD_NATIVE_APP` controls the product app target and defaults OFF.
- The Objective-C++/Swift bridge and native camera controller are application
  integration adapters, not part of the reusable AmScope driver.
- The vendor SDK remains isolated in the AmScope implementation.

The current branch statically maps every application CMake source entry to a
file in the repository tree. This is a source-registration audit only; CMake
configuration and compilation remain unverified.

## 10. Migration and compatibility rules

The migration is incremental and behavior-preserving until a behavior is intentionally changed.

- The v0.8 app remains the migration reference in the pre-refactor Git baseline/history; its old source paths are removed from the current application branch after relocation.
- Move ownership before changing behavior.
- Keep the existing camera backend and frame pipeline intact.
- Extract and test pure calculations before wiring them to UI.
- Replace shared global state with composition-root-owned dependencies.
- Maintain the dual-slide behavior and its single-stream routing semantics.
- Do not add speculative motor or processing implementations to this branch merely to populate the architecture.
- Do not declare the new target accepted or retire the v0.8 reference until the user has completed the required compile, behavior and hardware acceptance on the target Mac.

## Current implementation status

The source refactor is committed on `OPTIKA-B293-DUALSLIDE-APP`. Application
source ownership, color/layout tokenization, microscope configuration ownership,
optics-level automatic-resolution coordination and slide image geometry have
been moved into their intended feature/presentation/composition boundaries.
CMake source paths have been checked against the repository tree.

This is not an acceptance declaration. The application has not been configured,
compiled, launched or tested in this refactoring session, and no camera/hardware
verification has been performed. The single-frame-consumer, acquisition,
frame-pool and physical-calibration acceptance gates remain open until verified
in the user's environment.

## 11. Definition of done

The new architecture is established when:

1. The application entry point and UI live outside `Camera/`.
2. Camera builds independently of the OPTIKA application.
3. Camera, Motion and ImageProcessing have no dependency on the application UI.
4. The application composition root wires concrete adapters to capability contracts.
5. Camera lifecycle and frame transport remain isolated from UI state.
6. Slide presentation state is independent of camera session state.
7. Optical and image-coordinate calculations are pure, explicit and tested independently.
8. The main UI is composed from focused presentation units rather than one central view/model.
9. The existing two-slide workflow and preview performance invariants are preserved.
10. New hardware capabilities can be added as siblings without expanding the camera module's responsibility.

---

## Appendix A. Terminology

- **Camera frame:** immutable acquired pixel payload plus frame metadata.
- **Source image:** the image coordinate system used by a slide document and its overlays.
- **Slide pane:** a presentation surface; it is not a physical slide database record by definition.
- **Live destination:** application routing choice for the single incoming camera stream.
- **Capability:** a replaceable hardware or computational subsystem such as Camera, Motion or ImageProcessing.
- **Application adapter:** code that translates an application use case into a capability contract.
- **Functional core:** deterministic calculations and state transitions without external side effects.
- **Imperative shell:** lifecycle-owning objects that perform I/O and coordinate external resources.

## 13. Slide image coordinate contract

### 13.1 Coordinate spaces

The viewer has three distinct coordinate spaces:

1. **Source-image space:** coordinates in the retained camera image, measured in source pixels. Measurements and annotations are attached to this space.
2. **Document space:** AppKit points inside the scaled image-container view. The image and its overlays are drawn in this space.
3. **Viewport space:** AppKit points local to the fixed slide viewer. Pointer events arrive in this space; the image document is positioned and clipped inside it.

The source image dimensions define canonical geometry. Display scale, pan origin,
window dimensions and backing scale are presentation transforms; none changes
source coordinates or optical calibration.

### 13.2 Transform definitions

For a uniform document scale (s), source point (p_s), and document origin (o):

[
p_v = o + s p_s
]

[
p_s = (p_v - o) / s
]

The renderer derives independent horizontal and vertical source-to-document factors
from actual image and document sizes. This keeps overlay registration correct if a
future presentation path rounds or resamples one dimension differently.

The geometry module owns deterministic operations for:
- physical display-pixel scale from the AppKit backing-scale factor;
- aspect-preserving fit, capped at physical 1:1;
- document dimensions at a requested scale;
- centering and pan-bound clamping;
- viewport-to-source pointer conversion;
- source-to-document overlay scaling;
- pointer-anchored zoom transforms.

AppKit remains responsible for receiving events, applying the returned transform,
retaining the image, drawing and managing view lifecycle.

### 13.3 Invariants

- A measurement endpoint and annotation anchor remain in source-image coordinates.
- Zooming or panning does not rewrite measurement or annotation coordinates.
- A zoom keeps the source point beneath the pointer fixed unless pan-bound clamping
  requires a correction. The document must not expose blank space beyond its edges.
- Fit preserves aspect ratio, displays the complete source frame and does not
  upscale beyond physical 1:1.
- Lock preserves the selected scale when the viewport resizes.
- Full Resolution maps one source pixel to one physical display pixel. On Retina
  screens this is fewer than one AppKit point per source pixel; the complete image
  document may be larger than the viewport and is clipped, not resized.
- Physical measurement uses micrometers-per-source-pixel from optical calibration.
  It is independent of document scale, viewport size, zoom, pan and display backing
  scale.
- When source dimensions change, overlays tied to the previous source grid are
  cleared. A frozen pane continues to retain its previous image/document.
- Invalid dimensions or non-positive/non-finite scales do not produce source
  coordinates or a geometry transform.

Pointer hit testing preserves the existing inclusive maximum boundary
((x leq width), (y leq height)). Changing this to pixel-center or half-open
bounds is a separate behavioral decision, not an incidental consequence of
extracting the geometry.

### 13.4 Functional-core boundary

`SlideImageGeometry` is a deterministic CoreGraphics value module. It has no
AppKit, SwiftUI, camera, image-buffer, persistence or global-state dependency.
The image view owns `NativeSlideImageSizingMode`, locked-scale state and native
event lifecycles; it delegates mathematical transforms to this module.

The image container uses the same source-to-document scale for overlay rendering.
This removes the duplicated mapping rule between pointer input and
measurement/annotation drawing.

Measurement and annotation values are slide-feature data under
`Features/Slides`, not AppKit view declarations. A measurement captures the
micrometers-per-source-pixel value when its first endpoint is created; later
changes to objective or optical configuration do not retroactively rewrite an
existing measurement. Pixel distance remains available when calibration is
unknown, while physical distance remains unavailable. Pointer-tool selection
is presentation state and remains beside the native preview controls.
