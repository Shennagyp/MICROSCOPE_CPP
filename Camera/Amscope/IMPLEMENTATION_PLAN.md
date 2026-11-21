# AmScope Driver Implementation Plan

**Status:** Proposed implementation plan  
**Target repository:** `MICROSCOPE_CPP`, branch `main`  
**Scope:** Build the AmScope camera driver and Qt integration around the native Amcam SDK, using a separate high-throughput acquisition path and a control-plane abstraction.

This plan is based on the vendor C++ samples currently committed under `Camera/Amscope/samples`. It is not a claim that an application driver, SDK package, or application-level build has already been implemented.

## 1. Architectural decision

Implement AmScope as a native C++ backend with Qt presentation. Keep camera controls in the general camera-control abstraction, but keep frame acquisition out of that abstraction.

The camera session remains the single authority for the opened device and native SDK handle. Its responsibilities are separated into two independently usable surfaces:

- **Control plane:** enumerate/open/close, capabilities, resolution, exposure, gain, white balance, trigger configuration, options, and device state.
- **Acquisition plane:** start/stop streaming, receive/pull frames, manage buffers, attach frame metadata, and publish frames to consumers.

These are not two independent camera sessions. Both operate through one session owner and one `HAmcam` handle. The acquisition implementation must not open a second handle for the same device.

The backend selector chooses Amcam or GenTL at the driver boundary. The Amcam backend calls the Amcam SDK directly; it must not pretend that the SDK is a GenTL Producer or require an Amcam `.cti` file.

## 2. What the committed samples establish

The samples are valuable references for native API behavior and feature coverage. Their presentation and threading choices are not the production architecture.

| Sample | What it establishes | How to use it |
|---|---|---|
| `demosimplest` | `Amcam_StartPullModeWithCallback` and `Amcam_PullImageV4` inside the SDK callback | Reference for callback signature, event handling, and frame-info retrieval |
| `demowait` | Starts pull mode with a null callback, then waits for frames using `Amcam_WaitImageV3` in a loop | Primary candidate for an application-owned acquisition worker |
| `demoqt` | Qt controls, device selection, exposure/gain, white balance, still capture, FPS display | Reference for UI controls only; do not copy its UI-thread frame retrieval |
| `demomulti` / `demotwoqt` | Multiple handles and per-camera callback contexts | Reference for per-device ownership and future multi-camera support |
| `demoraw` | Raw output, bit depth, ROI, still-frame path | Reference for pixel formats and raw acquisition options |
| `demosofttrigger` / `demoexternaltrigger` / `demotriggerout` / `demotriggersync` | Trigger and I/O operations | Reference for trigger capability exposure |
| `demostill` / `demostillraw` | Still-image capture and separate still-image events | Reference for still capture as a distinct frame operation |
| `democfg` | Configuration-file open path | Reference for vendor configuration options |

### 2.1 Acquisition findings

The Qt sample does the following:

```text
Amcam SDK callback
    -> emit Qt signal
        -> Qt UI event loop
            -> Amcam_PullImage(...)
                -> QImage conversion/scaling
                    -> QLabel/QPixmap
```

This makes the UI event loop part of frame retrieval. It is a demonstration pattern, not the target high-throughput path.

The console callback examples retrieve the frame with `Amcam_PullImageV4` directly inside the SDK callback. This proves that the vendor's own C++ samples use callback-driven pull retrieval, but does not by itself prove every callback-thread or buffer-lifetime guarantee needed by a production pipeline.

Most importantly, `demowait` demonstrates another path:

```cpp
Amcam_StartPullModeWithCallback(hcam, nullptr, nullptr);

while (running)
{
    AmcamFrameInfoV3 info = {};
    HRESULT hr = Amcam_WaitImageV3(
        hcam, 1000, imageBuffer, 0, 24, 0, &info);
    // Handle timeout, error, or completed frame.
}
```

This is the first path to investigate for the production acquisition worker. It can keep frame waiting and retrieval outside the Qt UI loop. It must not be assumed safe for concurrent control calls until SDK thread-safety and wait semantics have been verified.

## 3. Target architecture

```text
                         Qt Application
                    controls          presentation
                       |                    ^
                       v                    |
              General Camera Controls       |
                       |                    |
                Camera Session Owner        |
                       |                    |
                one HAmcam handle           |
                   /                       |
                  /                        |
           Control Adapter   Acquisition Engine
                  |            |
             Amcam SDK      Wait/Pull Strategy
                               |
                         Frame Buffer Pool
                               |
                         Frame Publication
                               |
                      Bounded Frame Interface
                               |
                      Qt GPU / Render Path
```

### Ownership rules

1. `AmscopeSession` is the only owner of the native handle and its open/closed lifecycle.
2. Control and acquisition components are session-owned collaborators; neither independently opens or closes the handle.
3. The acquisition engine owns the acquisition worker, its stream state, and the buffers used for frame delivery.
4. The Qt UI does not call `Amcam_WaitImageV3`, `Amcam_PullImageV4`, or other frame-retrieval functions.
5. GPU resource creation, upload, and rendering occur on the selected Qt render/GPU execution path, not in the SDK callback.
6. Consumers receive a frame lease or equivalent lifetime-safe view; raw pointers into a reusable camera buffer must never escape without an ownership guarantee.

### Interface boundary

Keep the general camera abstraction focused on controls and device/session state. Represent acquisition as a separate capability or service, for example:

- `ICameraControls`: common control operations and read-back state.
- `ICameraFrameStream`: independent frame-stream lifecycle and frame publication.
- `AmscopeSession`: owns the single Amcam handle and coordinates both collaborators.
- `AmscopeControls`: maps common controls to the native Amcam calls.
- `AmscopeAcquisition`: implements the Amcam-specific acquisition strategy.
- `FrameLease` / `FrameMetadata`: common frame ownership and metadata contract.

The names are proposed; align them with the eventual project naming conventions before implementation. Do not create one oversized `ICamera` interface that puts frame retrieval back into the control abstraction.

## 4. Acquisition strategy: decide from the SDK contract

### Candidate A — dedicated worker using `Amcam_WaitImageV3`

This is the preferred prototype because it is demonstrated by the vendor's `demowait` sample and naturally separates acquisition from Qt.

The acquisition worker would:

1. Start the camera's pull mode with no event callback, as shown in `demowait`.
2. Wait for a frame using a finite timeout.
3. Pull into a buffer acquired from the frame pool.
4. Attach returned frame information and host-side timing.
5. Publish the buffer as an immutable frame lease.
6. Repeat until a stop request or terminal SDK error.

Before selecting this approach, verify:

- Whether `Amcam_WaitImageV3` is intended to be called from an application-owned worker thread.
- Whether finite timeout reliably bounds stop latency.
- Whether the wait call can coexist with exposure, gain, white-balance, resolution, trigger, and status calls on the same handle.
- Whether `WaitImageV3` exposes adequate frame metadata for the required pipeline.
- Whether a newer wait API is declared by the installed SDK header. Do not invent a `WaitImageV4` entry point unless it exists in that SDK.

A blocking wait must not hold a session mutex in a way that prevents control operations or shutdown from progressing. If the SDK is not safe for concurrent calls, define a single SDK-call ownership strategy and bound command latency using the SDK's supported wait timeout or another documented cancellation mechanism.

### Candidate B — SDK callback with `Amcam_PullImageV4`

Use this path if the wait API is unsuitable or does not provide the required semantics.

The SDK callback must follow the vendor ABI and use a stable per-session callback context. The callback must never call Qt UI methods, scale/convert images, save files, or perform image processing.

Two variants require SDK verification:

- **Callback-owned pull:** call `Amcam_PullImageV4` in the callback, as the vendor's console samples do; copy or lease the completed data into the bounded frame-publication path and return promptly.
- **Callback-notifies-worker:** callback only posts a lightweight signal, and a dedicated worker performs the pull. Use this only if the SDK contract guarantees that the frame remains available and correctly associated with the notification after the callback returns.

Do not choose callback-notifies-worker merely because it seems cleaner. It depends on the SDK's event queue, frame retention, and callback timing guarantees.

### Selection rule

Prototype both viable choices with the exact macOS SDK and target camera. Select based on sustained delivered frame rate, frame sequence behavior, control responsiveness, CPU cost, stop/restart reliability, and error recovery—not on API naming or theoretical assumptions.

Do not introduce Push mode unless it is present in the installed Amcam header and its buffer ownership/thread contract is understood.

## 5. Frame memory and delivery

The acquisition path must avoid per-frame allocation and unbounded queuing.

### Frame storage

- Use a bounded reusable buffer pool sized for the active resolution and pixel format.
- Compute capacity and row stride using the SDK's documented rules and selected pixel format. The sample's RGB24 / `TDIBWIDTHBYTES` allocation is not universal.
- Carry the actual width, height, stride, payload size, pixel format, frame timestamp/sequence (when supplied), host receive time, and configuration generation.
- A buffer is immutable while published. Return it to the pool only after all consumers release the frame lease.
- Rebuild or resize the pool only at a controlled stream reconfiguration boundary.
- Keep raw and converted pixel paths distinct. Do not force RGB conversion into acquisition if the SDK can return a useful raw format and a downstream consumer can handle it.

### Delivery policy

The preview path is freshness-oriented: a bounded latest-frame slot may replace an older frame that has not yet been rendered. It must not build a queue of stale preview frames.

A consumer that needs every frame, such as a later recorder or image-processing pipeline, requires a separate bounded ordered-delivery policy with explicit backpressure and drop reporting. Preview replacement must not silently dictate the loss policy for other consumers.

Do not integrate the separate ImageProcessing project in this milestone. Define only the stable frame hand-off boundary needed for a later integration.

## 6. Qt and GPU presentation

Qt owns UI state and presentation; it is not the acquisition scheduler.

- Keep camera control widgets in the Qt UI and send control requests to the session owner.
- Deliver frame leases to a Qt presentation adapter without one queued UI event per camera frame.
- Use a latest-frame notification or equivalent coalesced presentation mechanism for preview.
- Keep GPU texture/resource ownership on one selected render/GPU thread.
- Do not use `QPixmap` in the acquisition worker. Do not perform `QImage::scaled` or expensive format conversion in the SDK callback.
- Avoid assuming that a QLabel/QPixmap preview is the final high-performance renderer. Select the Qt rendering path after the UI architecture and desired frame rate are known.

A CPU-backed frame generally requires an upload to a GPU resource. Zero-copy GPU import is a separate optimization and must only be claimed if the vendor SDK exposes a compatible macOS shared-memory/surface mechanism and it is verified on the target hardware.

## 7. Session lifecycle and reconfiguration

The past start/stop and cleanup failures make session lifecycle a first-class implementation concern.

Define explicit states, at minimum:

`Closed -> Open -> Configured -> Streaming -> Stopping -> Configured/Open -> Closed`

Also define terminal/disconnected and failed states. All transitions must be serialized by the session owner.

### Stream stop / close

1. Reject new start, control, and reconfiguration requests as appropriate.
2. Request stream termination using the documented SDK mechanism.
3. Wake or cancel a blocking wait using a documented method; do not assume `Amcam_Close` or thread cancellation is sufficient.
4. Wait for the acquisition worker or callback to leave SDK code.
5. Ensure no new frames can be published.
6. Resolve outstanding frame leases and ensure SDK/native buffers are no longer referenced.
7. Release callback context and frame-pool storage.
8. Close the handle exactly once.
9. Publish the final session state.

Stop and close must be idempotent. Partial-start failure, camera disconnect, repeated stop, close during acquisition, and restart after stop must all follow the same ownership rules.

### Reconfiguration

Resolution and pixel-format changes alter buffer capacity and frame interpretation. Use a transaction:

1. Stop publication and acquisition.
2. Quiesce the acquisition call/callback.
3. Resolve outstanding leases or retain old-generation storage until leases finish.
4. Apply the new configuration using the session owner.
5. Query the effective configuration and rebuild the frame pool.
6. Increment the configuration generation.
7. Restart acquisition and publish only frames tagged with the new generation.

Do not free or reuse buffers from an old configuration while a consumer still holds a lease.

## 8. Proposed source layout

The current repository contains vendor examples only. Use this as the initial production layout, adjusting names to the real project conventions when the application structure arrives:

```text
Camera/
  Amscope/
    samples/                 # vendor references; keep separate from production code
    include/
      AmscopeSession.h
      AmscopeControls.h
      AmscopeAcquisition.h
      FrameTypes.h
    src/
      AmscopeSession.cpp
      AmscopeControls.cpp
      AmscopeAcquisition.cpp
      AmcamSdkAdapter.cpp
      FrameBufferPool.cpp
    qt/
      QtFramePresenter.h
      QtFramePresenter.cpp
    tests/
      ...
    CMakeLists.txt           # or the app's established build-system equivalent
```

The vendor SDK itself should be supplied through a configurable SDK root (headers plus platform-specific library/runtime files). Do not copy vendor sample implementations into production code wholesale, and do not commit redistributable SDK binaries unless the vendor license permits it.

The current samples already show the expected linkage patterns: `demoqt.pro` links `-lamcam`; the POSIX sample scripts use `clang++` or `g++` with `-lamcam`; the Visual Studio projects reference `amcam.lib`. The repository tree currently does not contain `amcam.h`, `libamcam`, or an application-level build configuration. SDK path, architecture, and runtime loading must therefore be resolved before the production driver can compile.

## 9. Implementation phases

### Phase 0 — SDK and build baseline

- Confirm target macOS version, CPU architecture (Apple Silicon / Intel / universal), Qt major version, compiler, C++ standard, and current application build system.
- Obtain the exact macOS Amcam SDK package and inspect its `amcam.h`, library binary, exported symbols, and license/redistribution terms.
- Build an unmodified minimal vendor C++ sample against that exact SDK and camera.
- Add SDK discovery/linking to the project build without hardcoding a developer-specific absolute path.
- Preserve the vendor samples as reference-only.

**Exit condition:** a reproducible native Amcam link/runtime baseline exists for the target machine and SDK version.

### Phase 1 — SDK contract and acquisition prototype

- Read the exact header and vendor API documentation for open/close, pull mode, wait mode, callbacks, stop, frame info, raw formats, still capture, and trigger APIs.
- Create a small non-UI prototype for the `WaitImageV3` worker path.
- Create a callback + `PullImageV4` comparison prototype.
- Verify concurrent control operations, timeout behavior, callback quiescence, disconnect behavior, and stop latency.
- Record delivered frame rate and dropped/error counts for both strategies.

**Exit condition:** one acquisition strategy is selected based on documented API guarantees and measurements. No Qt dependency is needed for this decision.

### Phase 2 — Session owner and control plane

- Implement device enumeration and stable device descriptors.
- Implement `AmscopeSession` as the sole owner of one `HAmcam` handle.
- Implement open/close and explicit state transitions.
- Add control mappings for resolution, exposure auto/manual, exposure time, gain, temperature/tint, white-balance-once, trigger/options, and read-back where supported.
- Translate vendor return codes into the project's error model without hiding unsupported operations.
- Define the synchronization policy for SDK calls based on Phase 1 findings.

**Exit condition:** controls and device state operate through one session without involving the frame-rendering path.

### Phase 3 — Acquisition engine and frame contract

- Implement the selected Wait or callback strategy behind `AmscopeAcquisition`.
- Add bounded buffer pool and immutable frame leases.
- Add frame metadata, configuration generation, and frame counters.
- Separate preview freshness policy from ordered consumers that require every frame.
- Ensure the acquisition engine shares the session's native handle and cannot close it independently.

**Exit condition:** a headless native test can stream frames into a bounded consumer without Qt or per-frame heap allocation.

### Phase 4 — Qt presentation and controls

- Connect the common controls to the Qt UI.
- Add a frame presenter that receives frame leases without making the UI event loop pull from Amcam.
- Select the actual Qt GPU/render mechanism and make one render thread own GPU resources.
- Coalesce preview updates and measure time spent in acquire, publish, wait-for-render, upload, and render.
- Keep the UI responsive while acquisition runs at the target rate.

**Exit condition:** sustained preview does not block acquisition or controls, and the application does not accumulate stale frame notifications.

### Phase 5 — Lifecycle, recovery, and camera features

- Verify repeated open/start/stop/restart/close cycles.
- Verify resolution reconfiguration with outstanding frame leases.
- Verify unplug/replug, SDK errors, partial startup failure, and shutdown during frame delivery.
- Add still-image acquisition as a separate path from the continuous preview stream.
- Add software/external trigger, GPIO/I/O, raw/bit-depth, and multi-camera support according to actual device capabilities.
- Use per-session callback/worker contexts; do not use the vendor samples' global singleton state in production.

**Exit condition:** lifecycle operations are repeatable and no stale callback, worker, handle, or buffer survives session closure.

### Phase 6 — Backend routing and integration seam

- Add backend selection between Amcam and GenTL at the driver/provider boundary.
- Share only genuinely common controls, session semantics, frame metadata, and frame-consumer interfaces.
- Keep Amcam SDK calls and GenTL Data Stream operations in their respective implementations.
- Define a future frame consumer seam for the separate ImageProcessing project without linking that project into the camera driver now.

**Exit condition:** the application can select either backend without leaking Amcam types into common code or weakening GenTL semantics.

### Phase 7 — Performance qualification

Measure on the actual target camera and macOS machine:

- SDK-reported frames and application-delivered frames per second.
- Preview-presented frames per second and preview replacement/drop count.
- Capture-to-publication and capture-to-presentation latency.
- CPU utilization, allocations per frame, memory bandwidth, and buffer-pool occupancy.
- Control-call latency while streaming.
- Stability over sustained operation and during configuration changes.

Use these measurements to decide whether later work on SDK push mode, shared GPU memory, pixel-format conversion, or multi-camera scheduling is justified.

**Exit condition:** the measured behavior satisfies the target workload; do not claim maximum FPS or zero-copy without evidence.

## 10. MVP scope and deferred work

### MVP

1. Native Amcam SDK integration and reproducible macOS build.
2. One-camera session owner.
3. Common control plane.
4. Dedicated acquisition path selected from Wait vs callback measurements.
5. Bounded frame pool and frame lease.
6. Qt preview that does not acquire or pull frames on the UI thread.
7. Correct stop/close/reconfigure behavior.

### Defer

- ImageProcessing library integration.
- gRPC or separate camera service.
- Zero-copy GPU import until vendor support is proven.
- Advanced multi-camera synchronization UI.
- Every vendor-specific feature not required by the target camera workflow.

The initial design should remain multi-camera-capable through per-device session ownership, but the first implementation should qualify one camera end-to-end before introducing multi-camera scheduling.

## 11. Acceptance criteria

The implementation is complete for the initial milestone when:

- The app links directly to the native Amcam SDK; Python is not required.
- Amcam and GenTL remain separate backend implementations.
- All camera control calls are reachable through the general control abstraction.
- Frame acquisition is not a method of that general control abstraction.
- One session owns one native handle and coordinates control and acquisition.
- No frame retrieval, scaling, or image processing runs on the Qt UI thread.
- The SDK callback, if used, does not call Qt UI APIs.
- Frame storage is bounded, reused safely, and has explicit ownership.
- Preview does not accumulate stale frames.
- Stop, close, reconnect, and reconfigure are safe under sustained acquisition.
- Frame-rate, latency, and drop behavior are measured on the target hardware.

## 12. Facts still requiring the installed SDK

The samples do not fully specify the production contract. Confirm these from the exact SDK header/documentation and target-device tests:

- Whether `Amcam_WaitImageV3` is supported and recommended for macOS on the target camera model.
- The concurrency contract between wait/pull and control calls on the same handle.
- How a pending wait is interrupted during stop/close.
- Whether `Amcam_Stop` is required before `Amcam_Close` and whether either waits for callback completion.
- Whether callback entry can overlap or be re-entered.
- The exact callback calling convention on macOS and the lifetime of callback context.
- Which frame-info version and pixel formats are supported by the model.
- Whether user-provided buffers, push mode, or GPU-shareable buffers exist in the actual SDK package.
- Whether GigE initialization/discovery calls such as `Amcam_GigeEnable` are required for the target device.

Until these facts are confirmed, keep them as explicit implementation gates rather than hard-coded assumptions.
