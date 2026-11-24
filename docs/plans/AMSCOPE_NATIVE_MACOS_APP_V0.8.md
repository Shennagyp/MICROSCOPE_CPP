# Native macOS App v0.8 — Implementation Plan

**Branch:** `amscope-native-app-mac.v.0.8`  
**Base branch:** `amscope-native-driver-0.7`  
**Current state:** Branch created from the validated driver baseline. The opt-in SwiftUI/AppKit app target, Objective-C++ bridge, serialized native lifecycle controller, common latest-frame mailbox, and direct-access AppKit preview are committed. Local build, visual inspection, feature parity, and hardware performance validation remain pending.

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
- [ ] Build and launch the native app on the user's Mac using the standalone Command Line Tools toolchain.
- [ ] Verify a native-only configuration with Qt disabled; do not remove the Qt targets from the repository.

**Current Phase 1 note:** The application is connected to the real C++ camera controller, but has not yet been built or run on the user's Mac. CMake Swift support is used with Ninja, avoiding the Xcode generator and its `xcodebuild` dependency.

**Gate:** The native application bundle builds and launches independently of Qt, without changing existing driver code.

## Phase 2 — Framework-neutral camera controller

- [x] Introduce a native C++ controller owning exactly one `AmcamCamera` instance.
- [x] Serialize enumeration and lifecycle commands (open, close, start, stop) through a bounded FIFO command worker.
- [x] Include request IDs and session generations in operation completions; reject stale frame publication through the acquisition generation gate.
- [x] Keep blocking camera commands and all completion callbacks off the main actor; the Objective-C++ bridge marshals completion blocks to the main thread.
- [x] Keep the native controller independent of `QtAmcamCamera`; it calls the same C++ driver directly.
- [ ] Port the generic feature descriptor/read/write API and the supported accessory command/telemetry API.
- [ ] Add controller tests for FIFO ordering, queue saturation, shutdown, generation changes, failed Start/Stop recovery, and stale completions.

**Gate:** Lifecycle and feature-request tests cover FIFO ordering, shutdown, cancellation, generation changes, failed Start/Stop recovery, and stale completion rejection.

## Phase 3 — Shared latest-frame mailbox

- [x] Move the existing `FrameStreamMailbox` implementation into `Camera/Common` as `camera::FrameStreamMailbox`.
- [x] Keep the former Qt header as a source-compatibility alias and switch `QtAmcamCamera` to the common type directly.
- [ ] Use this same single-slot mailbox in the native controller.
- [x] Preserve atomic publication, generation validation, notification coalescing, race-safe consume/re-arm, and prompt release of replaced frame leases.
- [x] Add framework-neutral tests for latest-frame selection, coalescing, stale-generation rejection, re-arm, and lease release; retain the existing Qt concurrency/affinity test.

**Gate:** Both frontends use the same mailbox implementation. Existing Qt behavior remains unchanged.

## Phase 4 — AppKit preview surface

- [x] Implement a custom AppKit `NSView` hosted by SwiftUI through `NSViewRepresentable`.
- [x] Invalidate the view through a coalesced main-thread notification from the native bridge; no per-frame SwiftUI observable state is created.
- [x] Consume the newest lease from the mailbox and release the prior displayed image when replaced or when acquisition stops.
- [x] Build each Core Graphics image through a direct-access `CGDataProvider` whose release callback owns the opaque frame lease.
- [x] Keep no history of frames/images and perform no application-side pixel-buffer copy.
- [ ] Validate RGB24, RAW8, RAW16, row stride, byte order, dimensions, invalid buffers, view teardown, and provider lifetime on the user's Mac.
- [ ] Measure frame-pool availability under sustained preview and confirm no starvation.

**Gate:** Preview correctness and lifetime tests pass; no pixel-buffer copy is introduced by application code; the four-frame pool does not starve during sustained display.

## Phase 5 — Native UI and control workflows

- [x] Build the main live-preview window, native application menu, and dedicated camera-controls window.
- [x] Keep camera discovery and lifecycle controls accessible through the File menu.
- [x] Add the bottom information bar with camera, resolution, pixel format, and acquisition state.
- [x] Update SwiftUI metadata only when dimensions or pixel format change, not on every frame.
- [x] Prevent overlapping lifecycle requests and selection changes while a camera session is active.
- [ ] Populate imaging and hardware controls from the existing feature descriptor model.
- [ ] Make only writable information-bar values (for example exposure/gain when supported) double-click editable and commit them through the existing feature model.
- [ ] Preserve and verify explicit camera lifecycle behavior when the window is hidden or closed.

**Current limitation:** The native UI currently has camera discovery and lifecycle controls only. Feature settings, accessory workflows, and editable imaging values are not yet bridged.

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
