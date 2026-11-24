# Native macOS Application Architecture (v0.8)

**Branch:** `amscope-native-app-mac.v.0.8`  
**Base:** `amscope-native-driver-0.7` (`17067f3c5b10a4bb0e748297bbd34ada295bcc43`)  
**Status:** Architecture baseline and initial SwiftUI/AppKit shell committed. Camera integration and macOS build/hardware validation are still pending.

## 1. Objective

Introduce a native macOS application using **SwiftUI for the application shell and controls, AppKit for the high-rate preview surface, and the existing C++ Amscope driver for camera ownership and acquisition**.

This is a frontend migration, not a camera-backend rewrite. The existing Qt preview remains available as a separate target until the native application passes the same functional and hardware performance gates.

The v0.8 user-interface structure preserves the established interaction requirement:

- The main window is centered on the live image.
- Camera and imaging controls are exposed through the **File** menu, which opens the native controls window.
- Acquisition and image metadata are shown in a compact bottom information bar.
- An information-bar value can be double-clicked to edit that value where it is writable.
- The preview is a purpose-built AppKit view hosted in SwiftUI, not a SwiftUI `Image` fed by a per-frame observable property.

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
5. The acquisition callback must not call Swift, enter `MainActor`, construct an `NSImage`, create a `Data` copy, convert pixels, or wait for rendering.
6. Presentation is **latest-frame-wins**, not an unbounded FIFO. If rendering falls behind, old preview frames are replaced; acquisition continues.
7. Session-generation validation occurs before and after mailbox publication. A stale session cannot republish into the current view.
8. There is no new per-frame SwiftUI state update, per-frame main-queue block, timer, display polling loop, or additional frame queue.
9. No UI control performs blocking camera I/O on the main thread.

The current acquisition implementation uses a four-frame pool. The native frontend must therefore keep downstream frame retention bounded: no history of frame leases, no array of pending frames, and no long-lived retention of obsolete preview images.

## 3. Layering and ownership

### Native application shell — SwiftUI

Owns macOS window composition, menus, control-window presentation, settings, user interaction, and low-frequency application state.

SwiftUI observes camera state, feature values, errors, and command completions. It does **not** observe every incoming frame.

### Preview surface — AppKit

A custom `NSView`, integrated through `NSViewRepresentable`, owns the rendering lifecycle. It consumes the latest published frame only when AppKit schedules a draw.

Do not route frames through SwiftUI `Image`, `@Published` frame properties, `NSBitmapImageRep`, or `Data` as an intermediate pixel container. Those abstractions obscure ownership and can introduce copying or excessive state invalidation.

### Objective-C++ boundary

A narrow Objective-C++ facade adapts Swift-facing methods and callbacks to the C++ camera controller. It must not expose vendor SDK types, `QObject`, `std::vector`, `std::shared_ptr`, or other complex C++ implementation types to Swift.

The bridge exposes:
- typed lifecycle and camera-control commands;
- low-frequency state and command results;
- opaque retained frame-lease handles and read-only frame metadata;
- one coalesced frame-available notification.

Swift does not directly own or call `HAmcam`.

### C++ camera runtime

The native frontend reuses `Amscope::Driver` and its existing `AmcamCamera` session owner.

The native-facing asynchronous command controller must preserve the existing Qt adapter's behavioral contract: serialized lifecycle and feature operations, FIFO mutation ordering, request IDs, session generations, stale-completion rejection, and callback delivery outside the acquisition worker. This controller is a **replacement for the Qt-facing controller in the native executable**, not a second controller layered on top of `QtAmcamCamera`.

The native application must not link or instantiate `QtAmcamCamera`. The Qt adapter and native controller are separate frontend adapters over the same driver, and they are never active as two owners of one camera session.

## 4. Frame handoff and rendering lifetime

The existing `camera::qt::FrameStreamMailbox` is a single-slot, atomic latest-frame mailbox with a notification-pending latch. Its implementation is framework-independent despite its current include path. The native frontend must reuse this behavior, not write a competing queue.

The mailbox contract is:

- publish by moving the immutable `FramePtr` lease;
- atomically replace the previous latest frame;
- recheck the acquisition generation after publication;
- schedule a notification only when the pending latch transitions from false to true;
- consume the latest lease and clear/re-arm the notification latch with the existing race-safe recheck.

As part of implementation, move this mailbox to a backend-neutral `Camera/Common` header and keep the existing Qt include as a compatibility alias. Both frontends must use the same implementation and the same tests.

### Core Graphics provider

The AppKit view should create a direct-access `CGDataProvider` over the existing frame bytes. The provider's release callback owns and releases an opaque frame lease, ensuring that the backing pool buffer remains alive for as long as Core Graphics may read it.

This is an application-side no-copy handoff: the bridge does not allocate a second pixel buffer or rewrite the frame. It is **not** a claim that Core Graphics, the window compositor, or the display hardware performs no internal transfer. Any such platform work is outside the application-controlled acquisition path and must be measured.

Use exact width, height, stride, pixel format, significant depth, and byte-order semantics. Preserve the existing preview interpretation: RGB24 as RGB and RAW8/RAW16 as preview grayscale; do not add demosaicing or precision conversion in the stream path.

A draw consumes the newest available frame. Replacing the displayed lease must release the old lease promptly. Disable implicit image animations and do not retain a sequence of prior `CGImage` objects. Test provider release timing against the four-frame pool to rule out pool starvation.

## 5. Notification and thread model

The acquisition callback publishes to the C++ mailbox and returns. It does not directly update a Swift property.

When the mailbox transitions to pending, the native adapter schedules one main-thread invalidation. The AppKit view consumes the latest lease during its draw. Further camera frames overwrite the mailbox slot while the invalidation is pending; they do not add more main-thread work items.

The view must tolerate being detached, resized, hidden, or closed while acquisition continues. Closing a window may stop presentation, but it must not implicitly close the camera unless an explicit application lifecycle command requests that action.

Camera lifecycle and feature operations execute on one serialized non-UI command executor. Their result callbacks are marshaled to the main actor only after the operation completes. The acquisition worker never waits for that executor or for the UI.

## 6. Build and dependency boundaries

- Add an opt-in `AMSCOPE_BUILD_NATIVE_MAC_APP` option, default OFF.
- The native target links `Amscope::Driver` and the minimal Apple frameworks it uses.
- The native target does not link Qt Core, Qt Widgets, `Amscope::Qt`, or `Camera::QtWidgets`.
- Existing Qt options and targets remain available and unchanged until native parity is established.
- Keep Swift/Objective-C++ enabled only for the native target configuration. CMake has supported Swift with the Ninja generator since 3.15; the project's 3.21 minimum is sufficient. The native app deliberately requires Ninja rather than the Xcode generator, so the standalone Command Line Tools package is enough and full Xcode / `xcodebuild` is not required. The shell targets macOS 10.15 and uses an AppKit `NSApplicationDelegate` / `NSWindow` lifecycle with SwiftUI hosted by `NSHostingController`, avoiding the macOS 13 SwiftUI `Window` scene requirement. Add the explicit Objective-C++ bridge when camera-controller integration begins.
- Prefer the narrow Objective-C++ facade over exposing the complete C++ type graph to Swift. This keeps C++ templates, STL ownership, and vendor types out of Swift's source interface.

## 7. Performance acceptance gates

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

## 8. Explicit non-goals for v0.8

- No second camera backend, native SDK wrapper, or second `HAmcam` owner.
- No changes to the vendor callback, pull worker, frame pool, or pixel-storage format.
- No per-frame image conversion, demosaicing, CPU copy, Swift `Data`, or image serialization.
- No `CVDisplayLink` polling loop, timer-driven frame polling, or independent acquisition loop.
- No Metal/IOSurface rewrite in the first pass. A GPU path is considered only after the direct-access AppKit path is correct and measured.
- No Qt styling experiment or FluentQt component reuse.
- No node editor, graph editor, or unrelated image-processing backend.

## 9. References

- Apple, [Creating a direct-access Core Graphics data provider](https://developer.apple.com/documentation/coregraphics/cgdataprovider/init%28datainfo%3Adata%3Asize%3Areleasedata%3A%29).
- Apple, [NSViewRepresentable](https://developer.apple.com/documentation/swiftui/nsviewrepresentable).
- Apple, [Importing Objective-C into Swift](https://developer.apple.com/documentation/swift/importing-objective-c-into-swift).
- CMake, [Swift language support](https://cmake.org/cmake/help/latest/command/enable_language.html) and [Xcode generator](https://cmake.org/cmake/help/latest/generator/Xcode.html).
