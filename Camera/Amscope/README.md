# Amscope native camera driver

This is the reusable, non-Qt C++ AmScope camera provider for the bundled
Amcam SDK. Vendor headers, libraries, documentation, and original samples
remain under `Camera/VendorSDK/AmscopeVendorSDK`. This directory contains the
Camera Module's native provider integration; consuming applications build
their own adapters and presentation without taking ownership of the SDK
session. Vendor sample code is not modified.

## Development roadmap

The governing Camera Module v2.0 roadmap and current implementation status are in
[CAMERA_V2_DEVELOPMENT_STATUS.md](../../docs/architecture/CAMERA_V2_DEVELOPMENT_STATUS.md).
Development continues on `camera-v.2.0`. The reusable camera library is a
**non-Qt programmatic capability**; application GUI and presentation belong to
consuming applications such as Optika.

The original [AmScope Native Driver and Camera Module Completion Plan](../../docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
is retained as the historical Phase 0–8 baseline. The later
[AmScope Native Release Plan](../../docs/architecture/AMSCOPE_NATIVE_RELEASE_PLAN.md)
describes the former Qt-preview delivery and is no longer the governing v2.0
plan. Common/provider-specific type ownership is documented in
[CAMERA_COMMON_TYPE_OWNERSHIP.md](../../docs/architecture/CAMERA_COMMON_TYPE_OWNERSHIP.md).
The detailed implementation inventory is in
[AMSCOPE_IMPLEMENTATION_INVENTORY.md](../../docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md);
the private SDK test-seam design is in
[AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md](../../docs/architecture/AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md);
and the GenTL/GenApi dependency assessment is in
[AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md](../../docs/architecture/AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md).

## Implemented

- One AmcamCamera owns one HAmcam handle for the complete open session.
- Enumeration copies SDK device data into CameraInfo; open uses the opaque
  device id returned by enumeration. Re-enumerate after a disconnect/replug.
- Common discovery fields use `camera::CameraDeviceInfo`: optional
  manufacturer/model/serial/firmware identity, common preview dimensions, and
  a tri-state model capability set. An opaque SDK device id is not assumed to
  be a serial number. Amcam model flags, model code, speed/fan limits, and I/O
  line counts remain in the AmScope-specific `CameraInfo` extension.
- Model-advertised capabilities are distinct from runtime feature access.
  Missing model metadata is `Unknown`; a known absent capability is
  `Unsupported`; neither is inferred from the availability of a UI editor.
- Acquisition starts pull mode with the SDK event callback. The callback only
  enqueues relevant event codes into a fixed-capacity (256) allocation-free
  FIFO; one C++ worker handles events and calls Amcam_PullImageV4.
- E_PENDING is a transient callback/frame-readiness race. Disconnect, generic
  SDK error, no-frame timeout, no-packet timeout and event-queue overflow are
  fatal acquisition conditions.
- Video mode uses the frame-health watchdog: the larger of five seconds or
  three times the last known exposure plus a one-second grace period. Only a
  successfully pulled frame resets that deadline; E_PENDING and image
  notifications alone do not. Triggered modes use the SDK hardware-heartbeat
  event instead, so an idle interval with no external/software trigger is not
  mistaken for a stalled camera. In trigger mode the driver temporarily saves
  and suppresses the SDK no-frame/no-packet timeouts, enables the heartbeat
  subscription, and restores the prior SDK values on return to video mode or
  Close. Heartbeat loss remains fatal. A verified exposure change wakes the
  timed event wait so its video deadline is recalculated immediately.
- Stop and Close are never called from the vendor callback. A failed user Stop
  while Streaming restores the previous stream and retains callback/frame state.
  The Qt frame-delivery gate is reactivated for that same acquisition after a
  recoverable Stop failure; a successful Stop/Close leaves the old gate invalid.
  Fatal acquisition callbacks immediately invalidate their frame gate and
  clear the latest-frame mailbox. Publication checks the generation before and
  after its atomic store; if the generation changed during publication, a CAS
  removes that stale lease without clearing a newer frame. The mailbox uses no lock.
  The fatal session transition is then enqueued into the same FIFO as Stop/Close and feature
  requests. Earlier accepted lifecycle operations run first and may supersede
  the fault notification; later requests bind to the post-fault generation. The
  fatal event has a reserved control slot beyond the normal 256-request limit.
  Successful Stop shuts down the event worker, drains callbacks, then releases
  session resources. Terminal Close remains independent of Stop success.
- Every successful Amcam_Stop marks a both-cache flush obligation
  (AMCAM_OPTION_FLUSH = 3). The driver flushes immediately and retries before
  the next Start if the SDK rejects the first flush. Terminal Close stops the event queue, joins the
  the callback-driven PullImage worker, and calls Amcam_Close directly; it does not depend on Stop/flush.
- The live acquisition mechanism remains SDK callback -> bounded event FIFO ->
  one C++ acquisition worker -> Amcam_PullImageV4. The worker selects the
  payload representation from the stopped-state stream configuration; it does
  not introduce another callback, polling loop, or acquisition worker.
- Processed live frames remain packed RGB24 (rowPitch = -1, stride = width * 3).
  RAW live mode uses Amcam_get_RawFormat for sensor FourCC/significant bit
  depth and AMCAM_OPTION_BITDEPTH for the returned storage container: one byte
  per pixel in 8-bit mode or two bytes per pixel in 16-bit mode, with no row
  padding. Frame metadata keeps both values distinct.
- RAW output enablement remains an Amcam-specific feature. RAW output
  precision is exposed through the backend-neutral
  `camera::RawOutputBitDepthFeature`. It is an integer feature with an
  explicit discrete set of exact significant-bit counts, not relative modes
  or storage widths. The Amcam backend derives this set from the live
  `Amcam_get_PixelFormatSupport` enumeration, retaining every distinct
  significant depth represented by supported RAW/sensor-mosaic pixel formats
  (for example, 8, 10, 12, 14 or 16 bits when those formats are actually
  enumerated by the connected camera). Selecting a depth resolves to a
  supported SDK pixel-format code through `AMCAM_OPTION_PIXEL_FORMAT` and
  verifies the selected format by readback. The selector does not reduce the
  choices to 8-bit and maximum, and it does not invent intermediate depths.
  If the modern pixel-format enumeration is unavailable, the backend does
  not advertise the legacy `AMCAM_OPTION_BITDEPTH` 8-bit/maximum switch as
  though it were a complete discrete selector; exact multi-depth selection
  is reported unavailable instead. This sensor-output precision setting is
  independent of the application's `Processed still` / `RAW still`
  capture-request selector and the separate RAW transport enablement option.
  Frame metadata keeps significant source depth separate from storage width;
  vendor RAW payload bytes are not transformed. After a still capture, the UI
  reports the returned frame's measured significant and storage bit depths
  separately above the capture controls; this readout is not a requested
  setting and is not inferred from the selected RAW mode. The Qt preview
  refuses to interpret RAW sensor bytes as RGB888 until a decoder is attached.
- Live and still payloads use the backend-neutral `camera::Frame` /
  `camera::FramePtr` contract from `Camera/Common`. It carries dimensions,
  row stride, buffer format, opaque format identity, significant source depth,
  storage width, payload ownership, and optional sequence/timestamp/exposure
  metadata. The Amcam SDK validity flags determine whether each metadata value
  is present; a reported zero is retained as a valid value.
- Amcam-only `sdkFlags`, exposure gain percent, and black level remain on the
  typed `amscope::AmcamFrame` extension. RAW pixel identifiers use the
  `amcam.fourcc` scheme and are not relabelled as PFNC. Amcam timestamps and
  per-frame exposure durations are represented in microsecond timebases; the
  timestamp clock origin remains unspecified.
- Four reusable buffers are exposed as immutable common `FramePtr` leases. If
  every buffer is retained, the worker drains into a private discard buffer and
  increments droppedFrames rather than blocking acquisition.
- Resolution can be changed while open and stopped. Start reads the final
  image dimensions and sizes the pool for the selected configuration.
- Exposure auto/manual, exposure time/gain, Temp/Tint white balance, one-shot
  auto white balance, and SDK frame-rate statistics are implemented. ExposureAuto
  maps Off/Continuous/Once to native SDK values 0/1/2; Once is not emulated as
  Continuous. The mode setter verifies immediate SDK readback.
- The programmatic camera API exposes settings only through
  ICameraFeatureAccess; vendor-specific setters/readers are private backend
  primitives. The initial generic control surface includes ExposureAuto,
  ExposureTime, AmcamAnalogGainPercent, AmcamColorMatrixEnabled,
  AmcamPixelFormat, AmcamWhiteBalanceTemperature, AmcamWhiteBalanceTint,
  AmcamWhiteBalanceRGBGain, AmcamFixedFrameRate, AmcamFrameSpeedLevel and
  AmcamResolutionIndex. Additional AmScope-specific image, sensor and hardware
  settings, plus read-only model capability facts, use the same typed contract.
  One-shot commands and still capture remain separate camera operations.
- The AmScope descriptors use model metadata and non-mutating SDK probes.
  One-shot auto white balance remains a command, not a Boolean feature.
- The shared typed feature contract also exposes the probed ISP controls
  (linear tone mapping, curve mode, video/still demosaic, denoise, flat-field
  and dark-field correction, defect-pixel correction, sharpening, orientation,
  test pattern and auto-exposure threshold),
  sensor controls (black level, conversion gain, low-noise and high-full-well
  modes), and hardware controls (sensor temperature, TEC enabled/target/voltage,
  fan speed and anti-fog heater level). Every control is hidden unless its
  model flag/profile policy and non-mutating runtime probe agree.
- Image-pipeline and sensor-mode writes are stopped-state-only to avoid changing
  sensor readout or output interpretation underneath a live stream. TEC, fan
  and anti-fog settings are also stopped-state-only for conservative thermal
  configuration. Sensor temperature and TEC voltage are read-only telemetry.
  Fan and TEC ranges are derived from the model descriptor or SDK range query,
  not assumed constants.
- Hardware ROI and binning/skip modes are exposed when model flags and
  non-mutating SDK probes confirm support. ROI uses a typed
  [x, y, width, height] vector; even coordinates and the selected preview-mode
  bounds are validated before write. Binning is an enumeration built from the
  SDK's supported value/method pairs. Both settings are stopped-state-only;
  the next Start obtains final frame dimensions from the SDK and sizes the
  existing frame pool for the selected configuration.
- The cooling fan also accepts the SDK's documented -1 value to restore the
  model default speed.
- An injectable exact-model allow/deny registry narrows the probed feature
  capabilities; a missing profile leaves SDK-probed capabilities unchanged.
  Profiles are available through the AmcamCamera constructor.
  The registry covers implemented settings and read-only model capability
  facts. Model facts include sensor class, monochrome/global-shutter flags,
  frame buffers, hardware trigger support, focus hardware, ST4, filter wheel,
  embedded and standalone light sources, hardware level range, and transport
  flags. These are metadata only: trigger execution, GPIO, focus movement, ST4
  guiding remain separate command surfaces. Embedded light-source and filter-
  wheel operations are implemented separately: light-source levels are
  range-checked and verified by readback; filter-wheel motion is submitted
  without blocking acquisition, and state reads distinguish movement from a
  confirmed slot. ST4 guide pulses and Stop are also dedicated operations with
  millisecond durations; guide-state reads preserve the SDK's S_OK (guiding)
  versus S_FALSE (idle) distinction. Astronomy auto-focus is a third independent
  accessory API: it exposes position, travel range, step size, moving state and
  optional focuser temperature (0.1 °C) and ambient telemetry in raw SDK units, plus separate absolute Move and Halt
  commands. It uses the SDK AAF actions and the AstroAutoFocuser model flag,
  not the lens AF/focus-motor APIs. Lens-mounted focus is a separate native
  surface: it reads structured lens and AF state, accepts manual focus
  positions only when the lens reports physical MF mode and the target lies
  within the SDK near/far motor range, and exposes aperture selection by the
  SDK's explicit posAM index into arrayFN labels only after aperture calibration.
  The aperture command is asynchronous at device level; refresh reads the
  current position rather than claiming immediate mechanical completion. AF
  mode switching remains unavailable
  until its working-distance parameters are documented. These command
  surfaces do not alter preview
  acquisition or trigger mode and are not ordinary camera feature settings.
- The former Qt preview application exposes trigger configuration separately from camera
  feature settings: mode/source/heartbeat changes are available while stopped,
  and finite software-trigger bursts are available only when the applied mode
  and model/profile permit them. Trigger configuration is a separate
  stopped-state command. Video,
  software/simulated, external and mixed external+software modes are exposed;
  external modes require an explicit source (opto-isolated input, GPIO0, GPIO1,
  counter, PWM or software). Software trigger commands submit finite image
  bursts only; continuous trigger mode and cancellation are not exposed.
  Triggered Start requires the model's hardware-event capability/profile so
  heartbeat health is available. Self-trigger parameters and GPIO direction/
  output programming remain separate follow-ups.
- Auto white-balance-once remains a separate native command; device-measured
  frame rate remains telemetry, not a configurable FPS feature.
- Still capture is independent of live-frame delivery. The complete SDK still-
  resolution table is queryable and selectable. Processed capture uses
  Amcam_SnapN; RAW capture uses Amcam_SnapR. Both commands require a still-table
  resolution index; UINT32_MAX is mapped to the still entry matching current
  preview dimensions. No sentinel is passed directly to the SDK.
  AMCAM_EVENT_STILLIMAGE is routed to a dedicated waiter and never enters the
  live event FIFO. The result is pulled with Amcam_PullImageV4(bStill=1).
  RAW stills preserve sensor significant depth separately from the BITDEPTH-
  selected 8/16-bit storage container. A timed-out or failed capture quarantines
  further still requests until a successful Stop/Start callback-drain boundary;
  the live acquisition queue, worker and pull path are unchanged.
- Callback-to-pull execution now follows the Swift model and the vendor's
  documented callback/PullImage pairing. Auto-exposure convergence notifications
  (AUTOEXPO_CONV / AUTOEXPO_CONVFAIL) are not surfaced as completion events:
  Swift's session handler ignores them too. A successful Once write means the
  SDK accepted and read back mode 2, not that convergence has been confirmed.
- CI validates compilation and queue behavior on Intel and Apple Silicon.
  Real-camera qualification remains for callback/Stop/Close timing, event rate,
  disconnect/timeouts, buffer layout, and retained-frame behavior.
- Shared feature-value validation is unit-tested through the common CMake target;
  macOS CI runs these tests before checking SDK and library artifacts.
- Application-owned delivered libraries use camera_-prefixed artifact names;
  the original Amcam SDK library remains libamcam.dylib.
- `Camera::Qt` provides the header-only `camera::qt::FrameStreamMailbox`
  primitive used by Qt adapters. The mailbox owns only the immutable latest-frame
  lease and notification-pending state; QObject notification remains with the
  adapter. It adds no camera, session, worker, or operation-queue ownership and
  has no Qt runtime dependency itself.
- `QtAmcamCamera` composes one `AmcamCamera` and embeds this mailbox by value. It
  executes device enumeration, lifecycle, and feature descriptor/read/write
  requests on one bounded, serialized worker and delivers
  completions on its Qt affinity thread. GUI Refresh/Open/Start/Stop/Close use
  this queue instead of blocking the UI.
  Lifecycle requests remain FIFO; session generations invalidate old-session
  feature work, while a separate feature-state generation invalidates stale
  descriptor/read snapshots without cancelling accepted FIFO writes or commands.
  The first accepted camera operation commits a QtAmcamCamera instance for its
  lifetime to synchronous or queued execution. Calls through the opposite API surface are
  rejected, so the two execution models cannot interleave on one camera.
- The Qt Widgets app builds its settings panel from common feature descriptors,
  including typed Boolean, integer, vector, floating-point and enumeration
  editors. The complete descriptor inventory is probed while the camera is
  stopped. Individual feature reads reuse that cached per-state inventory
  rather than rebuilding the full SDK probe set for every value.
- The preview holds the immutable `FramePtr` lease and wraps its pool memory
  in a non-owning `QImage`. A custom `FrameView` paints directly from that
  memory with `QPainter`; it does not call `QImage::copy()`, create a
  `QPixmap`, or allocate a per-frame presentation buffer. The lease remains
  alive until a newer frame replaces it.
- The latest-frame mailbox uses C++17 atomic `shared_ptr` operations and an
  atomic notification flag. It replaces stale frames instead of accumulating
  a queue. Frame notifications bypass the feature-request queue mutex. The
  mailbox still preserves acquisition-generation invalidation.
- The preview reports three separate rates, sampled at 1 Hz:
  **event FPS** counts `AMCAM_EVENT_IMAGE` callbacks, **pull FPS** counts
  successful `Amcam_PullImageV4` calls, and **paint FPS** counts unique frame
  sequences actually painted by `FrameView`. The UI also reports the frame
  exposure time and pool-exhaustion drops. All counters are atomic; no SDK
  telemetry call is made from the FPS monitor and no per-frame status-label
  formatting is performed.
- Every Start removes the generic SDK frame-rate cap (`AMCAM_OPTION_FRAMERATE=0`).
  After callback acquisition has started, the driver selects the model's
  documented maximum rate: maximum precise FPS plus 100% bandwidth for models
  advertising `AMCAM_FLAG_PRECISE_FRAMERATE`, or maximum `Amcam_put_Speed`
  level (the model descriptor's `maxspeed`) otherwise. The documented
  low-noise mode is disabled when supported because it trades frame rate for
  SNR. No arbitrary FPS target is applied. See the vendor's
  [frame-rate configuration procedure](https://touptekphotonics.com/FAQ/73).
  This selects the device's maximum supported mode; it cannot override the
  exposure-time, sensor-readout, bit-depth, or transport limits of a physical
  camera.
- The FPS monitor distinguishes camera callback delivery, successful SDK
  pulling, and GUI painting. A low event rate points upstream to exposure,
  configured preview dimensions, camera speed mode, bit depth, or transport;
  a gap between event and pull rates points to the acquisition worker; a gap
  between pull and paint rates points to UI presentation.
- Full feature descriptor/value refresh is deliberately not performed while
  streaming. Feature settings remain disabled until Stop; streaming commands
  and accessory operations retain their distinct APIs.

### Model-specific feature policy

The application can inject exact model profiles when creating the camera:

```cpp
amscope::AmcamCameraModelProfile profile;
profile.modelCode = "Exact SDK model name";
profile.allowedFeatures = {
    amscope::AmcamCameraFeature::ExposureAuto,
    amscope::AmcamCameraFeature::ExposureManual,
    amscope::AmcamCameraFeature::AnalogGain
};
profile.deniedFeatures = {
    amscope::AmcamCameraFeature::AnalogGain
};

amscope::QtAmcamCamera camera(
    amscope::AmcamCameraProfileRegistry({profile}));
```

A configured profile is an allow-list, and explicit deny entries take
precedence. The exact model code is whitespace-trimmed for lookup. Duplicate
profiles use the last entry. Profiles can only restrict capabilities already
confirmed by SDK probes; they cannot make unsupported hardware features appear.
A model without a configured profile uses its runtime-probed capabilities.

## CTI / GenTL integration status

This branch does not yet contain an integrated GenTL Consumer or CTI adapter.
The existing dependency assessment identified the official EMVA GenApi 3.5.1
runtime/SDK and GenTL 1.6 ABI, plus a tested Swift GenTL Consumer in
AMSCOPE_DRIVER/macos-version-0.4. That Swift Consumer has no direct C++ API
in the inspected branch. A real camera-vendor .cti Producer is still
required. The C++ Consumer integration choice remains open; see
[AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md](../../docs/architecture/AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md).
Do not reimplement GenApi, create a synthetic AmScope CTI Producer, or route
AmScope through GenApi without a documented XML/register-port interface. The
AmScope path remains on the native Amcam SDK.

## Non-Qt driver build and tests

The Camera v2.0 driver and common contracts build without Qt or a GUI framework.
Requirements: macOS, Xcode command-line tools, and CMake 3.21+.

Configure, build, and run the non-Qt test suite:

```sh
cmake -S Camera/Amscope -B build/amscope \
  -DCMAKE_BUILD_TYPE=Release \
  -DAMSCOPE_BUILD_QT_ADAPTER=OFF \
  -DAMSCOPE_BUILD_PREVIEW_APP=OFF \
  -DAMSCOPE_BUILD_NATIVE_PROBE=ON \
  -DAMSCOPE_BUILD_SDK_WAIT_PROBE=ON \
  -DBUILD_TESTING=ON
cmake --build build/amscope --config Release --parallel
ctest --test-dir build/amscope --output-on-failure
```

The normal CMake defaults are non-Qt. The two Qt options remain temporarily
available only for the legacy adapter/preview source and default to OFF. They
must not become transitive dependencies of the driver, common contracts,
installed package, or external non-Qt consumers.

For direct physical-camera acquisition diagnostics, use the production
AmcamCamera callback-to-pull path:

```sh
bash scripts/run-amscope-native-probe.sh --seconds 60
```

For an independent vendor SDK WaitImage reference path:

```sh
bash scripts/run-amscope-vendor-wait-probe.sh --seconds 60
```

Run these diagnostics separately. Each process must own the camera exclusively.
Neither probe is a replacement production backend; production acquisition
remains SDK callback → bounded event queue → one acquisition worker →
`Amcam_PullImageV4`.

### Legacy Qt preview and hardware-rate utility

The Qt adapter, Widgets preview, and Qt-based hardware-rate presentation remain
in source temporarily for historical comparison. They are not Camera v2.0
deliverables and are disabled by default. The legacy launcher
`scripts/run-amscope.sh` explicitly opts into them and requires Qt 6. Do not
use that launcher as the Camera v2.0 build or acceptance path.


## Acquisition-isolation diagnostics

Two standalone programs isolate the source of acquisition failures. They are
development diagnostics, not alternate production backends, and each process
must own the camera exclusively. **Never run the probes or the preview at the
same time.**

### Native driver, without Qt

Build and run the production `AmcamCamera` callback-to-pull path without the
Qt adapter, Widgets, or preview:

```sh
bash scripts/run-amscope-native-probe.sh --seconds 60
```

When more than one camera is connected, pass its current enumerated opaque ID:

```sh
bash scripts/run-amscope-native-probe.sh --device-id "CAMERA_ID" --seconds 60
```

The probe reports one-second frame-delivery counts, optional sequence values,
dropped frames, native camera state, last-frame age, and acquisition errors. It
also prints one `FRAME` record from the first delivered common frame: dimensions,
live/still marker, stride, payload byte count, buffer pixel layout, optional
format identity, significant/storage bit depths, and the availability and
timebases of sequence, timestamp, and exposure metadata. This record is intended for Phase 2 common
frame-contract verification; it is not an FPS benchmark. It also treats failed
Stop/Close cleanup as a failed probe. The default sample duration is 30 seconds;
supported values are 5–3600 seconds.

### Vendor SDK reference path, without AmcamCamera or Qt

Build and run the vendor SDK's polling-style reference path:

```sh
bash scripts/run-amscope-vendor-wait-probe.sh --seconds 60
```

For multiple cameras:

```sh
bash scripts/run-amscope-vendor-wait-probe.sh --device-id "CAMERA_ID" --seconds 60
```

This program uses `Amcam_StartPullModeWithCallback(camera, nullptr, nullptr)`
with `Amcam_WaitImageV3`, matching the bundled vendor wait-sample pairing.
It reports successful frames, timeouts, sequence progression, and available
read-only SDK packet/queue/drop counters. It does not change those SDK options.
A failed WaitImage call or failed Stop makes the probe fail. The default sample
duration is 60 seconds; supported values are 5–3600 seconds.

### Interpreting the comparison

Run the programs separately, using the same camera, resolution, exposure,
transport connection, and duration where possible.

| Native-only probe | Vendor-only probe | Interpretation |
|---|---|---|
| Stalls/faults | Stalls/faults | Investigate camera, firmware, USB/transport, SDK, or shared camera configuration first |
| Stalls/faults | Stable | Investigate the production AmcamCamera setup, callback-to-pull path, and its lifecycle |
| Stable | Stable | The native acquisition path is less likely to be the fault; isolate the application adapter or frame consumer next |
| Stable | Stalls/faults | Investigate the vendor polling path's compatibility with this model/configuration; do not infer that production callback/pull is faulty |

The vendor-only program is a reference diagnostic, not the application's
production acquisition strategy. The production driver remains
SDK callback -> bounded event queue -> one acquisition worker ->
`Amcam_PullImageV4`. The native-only probe exercises that exact driver path
while excluding Qt; the vendor-only probe intentionally exercises the
different `WaitImageV3` path to provide an independent SDK/device comparison.

## CMake target policy

The default `Camera/Amscope` configure builds the native AmScope driver and
common camera contracts without Qt. The legacy targets are opt-in only:

- `AMSCOPE_BUILD_QT_ADAPTER=ON`: legacy Qt Core adapter.
- `AMSCOPE_BUILD_PREVIEW_APP=ON`: legacy Qt Widgets preview; this also
  requires the Qt adapter.

These options are disabled by default and are not part of the Camera v2.0
package or external-consumer contract. New camera code must not depend on them.


## macOS SDK selection

The SDK contains two macOS libraries:

- mac/x64+arm64/libamcam.dylib: Intel x86_64 and Apple Silicon arm64, macOS 11+.
- mac/x64+x86/libamcam.dylib: Intel universal binary for older macOS support.

CMake selects based on the requested/native architecture. Override with
-DAMSCOPE_VENDOR_MAC_ARCH=x64+arm64 or -DAMSCOPE_VENDOR_MAC_ARCH=x64+x86.
The selected libamcam.dylib is copied beside libcamera_amscope_driver.dylib and the driver
uses @loader_path for runtime resolution.

## Legacy Qt frame handoff — reference only

`QtAmcamCamera`, `Camera::Qt` and their QObject notification mechanism are
legacy optional integration code. They are excluded from the Camera v2.0
programmatic API and are not required to consume frames.

The supported camera-facing contract is the non-Qt `AmcamCamera` API:
its `FrameCallback` receives an immutable `FramePtr` lease on the acquisition
worker. Consumers must retain the lease for the complete period in which they
read its pixel memory. They must not synchronously call start, stop, close or
destroy the camera from the acquisition callback. The application owns
notification scheduling and presentation.


## Lifecycle

1. Call AmcamCamera::enumerate() and present the returned devices.
2. Open the selected CameraInfo::id.
3. Configure exposure and, while stopped, resolution.
4. Start acquisition with a frame callback.
5. Stop before changing resolution or closing the session.
6. Re-enumerate after disconnect/replug because a camera id can change.

Frame and error callbacks execute on the native acquisition worker. Do not call
start, stop, close, or destroy AmcamCamera synchronously from either callback.
Queue lifecycle changes and native camera destruction to the owning application/
control thread. The application must serialize these transitions and ensure
the acquisition callback has returned before destroying the camera.

## Not included yet

The first hardware-I/O surface reads the model-reported opto/GPIO lines
while the camera is stopped and supports stopped-state configuration of
direction, signal format, output inversion, input activation/debounce, output
mode and strobe timing, with readback verification and rollback. Output-level writes, UART, counter/PWM
programming, self-trigger parameterization, synchronized multi-camera
acquisition, GenTL routing, RAW demosaicing for visible preview, and a concrete
GPU renderer remain separate capabilities.
