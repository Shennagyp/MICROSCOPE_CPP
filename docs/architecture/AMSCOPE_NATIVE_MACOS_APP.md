# Native macOS Application Architecture (v0.8)

**Branch:** `amscope-native-app-mac.v.0.8`  
**Base:** `amscope-native-driver-0.7` (`17067f3c5b10a4bb0e748297bbd34ada295bcc43`)  
**Status:** The native target has linked successfully on the user's Mac. The dual-slide workspace, single-consumer AppKit preview, camera open/start action, and split information bar are committed; updated build, visual QA, frame-pool validation, and hardware performance sign-off are pending.

## 1. Objective

Introduce a native macOS application using **SwiftUI for the application shell and controls, AppKit for the high-rate preview surface, and the existing C++ Amscope driver for camera ownership and acquisition**.

This is a frontend migration, not a camera-backend rewrite. The existing Qt preview remains available as a separate target until the native application passes the same functional and hardware performance gates.

The v0.8 user-interface structure is:

- The main window contains two persistent preview panes, Slide 1 and Slide 2, side by side. Either pane can be maximized independently and restored.
- A detected-camera button opens the chosen camera and starts acquisition in one action. The existing **File → Camera Controls…** window remains available for advanced lifecycle and feature controls.
- The bottom information region is a draggable horizontal split: editable quick controls on the left and read-only camera/image/session information on the right.
- Up to three supported writable numeric features are exposed as direct value-entry controls in the editable region; the full descriptor editor remains in Camera Controls.
- Both images are rendered by a single purpose-built AppKit preview owner hosted in SwiftUI, not by SwiftUI `Image` views or per-frame observable properties.
- The dedicated camera-controls window uses a width-flexible vertical layout inside an explicit scroll viewport; camera, acquisition, and descriptor groups remain reachable when the window is resized or the camera exposes many features.

## 2. Non-negotiable streaming contract

The camera stream is the critical path. A UI framework must never become part of acquisition scheduling.

The following existing topology is preserved:

```text
Amcam SDK callback
    |
    | enqueue event code only
    v
bounded fixed-capacity event queue
    |
    v
single application-owned acquisition worker
    |
    | Amcam_PullImageV4
    v
existing frame pool / immutable FramePtr lease
    |
    | synchronous callback; move the lease
    v
generation gate + one-slot latest-frame mailbox
    |
    | at most one outstanding render invalidation
    v
AppKit preview view
    |
    v
Core Graphics draw using the retained frame lease
```

### Invariants

1. `AmcamCamera` remains the sole owner of the native `HAmcam` session. There is one handle per opened session.
2. The vendor callback only enqueues event codes. It performs no SDK calls, image pulls, UI calls, lifecycle operations, or resource release.
3. There remains exactly one acquisition worker and one `Amcam_PullImageV4` path.
4. The frame pool, immutable `FramePtr` lease semantics, frame metadata, pixel formats, stride, drop accounting, and watchdog behavior remain unchanged.
5. The acquisition callback must not call Swift directly, enter `MainActor`, construct an `NSImage`, create a `Data` copy, convert pixels, or wait for rendering. The Objective-C++ bridge only schedules a coalesced main-queue invalidation.
6. Presentation is **latest-frame-wins**, not an unbounded FIFO. If rendering falls behind, old preview frames are replaced; acquisition continues.
7. Session-generation validation occurs before and after mailbox publication. A stale session cannot republish into the current view.
8. There is no per-frame SwiftUI state update, no unbounded per-frame main-queue backlog, no timer, display polling loop, or additional frame queue. The mailbox permits at most one outstanding notification; a consumer may re-arm it if a newer frame arrived during consumption.
9. No UI control performs blocking camera I/O on the main thread.

The current acquisition implementation uses a four-frame pool. The two-slide presentation may retain at most one current image lease per slide (two leases total), while the single-slot mailbox may retain one more. This leaves one pool slot available for the acquisition worker. There must be no image history, pending-frame array, duplicate preview consumer, or extra retained image per slide. Pool availability and provider-release timing are explicit hardware validation gates.

## 3. Layering and ownership

### Native application shell — SwiftUI

Owns macOS window composition, menus, control-window presentation, settings, user interaction, and low-frequency application state.

SwiftUI observes camera state, feature descriptors/values, errors, active-slide intent, and command completions. Controls are generated from the backend's feature metadata and preserve the feature's declared type, range, unit, discrete options, and writability. The bottom bar promotes a small set of writable numeric features as direct editable controls; it does **not** observe every incoming frame.

### Preview surface — AppKit

A custom dual-pane `NSView`, integrated through `NSViewRepresentable`, owns the rendering lifecycle. It registers exactly one bridge frame callback and consumes the latest published lease once. It routes the new `CGImage` to the selected slide pane; each pane retains its own current image for AppKit drawing.

Do not route frames through SwiftUI `Image`, `@Published` frame properties, `NSBitmapImageRep`, or `Data` as an intermediate pixel container. Those abstractions obscure ownership and can introduce copying or excessive state invalidation.

### Objective-C++ boundary

A narrow Objective-C++ facade adapts Swift-facing methods and callbacks to the C++ camera controller. It must not expose vendor SDK types, `QObject`, `std::vector`, `std::shared_ptr`, or other complex C++ implementation types to Swift.

The bridge exposes:
- typed lifecycle and camera-control commands;
- typed feature descriptors and read/write values through a stable Objective-C API;
- low-frequency state and command results;
- opaque retained frame-lease handles and read-only frame metadata;
- one coalesced frame-available notification.

Swift does not directly own or call `HAmcam`.

### C++ camera runtime

The native frontend reuses `Amscope::Driver` and its existing `AmcamCamera` session owner.

The native-facing asynchronous command controller preserves the relevant Qt adapter semantics: serialized lifecycle and feature operations, FIFO mutation ordering, request IDs, session and feature-state generations, stale-snapshot rejection, and callbacks outside the acquisition worker. Lifecycle and feature operations run on one bounded command executor. This controller is a **replacement for the Qt-facing controller in the native executable**, not a second controller layered on top of `QtAmcamCamera`.

The native application must not link or instantiate `QtAmcamCamera`. The Qt adapter and native controller are separate frontend adapters over the same driver, and they are never active as two owners of one camera session.

## 4. Frame handoff and rendering lifetime

The shared `camera::FrameStreamMailbox` in `Camera/Common` is a single-slot, atomic latest-frame mailbox with a notification-pending latch. The Qt include path remains as a source-compatibility alias, while `QtAmcamCamera` now consumes the common type directly. The native frontend must use this same implementation, not write a competing queue.

The mailbox contract is:

- publish by moving the immutable `FramePtr` lease;
- atomically replace the previous latest frame;
- recheck the acquisition generation after publication;
- schedule a notification only when the pending latch transitions from false to true;
- consume the latest lease and clear/re-arm the notification latch with the existing race-safe recheck.

The mailbox has already been moved to the backend-neutral `Camera/Common` header. The existing Qt include remains as a source-compatibility alias; `QtAmcamCamera` and the native controller use the same implementation. The same framework-independent mailbox tests exercise latest-frame replacement, coalescing, generation rejection, re-arm, and lease release.

### Core Graphics provider

The single AppKit consumer takes the mailbox's newest lease on the coalesced main-thread notification, creates a direct-access `CGDataProvider` over the existing frame bytes, and routes the resulting `CGImage` to the current slide pane. Each of the two panes retains at most one `CGImage`. The provider's release callback owns and releases an opaque frame lease, ensuring that the backing pool buffer remains alive for as long as Core Graphics may read it. `draw(_:)` renders the pane's current image; it does not dequeue frames or perform camera work.

This is an application-side no-copy handoff: the bridge does not allocate a second pixel buffer or rewrite the frame. It is **not** a claim that Core Graphics, the window compositor, or the display hardware performs no internal transfer. Any such platform work is outside the application-controlled acquisition path and must be measured.

Use exact width, height, stride, pixel format, significant depth, and byte-order semantics. Preserve the existing preview interpretation: RGB24 as RGB and RAW8/RAW16 as preview grayscale; do not add demosaicing or precision conversion in the stream path.

Frame consumption is separate from `draw(_:)`: the main-thread notification transfers the newest lease from the mailbox and constructs the `CGImage`; drawing only renders the selected pane's retained image. Replacing an image in one pane must release that pane's prior provider lease promptly. Switching the live destination, stopping acquisition, closing the camera, or maximizing a pane must not clear either slide image. Do not retain a sequence of prior `CGImage` objects. Test provider release timing with two retained slide images against the four-frame pool to rule out pool starvation.

## 5. Two-slide presentation model

The main workspace has two persistent slide panes (Slide 1 and Slide 2) arranged
side by side in a resizable AppKit split view. Either pane can temporarily occupy
the full preview region and then return to the split layout. The user can resize
the pane divider independently of the window size.

There is still exactly one camera session, one acquisition worker, one latest-frame
mailbox, and one AppKit mailbox consumer. The two panes must **not** each register
their own `onFrameAvailable` callback or independently call `takeLatestFrame()`;
the bridge exposes one callback property and one mailbox, so competing consumers
would race and one pane could steal frames from the other. The single callback is
asserted onto the main actor at the bridge's documented main-queue boundary.

The single preview owner routes each consumed frame to the currently selected
live destination. Each slide pane retains only its own most recently displayed
`CGImage`; the image provider retains that frame's immutable lease. Changing the
live destination does not clear or replace the other pane's image. New frames
replace the image only in the selected destination pane. Stop/Close and pane
maximize/restore also do not clear the retained slide images.

The bottom information region is a user-resizable horizontal split: editable
quick-access feature controls are on the left; read-only camera, resolution,
pixel-format, acquisition-state, and live-destination information are on the right.
Quick controls are promoted only from supported readable/writable numeric feature
descriptors; the full descriptor editor remains available in the Camera Controls
window.

**Current hardware boundary:** selecting Slide 1 or Slide 2 currently changes
only the UI's live-frame destination. It does not physically move the microscope
stage, actuate a slide changer, or change the camera session. A future controller
operation must perform the physical switch and acknowledge the resulting slide
position. The controller's acknowledged position, not an optimistic view-local
selection, must become authoritative when hardware switching is integrated.

**Frame-pool budget:** the current driver pool has four slots. The two slide panes
may retain at most two displayed frame leases; the latest-frame mailbox may retain
one more, leaving one slot for the acquisition worker. There must be no image
history or additional retained frame queue. Sustained two-slide streaming must
verify that Core Graphics releases replaced providers promptly and that the pool
does not starve or increase drops.

## 6. Notification and thread model

The acquisition callback publishes to the C++ mailbox and returns. It does not directly update a Swift property.

When the mailbox transitions to pending, the native adapter schedules one main-thread callback. The AppKit view consumes the latest lease in that callback, builds the direct-access `CGImage`, and requests a redraw. Further camera frames overwrite the mailbox slot while the notification is pending; they do not add more main-thread work items. The display image is not stored as per-frame SwiftUI state.

The view must tolerate being detached, resized, hidden, or closed while acquisition continues. Closing a window may stop presentation, but it must not implicitly close the camera unless an explicit application lifecycle command requests that action. Application termination explicitly queues camera Close and waits for its completion before exiting.

Camera lifecycle and feature operations execute on one serialized non-UI command executor. Their result callbacks are marshaled to the main actor only after the operation completes. The acquisition worker never waits for that executor or for the UI.

## 7. Build and dependency boundaries

- Add an opt-in `AMSCOPE_BUILD_NATIVE_MAC_APP` option, default OFF.
- The native target links `Amscope::Driver` and the minimal Apple frameworks it uses.
- The native target does not link Qt Core, Qt Widgets, `Amscope::Qt`, or `Camera::QtWidgets`.
- Existing Qt options and targets remain available and unchanged until native parity is established.
- Keep Swift/Objective-C++ enabled only for the native target configuration. CMake has supported Swift with the Ninja generator since 3.15; the project's 3.21 minimum is sufficient. The native app deliberately requires Ninja rather than the Xcode generator, so the standalone Command Line Tools package is enough and full Xcode / `xcodebuild` is not required. The shell targets macOS 10.15 and uses an AppKit `NSApplicationDelegate` / `NSWindow` lifecycle with SwiftUI hosted by `NSHostingController`, avoiding the macOS 13 SwiftUI `Window` scene requirement. The Objective-C++ bridge and bridging header are in place.
- Prefer the narrow Objective-C++ facade over exposing the complete C++ type graph to Swift. This keeps C++ templates, STL ownership, and vendor types out of Swift's source interface.

## 8. Performance acceptance gates

“Streaming performance is unchanged” is an acceptance condition, not an assumption.

Compare the validated Qt baseline and the native app on the same Mac, camera, USB connection, resolution, pixel format, exposure, and runtime configuration. Record build configuration and environmental conditions.

Required observations:

| Dimension | Acceptance condition |
|---|---|
| Camera session | One `HAmcam` owner; no duplicated open session |
| Acquisition path | No changes to the SDK callback, event queue, acquisition worker, or pull loop |
| Pulled frames | Sustained `framesPulled/sec` is not lower than baseline beyond measured run-to-run noise |
| Camera drops | No additional camera-side drops under equivalent conditions |
| Callback cost | No UI, conversion, blocking, or unbounded allocation added to the acquisition callback |
| Frame memory | No application-side pixel copy; no growth in outstanding frame leases |
| Mailbox | One latest slot; at most one pending render notification |
| Preview | Measures actual presented FPS and frame age separately from camera pull rate |
| Stability | No pool starvation, stale-session publication, use-after-free, or unbounded memory growth |

The hardware comparison must include a sustained live session, not merely a UI launch or a synthetic frame test. Report camera acquisition rate and display presentation rate separately: a display may intentionally skip intermediate frames while the camera continues pulling every frame.

Do not claim parity until the user's Mac has run the same baseline and native-app hardware procedure. No build or hardware test is implied by this architecture document.

## 9. Explicit non-goals for v0.8

- No second camera backend, native SDK wrapper, or second `HAmcam` owner.
- No changes to the vendor callback, pull worker, frame pool, or pixel-storage format.
- No per-frame image conversion, demosaicing, CPU copy, Swift `Data`, or image serialization.
- No `CVDisplayLink` polling loop, timer-driven frame polling, or independent acquisition loop.
- No Metal/IOSurface rewrite in the first pass. A GPU path is considered only after the direct-access AppKit path is correct and measured.
- No Qt styling experiment or FluentQt component reuse.
- No node editor, graph editor, or unrelated image-processing backend.

## 10. References

- Apple, [Creating a direct-access Core Graphics data provider](https://developer.apple.com/documentation/coregraphics/cgdataprovider/init%28datainfo%3Adata%3Asize%3Areleasedata%3A%29).
- Apple, [NSViewRepresentable](https://developer.apple.com/documentation/swiftui/nsviewrepresentable).
- Apple, [Importing Objective-C into Swift](https://developer.apple.com/documentation/swift/importing-objective-c-into-swift).
- CMake, [Swift language support](https://cmake.org/cmake/help/latest/command/enable_language.html) and [Xcode generator](https://cmake.org/cmake/help/latest/generator/Xcode.html).
