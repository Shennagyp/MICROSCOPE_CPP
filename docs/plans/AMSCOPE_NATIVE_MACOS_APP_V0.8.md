# Native macOS App v0.8 — Implementation Plan

**Branch:** `amscope-native-app-mac.v.0.8`  
**Base branch:** `amscope-native-driver-0.7`  
**Current state:** Branch created from the validated driver baseline. The opt-in SwiftUI/AppKit target, Objective-C++ bridge, serialized native controller, shared latest-frame mailbox, lease-backed AppKit preview, and descriptor-driven feature editors are committed. Accessory workflows, local build/visual validation, and hardware performance sign-off remain pending.

## Phase 0 — Baseline and architecture

- [x] Create the feature branch from `amscope-native-driver-0.7`.
- [x] Inspect the C++ driver, Qt adapter, frame mailbox, frame lease, and current Qt frame renderer.
- [x] Record the streaming invariants and native app architecture.
- [ ] Capture a repeatable baseline on the user's Mac for the same camera/resolution/pixel format used by the native app.
- [ ] Record sustained pull rate, event rate, camera drops, callback duration if instrumented, preview presentation rate, CPU, and memory.

**Gate:** The baseline evidence is recorded before making changes to the driver or frame path.

## Phase 1 — Native app target and launch shell

- [x] Add an opt-in `AMSCOPE_BUILD_NATIVE_MAC_APP` target; it is OFF by default and requires the Ninja generator, not full Xcode.
- [x] Add the SwiftUI shell, AppKit-managed main window, File-menu entry, and dedicated controls window hosted by `NSHostingController`.
- [x] Set the initial deployment target to macOS 10.15; avoid the newer SwiftUI `Window` scene API.
- [x] Add the Objective-C++ bridge and Swift bridging header.
- [x] Add camera enumeration and lifecycle actions (open, close, start, stop) to the controls window.
- [x] Add the AppKit preview surface and connect it to the shared latest-frame lease.
- [x] Configure and build the native app on the user's Mac using the standalone Command Line Tools toolchain and Ninja.
- [ ] Launch the generated app bundle and verify the native windows/menu.
- [x] Verify a native-only configuration with Qt disabled; do not remove the Qt targets from the repository.

**Current Phase 1 note:** On 2026-11-24, the user's Mac successfully configured the native-only build with Ninja, Swift 6.4.2, AppleClang 21, Objective-C++, and Qt adapter/preview disabled. The `amscope_native_mac_app` target linked successfully. The linker emitted only a non-fatal duplicate `-lc++` warning. Launch and visual inspection remain pending. CMake Swift support is used with Ninja, avoiding the Xcode generator and its `xcodebuild` dependency.

**Gate:** The native application bundle builds and launches independently of Qt, without changing existing driver code.

## Phase 2 — Framework-neutral camera controller

- [x] Introduce a native C++ controller owning exactly one `AmcamCamera` instance.
- [x] Serialize enumeration and lifecycle commands (open, close, start, stop) through a bounded FIFO command worker.
- [x] Include request IDs and session generations in operation completions; reject stale frame publication through the acquisition generation gate.
- [x] Keep blocking camera commands and all completion callbacks off the main actor; the Objective-C++ bridge marshals completion blocks to the main thread.
- [x] Keep the native controller independent of `QtAmcamCamera`; it calls the same C++ driver directly.
- [x] Port the generic feature descriptor/read/write API with type-preserving Objective-C++ conversion and snapshot-generation rejection.
- [ ] Port the supported accessory command/telemetry API (trigger configuration, I/O lines, filter wheel, guide port, focuser, still capture, and device-specific controls).
- [ ] Add controller tests for FIFO ordering, queue saturation, shutdown, generation changes, failed Start/Stop recovery, stale completions, and the preview-resolution stop/apply/restart transaction.

**Gate:** Lifecycle and feature-request tests cover FIFO ordering, shutdown, cancellation, generation changes, failed Start/Stop recovery, and stale completion rejection.

## Phase 3 — Shared latest-frame mailbox

- [x] Move the existing `FrameStreamMailbox` implementation into `Camera/Common` as `camera::FrameStreamMailbox`.
- [x] Keep the former Qt header as a source-compatibility alias and switch `QtAmcamCamera` to the common type directly.
- [x] Use this same single-slot mailbox in the native controller.
- [x] Preserve atomic publication, generation validation, notification coalescing, race-safe consume/re-arm, and prompt release of replaced frame leases.
- [x] Add framework-neutral tests for latest-frame selection, coalescing, stale-generation rejection, re-arm, and lease release; retain the existing Qt concurrency/affinity test.

**Gate:** Both frontends use the same mailbox implementation. Existing Qt behavior remains unchanged.

## Phase 4 — AppKit preview surface

- [x] Implement a custom AppKit `NSView` hosted by SwiftUI through `NSViewRepresentable`.
- [x] Invalidate the view through a coalesced main-thread notification from the native bridge; no per-frame SwiftUI observable state is created.
- [x] Consume the newest lease from the mailbox and release a pane's prior displayed image when a newer frame replaces it; retain the last image across slide switches and Stop/Close.
- [x] Build each Core Graphics image through a direct-access `CGDataProvider` whose release callback owns the opaque frame lease.
- [x] Keep no history of live preview frames and perform no application-side pixel-buffer copy in the continuous acquisition/render path. The separate user-triggered capture path may make one bounded pixel copy before encoding a persisted still.
- [ ] Validate RGB24, RAW8, RAW16, row stride, byte order, dimensions, invalid buffers, view teardown, and provider lifetime on the user's Mac.
- [ ] Measure frame-pool availability under sustained preview and confirm no starvation.

**Gate:** Preview correctness and lifetime tests pass; no pixel-buffer copy is introduced by application code; the four-frame pool does not starve during sustained display.

## Phase 5 — Native UI and control workflows

- [x] Build the main live-preview window, native application menu, and dedicated camera-controls window.
- [x] Add side-by-side Slide 1 / Slide 2 AppKit preview panes with a draggable divider and independent maximize/restore controls.
- [x] Route each incoming frame to the selected slide pane while retaining one last image per slide.
- [x] Add detected-camera actions that open the selected device and start acquisition through the existing serialized controller.
- [ ] Verify the dual-pane layout, active-slide routing, image persistence, and resizable workspace dock on macOS.
- [x] Keep camera discovery and lifecycle controls accessible through the File menu.
- [x] Replace the single-row bottom strip with a resizable, tabbed right-side workspace dock for Camera Controls and Smart Gallery; support hide/show and float/re-dock as a utility panel.
- [x] Add per-slide floating toolbars for still capture, pointer/pan, distance measurement, text annotation, and clearing slide overlays.
- [ ] Verify dock resizing, float/re-dock, capture file output, gallery recovery/retention, tool selection, image-coordinate overlays, and measurement/annotation rendering on macOS.
- [x] Remove the duplicate Camera, Resolution, Format, State, and Live read-only property fields and their unused observable frame-metadata properties; show camera identity/state only beside lifecycle controls on the left.
- [x] Prevent overlapping lifecycle requests and selection changes while a camera session is active.
- [x] Populate imaging controls from the camera's actual feature descriptor model, preserving boolean/integer/vector/double/string/enum types, ranges, units, and writability.
- [x] Render all available generic feature descriptors in the Camera Controls dock: writable boolean, integer, floating-point, string, integer-vector, and enumeration controls; read-only descriptor values remain visible.
- [x] Separate exposure range/capability from the current exposure readback so manual Exposure Time remains available when Auto Exposure is switched to Off; validate each requested value against the current SDK range.
- [x] Expose the camera-reported preview resolution choices in the dock and route selection through the native bridge and serialized controller.
- [x] When resolution changes during streaming, clear the pending old-size frame, stop acquisition, apply the resolution through the existing `AmcamCamera` feature API, and restart under a new session generation.
- [x] Size read-only numeric values to their content instead of reserving a wide editor region around each value.
- [ ] Verify manual Exposure Time and Analog Gain changes after switching Auto Exposure to Off; verify the reported ranges and readback.
- [ ] Verify preview-resolution selection with acquisition both stopped and running, including successful restart and Stop/restart failure handling.
- [ ] Port dedicated accessory workflows and telemetry from the existing Qt/native contract.
- [x] Queue camera Close and wait for completion before application termination.
- [ ] Verify window detach/close, disconnect/reconnect, and still-running camera behavior on hardware.

**Current limitation:** The two-slide UI currently changes only the destination pane for incoming frames. It does not move a physical stage or operate a slide changer; this must be added through the controller later. Dedicated accessory workflows, post-change compile/visual validation, frame-pool validation, and hardware performance validation remain outstanding. Preview-resolution changes can stop and restart acquisition, so the local UI and hardware checks must confirm that the selected dimensions are actually applied and that the stream recovers correctly.

**Gate:** All controls use asynchronous controller requests; no camera call blocks the main actor; UI state cannot issue stale commands to a newly selected device.

## Phase 6 — Functional parity and application lifecycle

- [x] Port discovery, open/close, start/stop, and generic feature descriptor/read/write workflows.
- [ ] Port trigger, I/O, filter-wheel, ST4, focuser, the vendor SDK's dedicated hardware still-capture operation, and other supported device workflows. Smart Gallery currently saves the displayed preview image; it does not yet call the SDK still-capture API.
- [ ] Verify close/stop sequencing, application termination, view teardown, device disconnect, reconnect, and stale callback behavior.
- [x] Keep the Qt application as a separate fallback while parity remains incomplete.

**Gate:** The native UI covers the existing supported camera workflows selected for v0.8 without changing driver semantics.

## Slide workspace, dock separation, and image document sizing (2026-11-24)

The slide workspace and tool dock are separate siblings in a horizontally
resizable `HSplitView`. The preview owns the left canvas; the Camera Controls
/ Smart Gallery tab group occupies the right dock. The right dock can be hidden,
floated into an independent utility panel, and re-docked. It is never painted
over a slide viewport. The SwiftUI preview region is clipped to its allocated
canvas, and the native AppKit preview/split-view hierarchy has matching clipping
boundaries. An oversized image document is clipped by its own slide viewport,
not by or into the tool dock.

The two existing slide panes, divider, and window remain unchanged. The
former upper header row is removed and replaced by specimen-view annotations
and a compact floating action cluster.
Each pane is a fixed viewport containing a separate image document. The
viewport is the visible window onto the specimen; it is not the image itself.

Each pane has two distinct controls:

- **Lock / Auto Resize:** the open-lock icon means the image follows the
  viewport size. Clicking it preserves the exact scale currently displayed;
  it does not jump to 1:1. The closed-lock icon means the selected scale is
  retained when that pane is resized. Clicking it resumes automatic resizing.
- **1:1:** displays the complete source image in a full-size image document at
  native physical-pixel scale: source width × source height, with one camera
  pixel mapped to one physical display pixel. The document is not resized to
  the viewport, cropped to a smaller backing image, or replaced by a preview.
  If the viewport is smaller than the document, only the visible region is
  shown; dragging pans the viewport over the same complete image document,
  with movement clamped at image edges.

The implementation distinguishes three states internally: Resizable,
Locked Scale, and Full Resolution. This prevents a camera resolution change
or display backing-scale change from turning an ordinary locked scale into
1:1. Full Resolution derives its point dimensions from the current display's
backing scale so that the one-source-pixel-to-one-physical-pixel invariant
remains true.

In **Resizable** mode, the image document automatically recalculates its
scale when that pane's viewport changes because of the split divider or
maximize/restore. It preserves aspect ratio and fits the complete source frame
inside the viewport; it never crops specimen edges merely to eliminate
margins, and it never upscales beyond native 1:1 physical-pixel size. In
**Locked Scale** mode, the document retains its selected scale when the
viewport changes. Scroll-wheel or trackpad zoom locks the pane only after the
scale actually changes. The original full-resolution CGImage remains the
source in all modes.

Each slide owns its own mode, scale, and pan position. The context menu exposes
Resizable, Lock Current Scale, and View Full Resolution (1:1). A separate
floating per-slide tool strip provides capture, pointer/pan, distance, text-note,
and clear-overlay actions; it does not add another frame consumer.

Downsampling uses high-quality interpolation; native 1:1 mapping disables
interpolation. The provider-backed full-resolution CGImage remains the source;
no pixel-buffer copy or frame history is added. Camera resolution selection
and optical Auto Res remain independent of image presentation.

**Verification pending:** rebuild and verify that the right-side dock remains a separate split-view sibling, floats and re-docks correctly, and does not cover either slide; verify that the 1:1 document dimensions
match the complete source frame at physical-pixel scale; pan to all four image
edges without gaps or unreachable regions; verify independent per-pane scale
states, divider resizing, camera mode changes, and Retina backing-scale changes.
No local build or hardware test is claimed.
## Transparent window and specimen annotation surfaces (2026-11-24)

Transparency is implemented at the actual window and content-surface level,
not only by assigning a clear fill to an inner control stack.

- The main window uses a transparent title bar with
  `.fullSizeContentView`.
- The main `NSWindow` is non-opaque with a clear background.
- The `NSHostingView` and native window content view use clear, non-opaque
  backing layers.
- Each slide pane and its specimen annotation/action plates use clear, non-opaque parent surfaces; localized annotation plates provide contrast behind their contents.
- The main-window tool dock uses its own panel surface; native controls remain opaque and legible.

The former full-width upper slide header is removed. Slide identity, live/held
status, and frame statistics are now compact specimen-view annotations at the
upper-left corner. They retain the same text and values, with a restrained
annotation plate for legibility. The existing slide actions remain available
in a separate compact upper-right floating control plate; they no longer
occupy a dedicated header row or reduce the image viewport height.

The annotation and action plates are overlays inside the clipped slide pane,
not a replacement opaque bar. Native buttons, symbols, labels, status text,
toggles, and feature controls retain their normal foreground opacity and
native styling. Feature-control cards retain their own control-background
fills.

The right-side dock remains a separate split-view sibling. When floated, it
uses its own utility panel. Neither state overlays or covers a specimen viewport.

**Verification pending:** inspect the main window on macOS in light and dark
appearance; confirm both bar backgrounds reveal the transparent window surface
while every control remains fully legible and interactive. Verify the native
title-bar controls remain visible and operable. No local visual test is claimed.

## Two-slide workspace and Visual Studio-style tool docking (2026-11-24)

The main canvas retains the two side-by-side AppKit slide panes and draggable
divider. The former full-width bottom feature strip has been replaced by a
right-side, horizontally resizable tool dock.

The shared dock is a tab group with:

- **Camera Controls:** camera discovery, open/start, Stop and Close through the
  camera-name context menu, state, microscope objective, Auto Res, live-frame
  destination, and all generic feature descriptors with adjacent labels.
- **Smart Gallery:** saved preview captures with thumbnails, source dimensions,
  capture date/time, open-in-viewer, Show in Finder, and Delete.

The dock can be hidden, shown, floated as a resizable utility panel, and
re-docked into the main window. Its two tools are tabs in one shared dock group;
independent free-form tab dragging/rearrangement and docking to arbitrary
window edges are not part of this increment.

Each slide owns a separate floating overlay toolbar. It contains capture,
freeze/resume, pointer/pan, distance measurement, text annotation, and
clear-overlay actions. Tool selection and freeze state are local to the slide
pane; changing the tool on Slide 1 does not change Slide 2's tool state.
Selecting distance or annotation mode freezes that pane's currently displayed
source image automatically. Resuming live display returns the tool to Pan mode.
The camera acquisition continues; the single preview consumer continues
draining and releasing new leases without replacing the frozen pane image.
Existing zoom, pan, sizing, live-target, and maximize actions remain per pane.

A distance measurement is made by dragging between two points. Its endpoints
are converted from viewport coordinates into the original image's pixel grid,
and the line follows image zoom and pan. The displayed length is explicitly
in source pixels. Physical units (µm/mm) are not displayed because no complete
stage-micrometer calibration for the current objective/camera/adapter path is
yet attached to this measurement model.

A text annotation is placed at a source-image coordinate and entered through
a native text prompt. Measurements and notes are in-memory overlays belonging
to the pane. They survive zooming and panning but are cleared when the source
image dimensions change, because the prior pixel coordinate system can no
longer be assumed to describe the new frame. They are not yet persisted across
application restart or included in the PNG capture; captures currently preserve
the raw displayed camera image without overlay burn-in.

The capture button saves the current pane image as a PNG under
`~/Pictures/AmScope Captures`. Encoding and thumbnail creation run away from
the UI thread. A single capture is processed at a time; the gallery loads at
most 80 recent thumbnails into memory, while older PNG captures remain on disk; thumbnails are capped at 320 pixels. It uses the
existing pane image as its source; it does not add a mailbox consumer or claim
the camera driver's separate hardware still-capture API.

The AppKit preview parent remains the only consumer of the bridge's frame
callback and mailbox. Each pane retains at most one provider-backed live image.
The explicit capture path copies the selected image's pixel data before PNG
encoding so the acquisition frame lease is not retained as gallery history.
The capture operation remains user-triggered and bounded; the regular frame
path has no new consumer, queue, or per-frame SwiftUI observation.

**Verification pending:** rebuild and inspect on macOS; resize the main dock;
float and re-dock it; switch Camera Controls / Smart Gallery; capture from both
slides; confirm output files and startup recovery; verify one-at-a-time capture
behavior, an 80-thumbnail in-memory limit, and no automatic deletion of older PNGs; select tools independently on both slides; verify Ruler/Annotation
automatically freezes the pane and Resume returns it to Pan/live display; confirm
frozen panes continue draining and releasing incoming frame leases; drag distance
measurements and add notes; pan/zoom and verify overlays remain
at their original image coordinates; confirm overlays clear on a source
dimension change; and monitor frame-pool availability, drops, and memory.
No local build or visual/hardware result is claimed.

## Deferred comparison workflow roadmap (not part of current implementation)

The following are product-direction requirements for a later comparison
workflow. They are intentionally **not** implementation tasks for the current
v0.8 UI change.

1. **Default to a single-slide workspace.** Opening the application should
   present one primary slide at full viewer priority. Provide an immediate,
   one-click transition to a split/compare workspace when the user needs a
   second slide. Returning to single-slide mode must be equally direct.
2. **Prioritize registration and synchronization over analytics.** The
   comparison workflow should support linked pan and zoom, with an explicit
   lock/unlock state, plus rapid navigation to corresponding regions between
   slides. Percentage/delta metrics should not dominate the default workflow.
   Consider quantitative analytics only for workflows explicitly centered on
   biomarker measurement, AI output comparison, or similar validated tasks.
3. **Protect primary-viewer responsiveness and resource budgets.** Opening a
   second slide must not degrade the primary viewer's interaction latency or
   frame presentation. Define memory, CPU, GPU, decode, and cache budgets;
   use bounded caches and explicit lifecycle/release policies, particularly
   for large whole-slide images (WSIs) and constrained RAM/GPU systems. Measure
   the single-slide baseline and enforce a performance budget before enabling
   comparison at scale.

**Status:** Deferred. Do not implement these items as part of the current
annotation/header change. They require a separate design and performance
milestone.

## Phase 7 — Streaming performance and hardware sign-off

- Run the same sustained hardware scenario on the Qt baseline and native app.
- Compare `framesPulled/sec`, image events, camera-side drops, preview presented FPS, frame age, CPU, memory, and lease/pool behavior.
- Repeat enough runs to identify normal run-to-run variation.
- Verify no per-frame SwiftUI update, unbounded queue, frame history, extra acquisition worker, or application-side pixel copy exists.
- Fix any regression before treating the native app as the preferred frontend.

**Gate:** No measurable acquisition regression beyond baseline noise, no additional camera drops, stable bounded memory, no frame-pool starvation, and correct rendered image formats.


## Standalone Command Line Tools build findings (2026-11-24)

The first native-only configure was initially attempted while the local checkout
was still on `amscope-native-driver-0.7`. CMake correctly reported
`AMSCOPE_BUILD_NATIVE_MAC_APP` as unused and the native target did not exist.
After switching to `amscope-native-app-mac.v.0.8`, CMake detected Swift 6.4.2,
AppleClang 21, and Objective-C++; generation succeeded with Ninja and Qt disabled.

The first native target build reached Swift compilation/linking and exposed
these frontend integration issues:

- Objective-C `NSError` block parameters are imported into Swift as
  `any Error`; the model's error presenter now accepts `Error`.
- The standalone Swift toolchain invocation could not resolve SwiftUI's
  `StateMacro` plug-in for the information-bar editor. The editor identity is
  now represented by a low-frequency `@Published` model property and a
  computed `Binding`, avoiding that macro dependency.
- The bridge guarantees feature-read completions are dispatched to the main
  queue. The model now uses `MainActor.assumeIsolated` at that callback
  boundary, preserving synchronous completion delivery without adding a
  separate task/dispatch for each feature response.

These are UI/control-plane corrections only. They do not modify
`AmcamCamera`, its vendor callback, event queue, acquisition worker,
`Amcam_PullImageV4`, frame storage, frame leases, or the shared mailbox.

**Build status remains pending.** The fixes must be verified by rerunning the
native target build on the user's Mac. No app launch, camera streaming,
pixel-format, lease-lifetime, or hardware performance result is claimed from
the compiler diagnostics alone.


## Second standalone Swift build findings (2026-11-24)

After the first corrections were pulled, the next native target build no longer
reported the earlier `NSError` conversion or SwiftUI `StateMacro` errors.
It exposed:

- Swift 6.4 imports the variadic Objective-C `NSLog` API as unavailable. The
  termination path no longer calls it.
- Swift imports the Objective-C bridge completion blocks as Sendable closures.
  The bridge implementation explicitly dispatches every camera lifecycle,
  enumeration, feature, and acquisition-error callback to the main queue. The
  model now uses `MainActor.assumeIsolated` at those documented callback
  boundaries. This preserves synchronous delivery on the existing main queue;
  it does not add a second dispatch or create per-frame Swift tasks.
- The feature-write callback previously captured the Objective-C feature
  descriptor object. It now captures only the feature name string, avoiding a
  non-Sendable Objective-C object crossing the imported Sendable closure.

Application termination now treats a failed camera close as a recoverable
failure: the application remains open, refreshes the model's camera state,
and reports the error rather than silently quitting while a camera session may
still be active.

These changes remain in the UI/control plane. They do not modify the vendor
callback, event queue, acquisition worker, `Amcam_PullImageV4`, frame pool,
frame leases, or latest-frame mailbox.

**Build status remains pending.** The reported build stopped at Swift linking
because of the `NSLog` compile error. The changes above have been committed,
but the native target must be rebuilt on the user's Mac to verify them. No
application launch or camera-streaming result is claimed.


## Native target build result (2026-11-24)

User-reported command:

```sh
cmake --build build/amscope-native-mac \
  --target amscope_native_mac_app \
  --parallel
```

Result: `[1/1] Linking Swift executable amscope_native_mac_app.app/Contents/MacOS/amscope_native_mac_app`
completed successfully. The linker printed `ld: warning: ignoring duplicate libraries: '-lc++'`.
This is a warning, not a failed build. Do not change link dependencies solely to
silence it; first verify app launch and runtime behavior.

The configuration used `AMSCOPE_BUILD_NATIVE_MAC_APP=ON`,
`AMSCOPE_BUILD_QT_ADAPTER=OFF`, and
`AMSCOPE_BUILD_PREVIEW_APP=OFF`. No Qt target was required to build the native
application. The app has not yet been launched, and the build result does not
establish camera connectivity, frame rendering correctness, lease lifetime,
pool availability, or streaming performance parity.

## Controls-window layout correction (2026-11-24)

The first native controls-window inspection exposed a layout defect: the SwiftUI root
imposed a 620-point minimum width while the `NSWindow` opened at 520 points, and
the long descriptor-driven feature list had no explicit vertical scroll viewport.
Maximizing the window could not reliably make the full control set accessible.

The UI correction:

- Replaces the `Form` root with a vertical `ScrollView` containing a width-flexible
  `VStack`, so the viewport tracks the resizable window while the complete feature
  list remains reachable by scrolling.
- Uses `GroupBox` for Camera, Acquisition, and each feature group; feature rows
  wrap long labels and explanatory text within the available width.
- Removes the conflicting 620-point content minimum and opens the controls window
  at 680 × 700 points while retaining a 480 × 420 minimum window size.
- Leaves the C++ controller, Objective-C++ bridge, feature API, acquisition path,
  frame mailbox, and preview path unchanged.

**Verification pending:** pull the change, rebuild the native target, then verify
that the controls window resizes in both dimensions and that all feature groups
remain reachable by vertical scrolling at the minimum size and when maximized.

## Implementation rules

- Do not change `AmcamCamera.cpp`, the SDK callback, event queue, pull loop, or frame-pool layout for UI convenience.
- Do not add a second camera backend or a second acquisition thread.
- Do not connect the acquisition callback directly to Swift or `MainActor`.
- Do not publish every frame through SwiftUI observation.
- Do not create a second pixel buffer in the continuous preview path using `Data`, `NSBitmapImageRep`, or equivalent. A user-triggered still capture is the explicit exception: it may detach one image from its frame lease, bounded to one concurrent capture.
- Do not introduce Metal/IOSurface until the AppKit direct-access route is correct and its actual bottleneck has been measured.
- Do not mark a performance gate complete without user-run hardware evidence.


## Workspace dock and all-feature mapping (2026-11-24)

The right dock is a resizable tab group, not a single-row control strip. The
Camera Controls tab lists camera connection/state and lifecycle actions,
microscope objective, Auto Res, live frame destination, and the generic feature
descriptor list. Feature controls retain their names beside their editors.
Smart Gallery is a separate tab showing persisted PNG preview captures.

The dock is hideable and can float in a resizable utility panel or return to
the main window. The group currently contains two tabs; arbitrary docking
layouts and independent tab dragging are deferred.

The preview image is captured from the pane's current `CGImage`, not by calling
the vendor SDK's dedicated still-capture operation. Captures are PNG files in
the user's Pictures/AmScope Captures folder. Gallery rendering loads at most the 80 most recent thumbnails into memory;
older PNG captures remain on disk and are not deleted automatically. Only one
capture is processed at a time, and thumbnails are generated asynchronously. The capture path copies image pixels before encoding, avoiding
long retention of a frame-pool lease.

Per-slide tool overlays store points in source-image pixel coordinates. The
current ruler measures straight-line pixel distance only. Text notes are
anchored to the image grid. Both stay aligned through pan/zoom, clear when the
source dimensions change, and are session-only. They are not yet saved into
capture metadata or burned into PNGs. Calibrated physical units, angles/areas,
editable overlay objects, and annotation persistence remain future work.

**Verification pending:** rebuild/launch on macOS; verify feature labels and
control editing, camera state actions, main dock resize, floating-panel
lifecycle, capture/save/retention, per-slide tool isolation, overlay coordinate
mapping, source-size reset behavior, and memory/frame-pool behavior. No local
build, visual test, or hardware verification is claimed.

## Phase 5 addition — microscope optics and automatic resolution (2026-11-24)

- [x] Add a persisted OPTIKA B-293 optical profile with N-PLAN objectives,
  magnifications, N.A., immersion, 160 mm tube system, and Abbe condenser N.A.
- [x] Replace the single 550 nm Rayleigh wavelength with a configurable H&E
  chromatin band (initially 540–600 nm); report RF across the band and use its
  shortest wavelength for conservative mode qualification.
- [x] Add a separate Microscope Configuration window for optical-profile,
  sensor-pitch, named camera-adapter presets (FMA050, M-115, M-114), custom
  total-relay configuration, wavelength, and sampling-target configuration.
- [x] Add the manually selected active-objective state to the compact dock.
- [x] Add an Auto Resolution policy that evaluates only camera-reported modes,
  chooses the lowest-pixel-count mode meeting the configured Rayleigh sampling
  target, and falls back to maximum mode with a visible shortfall when no mode
  meets the target.
- [x] Manual selection of camera resolution disables Auto Resolution.
- [ ] Verify the actual MU503 mode-to-binning/full-field mapping.
- [ ] Verify that the configured ToupTek FMA050 preset matches the physical
  optical path; confirm whether any additional relay element is actually stacked
  and calibrate effective magnification against a stage micrometer.
- [ ] Verify preset switching, custom-path entry, persistence across restart, and
  migration of an older numeric-only profile.
- [ ] Verify mode selection and stream recovery for all four objectives on the
  user's hardware.
- [ ] Implement and verify the separate preview-pane auto-expansion policy;
  it must not be coupled to camera acquisition resolution.

**Important model boundary:** the B-293 objective nosepiece is manual. The active
objective is a user-confirmed state, not an electronically sensed state. The
default camera-path preset is ToupTek FMA050 at 0.50×, based on the user's
identified adapter. Adapter selection remains editable and is not electronically
detected; the user must keep it aligned with the physically installed optical
path.


## Third standalone Swift build finding — configuration-window draft state (2026-11-24)

After the camera-adapter selector was added, the native target reached the Swift
link invocation but failed because the installed standalone Command Line Tools
toolchain could not load the SwiftUI `StateMacro` implementation from
`SwiftUIMacros`. The two new `@State` properties in
`MicroscopeConfigurationWindow` triggered the primary error. The later
missing `_draft` / `$draft` symbols and immutable-`self` diagnostics were
cascading errors from those unavailable property wrappers.

The correction removes `@State` from this window and moves the editable
configuration draft into a model-owned `MicroscopeConfigurationDraftStore`,
an `ObservableObject` using `@Published`, observed by the window through
`@ObservedObject`. This matches the state-management workaround already used
elsewhere in the native application for the same standalone toolchain limitation.
The store retains the draft while the configuration window remains owned by its
window controller; applying updates the persisted microscope profile, while
editing and reset operations remain draft-only until Apply.

**Verification pending:** rebuild the native target on the user's Mac. The
reported failure is addressed in source, but no successful rebuild is claimed.
No camera driver, SDK callback, acquisition, frame mailbox, or image lifetime
code is changed by this correction.



## Frame telemetry and automatic-resolution regression correction (2026-11-24)

The AppKit preview header reports cumulative camera frames pulled, images
assigned to each slide pane, and driver-reported dropped frames. The counters
are read from the existing bridge during the single coalesced mailbox
notification. Both panes show the shared acquisition totals; each pane keeps
its own image-assignment count. No additional consumer or frame queue is added.

A first attempt to add preview backing-pixel matching made display size a hard
second eligibility condition in the optical Auto Res selector and enabled it
by default. That coupled two independent policies and could force the maximum
camera mode solely because the active Retina preview rectangle exceeded a
lower camera mode. It made the optical selector appear stuck and obscured
whether the optical decision itself was being applied.

The coupling has been removed. Auto Res again selects only from the configured
optical Rayleigh criterion and the camera-reported modes. Display matching is
deferred until it can be implemented as a separate, explicitly tested policy
that does not silently override the optical choice.

**Verification pending:** confirm pulled, per-pane assigned, and dropped
counters during sustained streaming; verify objective changes select the
expected optical mode; verify camera mode changes complete Stop/Apply/Start and
the refreshed feature value equals the selected symbol. No build or hardware
verification is claimed by this source change.
