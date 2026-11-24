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
- [ ] Add controller tests for FIFO ordering, queue saturation, shutdown, generation changes, failed Start/Stop recovery, and stale completions.

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
- [x] Keep no history of frames/images and perform no application-side pixel-buffer copy.
- [ ] Validate RGB24, RAW8, RAW16, row stride, byte order, dimensions, invalid buffers, view teardown, and provider lifetime on the user's Mac.
- [ ] Measure frame-pool availability under sustained preview and confirm no starvation.

**Gate:** Preview correctness and lifetime tests pass; no pixel-buffer copy is introduced by application code; the four-frame pool does not starve during sustained display.

## Phase 5 — Native UI and control workflows

- [x] Build the main live-preview window, native application menu, and dedicated camera-controls window.
- [x] Add side-by-side Slide 1 / Slide 2 AppKit preview panes with a draggable divider and independent maximize/restore controls.
- [x] Route each incoming frame to the selected slide pane while retaining one last image per slide.
- [x] Add detected-camera actions that open the selected device and start acquisition through the existing serialized controller.
- [ ] Verify the dual-pane layout, active-slide routing, image persistence, and compact dock on macOS.
- [x] Keep camera discovery and lifecycle controls accessible through the File menu.
- [x] Add one compact bottom dock with camera connection indicator and lifecycle actions, slide target selection, and horizontally scrollable generic camera feature controls.
- [x] Remove the duplicate Camera, Resolution, Format, State, and Live read-only property fields and their unused observable frame-metadata properties; show camera identity/state only beside lifecycle controls on the left.
- [x] Prevent overlapping lifecycle requests and selection changes while a camera session is active.
- [x] Populate imaging controls from the camera's actual feature descriptor model, preserving boolean/integer/vector/double/string/enum types, ranges, units, and writability.
- [x] Render all available generic feature descriptors in the bottom dock: writable boolean, integer, floating-point, string, integer-vector, and enumeration controls; read-only descriptor values remain visible.
- [ ] Port dedicated accessory workflows and telemetry from the existing Qt/native contract.
- [x] Queue camera Close and wait for completion before application termination.
- [ ] Verify window detach/close, disconnect/reconnect, and still-running camera behavior on hardware.

**Current limitation:** The two-slide UI currently changes only the destination pane for incoming frames. It does not move a physical stage or operate a slide changer; this must be added through the controller later. Dedicated accessory workflows, post-change compile/visual validation, frame-pool validation, and hardware performance validation remain outstanding.

**Gate:** All controls use asynchronous controller requests; no camera call blocks the main actor; UI state cannot issue stale commands to a newly selected device.

## Phase 6 — Functional parity and application lifecycle

- [x] Port discovery, open/close, start/stop, and generic feature descriptor/read/write workflows.
- [ ] Port trigger, I/O, filter-wheel, ST4, focuser, still-capture, and other supported device workflows.
- [ ] Verify close/stop sequencing, application termination, view teardown, device disconnect, reconnect, and stale callback behavior.
- [x] Keep the Qt application as a separate fallback while parity remains incomplete.

**Gate:** The native UI covers the existing supported camera workflows selected for v0.8 without changing driver semantics.

## Two-slide workspace and information bar (2026-11-24)

The main window now has two side-by-side slide panes and one thin bottom dock; the
separate camera row above the preview has been removed. The dock contains camera connection/lifecycle actions, a Slide 1/Slide 2 target
selector, and the complete currently exposed generic feature descriptor list in one
horizontally scrollable strip. The dock can collapse or detach into a floating
utility panel and re-dock; duplicated camera/resolution/format/state property
readouts have been removed. Each slide pane has a persistent Switch Live control,
its own maximize/restore control, and a thin 1-point red outer border only while selected as the
UI live target. A new frame is routed only to the selected pane; the other retains
its last displayed image.

Each detected camera button runs Open followed by Start using the existing
serialized C++ controller. This is still one camera session, not one session per
slide. The active-slide choice is a UI routing intent pending future controller
support for physical slide/stage switching.

The AppKit preview parent is the only consumer of the bridge's frame callback and
mailbox. Do not add one bridge listener or mailbox consumer per pane. Each pane
retains at most one provider-backed image lease. With the existing four-slot frame
pool, the two retained slide images plus a mailbox frame leave one slot for the
acquisition worker; sustained pool availability and camera drop counts must be
verified before acceptance.

**Verification pending:** build and launch on macOS; resize the pane divider;
maximize and restore each pane; switch the live destination repeatedly; confirm
the inactive slide image does not change; Stop and Close must not erase either
image; and verify no frame-pool starvation, unexpected drops, or memory growth.

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
- Do not create a second pixel buffer using `Data`, `NSBitmapImageRep`, or equivalent.
- Do not introduce Metal/IOSurface until the AppKit direct-access route is correct and its actual bottleneck has been measured.
- Do not mark a performance gate complete without user-run hardware evidence.


## Compact camera dock and all-feature mapping (2026-11-24)

The bottom dock is a single 50-point expanded row (28 points collapsed) below the
two slide previews. It is not split into an editable pane and a read-only property
pane. Camera identity and connection state are shown beside the camera selector
and lifecycle actions at the left. Camera, Resolution, Format, State, and Live
property fields were removed from the dock.

Every available generic `featureDescriptors` entry is mapped in the dock's
horizontally scrollable strip. Writable descriptors use type-appropriate controls:
boolean Toggle, discrete integer Picker, enumeration Picker, or text entry for
continuous integer, floating-point, string, and integer-vector values. Read-only
descriptors display their current values. A segmented Slide 1 / Slide 2 selector
and per-pane Switch Live actions control the UI live destination. The active pane
uses a 1-point red outline.

The dock can collapse, detach into a floating utility panel, and re-dock. The
current selection changes frame routing only; physical microscope slide/stage
movement remains a future controller operation. Updated compile, visual, and
hardware validation are pending.

**Scope limit:** this maps every generic feature descriptor currently exposed by
the native bridge. Separate camera commands already defined in the Qt/native driver
facade (auto white balance, trigger/I/O configuration, frame-rate telemetry, still
capture, embedded light source, filter wheel, ST4, focuser, lens focus/aperture)
are not yet connected to the native bridge/controller and are not claimed as
available dock controls.

