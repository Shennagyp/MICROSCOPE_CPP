# Native macOS App v0.8 — Implementation Plan

**Branch:** `amscope-native-app-mac.v.0.8`  
**Base branch:** `amscope-native-driver-0.7`  
**Current state:** Branch created from the validated driver baseline. Architecture and performance criteria are committed. The opt-in app target and a non-streaming SwiftUI/AppKit shell are in place; camera integration and local build validation remain pending.

## Phase 0 — Baseline and architecture

- [x] Create the feature branch from `amscope-native-driver-0.7`.
- [x] Inspect the C++ driver, Qt adapter, frame mailbox, frame lease, and current Qt frame renderer.
- [x] Record the streaming invariants and native app architecture.
- [ ] Capture a repeatable baseline on the user's Mac for the same camera/resolution/pixel format used by the native app.
- [ ] Record sustained pull rate, event rate, camera drops, callback duration if instrumented, preview presentation rate, CPU, and memory.

**Gate:** The baseline evidence is recorded before making changes to the driver or frame path.

## Phase 1 — Native app target and launch shell

- [x] Add an opt-in `AMSCOPE_BUILD_NATIVE_MAC_APP` target; it is OFF by default and requires the Ninja generator, not full Xcode.
- [x] Add the initial SwiftUI app shell, AppKit-managed native main window, File-menu entry, and dedicated controls window hosted by `NSHostingController`.
- [x] Set the initial deployment target to macOS 10.15; avoid the newer SwiftUI `Window` scene API.
- [x] Add an AppKit `NSViewRepresentable` preview placeholder; no camera frames are connected yet.
- [ ] Add the Objective-C++ bridge boundary without opening the camera.
- [ ] Build and launch the native app on the user's Mac using the standalone Command Line Tools toolchain.
- [ ] Verify a native-only configuration with Qt disabled; do not remove the Qt targets from the repository.

**Current Phase 1 note:** The preview and controls window are scaffolding only. No camera session is created, no stream is consumed, and no build, CTest, or hardware run has been performed for this branch. CMake Swift support is used with Ninja, avoiding the Xcode generator and its `xcodebuild` dependency.

**Gate:** The native application bundle builds and launches independently of Qt, without changing existing driver code.

## Phase 2 — Framework-neutral camera controller

- Introduce a C++ controller for native application lifecycle and feature requests.
- Preserve one `AmcamCamera` instance, one native handle per session, FIFO command execution, request IDs, session generations, and stale-completion rejection.
- Move blocking camera commands off the main actor.
- Ensure the controller is not layered on top of `QtAmcamCamera`; the native executable uses the direct C++ driver/controller path.
- Keep all controller callbacks and command results separate from acquisition callbacks.

**Gate:** Lifecycle and feature-request tests cover FIFO ordering, shutdown, cancellation, generation changes, failed Start/Stop recovery, and stale completion rejection.

## Phase 3 — Shared latest-frame mailbox

- [x] Move the existing `FrameStreamMailbox` implementation into `Camera/Common` as `camera::FrameStreamMailbox`.
- [x] Keep the former Qt header as a source-compatibility alias and switch `QtAmcamCamera` to the common type directly.
- [ ] Use this same single-slot mailbox in the native controller.
- [x] Preserve atomic publication, generation validation, notification coalescing, race-safe consume/re-arm, and prompt release of replaced frame leases.
- [x] Add framework-neutral tests for latest-frame selection, coalescing, stale-generation rejection, re-arm, and lease release; retain the existing Qt concurrency/affinity test.

**Gate:** Both frontends use the same mailbox implementation. Existing Qt behavior remains unchanged.

## Phase 4 — AppKit preview surface

- Implement a custom AppKit `NSView` hosted by SwiftUI through `NSViewRepresentable`.
- Invalidate the view through one coalesced main-thread notification, not one Swift task or dispatch block per camera frame.
- Consume the newest lease when drawing.
- Build the Core Graphics image through a direct-access `CGDataProvider` with a release callback that owns the opaque frame lease.
- Validate RGB24, RAW8, RAW16, row stride, byte order, dimensions, invalid buffers, view teardown, and provider lifetime.
- Retain no history of old frames/images; avoid SwiftUI frame state and implicit image animations.

**Gate:** Preview correctness and lifetime tests pass; no pixel-buffer copy is introduced by application code; the four-frame pool does not starve during sustained display.

## Phase 5 — Native UI and control workflows

- Build the main live-preview window, native application menu, and dedicated camera-controls window.
- Keep camera controls accessible through the File menu as previously specified.
- Add the bottom image/acquisition information bar.
- Permit double-click editing only for writable information-bar values; validate edits through the existing camera feature model.
- Keep low-frequency camera state and feature results in SwiftUI state; do not bind frame frequency to the SwiftUI view hierarchy.
- Preserve explicit camera lifecycle behavior when the window is hidden or closed.

**Gate:** All controls use asynchronous controller requests; no camera call blocks the main actor; UI state cannot issue stale commands to a newly selected device.

## Phase 6 — Functional parity and application lifecycle

- Port the required discovery, open/close, start/stop, feature controls, telemetry, and supported accessories.
- Verify close/stop sequencing, application termination, view teardown, device disconnect, reconnect, and stale callback behavior.
- Keep the Qt application as a separate fallback while parity remains incomplete.

**Gate:** The native UI covers the existing supported camera workflows selected for v0.8 without changing driver semantics.

## Phase 7 — Streaming performance and hardware sign-off

- Run the same sustained hardware scenario on the Qt baseline and native app.
- Compare `framesPulled/sec`, image events, camera-side drops, preview presented FPS, frame age, CPU, memory, and lease/pool behavior.
- Repeat enough runs to identify normal run-to-run variation.
- Verify no per-frame SwiftUI update, unbounded queue, frame history, extra acquisition worker, or application-side pixel copy exists.
- Fix any regression before treating the native app as the preferred frontend.

**Gate:** No measurable acquisition regression beyond baseline noise, no additional camera drops, stable bounded memory, no frame-pool starvation, and correct rendered image formats.

## Implementation rules

- Do not change `AmcamCamera.cpp`, the SDK callback, event queue, pull loop, or frame-pool layout for UI convenience.
- Do not add a second camera backend or a second acquisition thread.
- Do not connect the acquisition callback directly to Swift or `MainActor`.
- Do not publish every frame through SwiftUI observation.
- Do not create a second pixel buffer using `Data`, `NSBitmapImageRep`, or equivalent.
- Do not introduce Metal/IOSurface until the AppKit direct-access route is correct and its actual bottleneck has been measured.
- Do not mark a performance gate complete without user-run hardware evidence.
