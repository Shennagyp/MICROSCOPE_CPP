# Amscope native camera driver

This is the application-side C++ driver for the bundled Amcam SDK. Vendor
headers, libraries, documentation, and original samples remain under
Camera/VendorSDK/AmscopeVendorSDK. This directory contains the application
integration and does not modify vendor sample code.

## Development roadmap

The authoritative phase-by-phase completion plan, including the mandatory real-camera test gate for every phase, is maintained in [AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md](../../docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md).
The current continuation state is in [DEVELOPMENT_HANDOFF.md](../../docs/architecture/DEVELOPMENT_HANDOFF.md), the source/test mapping is in [AMSCOPE_IMPLEMENTATION_INVENTORY.md](../../docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md), the GenTL/GenApi dependency assessment is in [AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md](../../docs/architecture/AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md), and the per-phase hardware record template is in [PHASE_HARDWARE_TEST_RECORD_TEMPLATE.md](../../docs/testing/hardware/PHASE_HARDWARE_TEST_RECORD_TEMPLATE.md).

## Implemented

- One AmcamCamera owns one HAmcam handle for the complete open session.
- Enumeration copies SDK device data into CameraInfo; open uses the opaque
  device id returned by enumeration. Re-enumerate after a disconnect/replug.
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
  clear the latest-frame mailbox; the gate check and mailbox publication share
  the mailbox lock, so an old frame cannot appear after that clear. The fatal
  session transition is then enqueued into the same FIFO as Stop/Close and feature
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
- AmcamRawOutputEnabled and AmcamRawBitDepth are exposed through the same
  typed feature interface and are writable only while the camera is stopped.
  Model RAW flags, SDK option probes, and the exact-model RawOutput profile
  policy must all permit the feature. The Qt preview explicitly refuses to
  interpret RAW sensor bytes as RGB888; it reports the active RAW format until
  a Bayer/RAW decoder is attached.
- Every frame includes sequence, timestamp, exposure, gain, and black-level metadata.
- Four reusable buffers are exposed as immutable FramePtr leases. If every
  buffer is retained, the worker drains into a private discard buffer and
  increments droppedFrames rather than blocking acquisition.
- Resolution can be changed while open and stopped. Start reads the final
  image dimensions and sizes the pool for the selected configuration.
- Exposure auto/manual, exposure time/gain, Temp/Tint white balance, one-shot
  auto white balance, and SDK frame-rate statistics are implemented. ExposureAuto
  maps Off/Continuous/Once to native SDK values 0/1/2; Once is not emulated as
  Continuous. The mode setter verifies immediate SDK readback.
- The Qt application boundary exposes camera settings only through
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
  Profiles are available through AmcamCamera and QtAmcamCamera constructors.
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
- The preview application exposes trigger configuration separately from camera
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
- The Qt Core adapter has a one-frame latest mailbox. It coalesces notifications
  and schedules Qt signals onto the QObject affinity thread.
- QtAmcamCamera executes device enumeration, lifecycle, and feature
  descriptor/read/write requests on one bounded, serialized worker and delivers
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
- The preview reports **pull FPS**, counted atomically after successful
  `Amcam_PullImageV4` calls, separately from **paint FPS**, counted only when
  `FrameView` paints a new frame sequence. Measuring throughput does not poll
  the camera SDK or contend for the SDK-call mutex. Dropped pool frames are
  displayed separately.
- Every Start removes the generic SDK frame-rate cap (`AMCAM_OPTION_FRAMERATE=0`).
  After callback acquisition has started, the driver selects the model's
  documented maximum rate: maximum precise FPS plus 100% bandwidth for models
  advertising `AMCAM_FLAG_PRECISE_FRAMERATE`, or maximum `Amcam_put_Speed`
  level otherwise. The documented low-noise mode is disabled when supported
  because it trades frame rate for SNR. No arbitrary FPS target is applied.
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

## One-command build and run

Requirements: macOS, Xcode command-line tools, CMake 3.21+ (either the
CMake GUI application or its command-line distribution), and Qt 6 with Core
and Widgets. The launcher detects the command-line executable inside the
standard `/Applications/CMake.app` or `~/Applications/CMake.app` bundle,
so a separate terminal installation of CMake is not required. For a custom
app location, set `CMAKE_APP` to the CMake.app bundle path, or set
`CMAKE_EXECUTABLE` to its full `Contents/bin/cmake` executable path.

Install Qt 6 with Homebrew when it is not already present:

```sh
brew install qt@6
```

From the repository root, compile the driver, Qt adapter, and preview app, then
launch the preview window with:

```sh
./scripts/run-amscope.sh
```

The script detects the current Mac architecture, selects the matching bundled
Amcam dylib, discovers Homebrew Qt 6 (or uses CMAKE_PREFIX_PATH when supplied),
configures CMake, builds, and launches Camera/Amscope/app/main.cpp's preview
application.

The fast launcher disables test executable builds by default so the first preview
launch does not spend time compiling tests. Set `AMSCOPE_BUILD_TESTING=ON` to
include the test executables in a local build; CI explicitly keeps testing enabled.

To run only the SDK camera-enumeration smoke check after building:

```sh
./scripts/run-amscope.sh --list-cameras
```

The preview UI lets you refresh the device list, open a selected camera, start
or stop acquisition, close the session, and inspect/edit the currently available
camera features through descriptor-generated controls. Settings use the common
typed feature contract rather than AmScope-specific widget setters. Potentially
blocking feature requests run off the UI thread; stale completions are ignored.
The image path uses QLabel/QImage for a simple CPU preview; it is not the final
GPU renderer or a zero-copy presentation path.

## Direct CMake build

The default build includes both the Qt 6 Core adapter and Qt Widgets preview:

```sh
cmake -S Camera/Amscope -B build/amscope \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build/amscope --config Release --parallel
```

To build only the non-Qt native driver:

```sh
cmake -S Camera/Amscope -B build/amscope \
  -DAMSCOPE_BUILD_QT_ADAPTER=OFF \
  -DAMSCOPE_BUILD_PREVIEW_APP=OFF
cmake --build build/amscope --parallel
```

## macOS SDK selection

The SDK contains two macOS libraries:

- mac/x64+arm64/libamcam.dylib: Intel x86_64 and Apple Silicon arm64, macOS 11+.
- mac/x64+x86/libamcam.dylib: Intel universal binary for older macOS support.

CMake selects based on the requested/native architecture. Override with
-DAMSCOPE_VENDOR_MAC_ARCH=x64+arm64 or -DAMSCOPE_VENDOR_MAC_ARCH=x64+x86.
The selected libamcam.dylib is copied beside libcamera_amscope_driver.dylib and the driver
uses @loader_path for runtime resolution.

## Qt frame handoff

Connect frameAvailable() to a slot on the intended consumer/render thread with
Qt::QueuedConnection. In that slot call takeLatestFrame(). Keep the returned
FramePtr alive for the complete period in which the renderer reads its pixels,
then release it.

The mailbox is preview-oriented and freshness-first; it is not an ordered
recording/processing queue. A later recording path needs its own bounded queue
and explicit backpressure/drop policy.

The bridge does not create a GPU context, convert frames to QImage, or claim
zero-copy GPU upload. Those responsibilities belong to the application
renderer and must be implemented against the selected Qt rendering API.

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
control thread. Destroy QtAmcamCamera on its owning Qt thread; its destructor
stops and closes the session.

## Not included yet

The first hardware-I/O surface reads the model-reported opto/GPIO lines
while the camera is stopped and supports stopped-state configuration of
direction, signal format, output inversion, input activation/debounce, output
mode and strobe timing, with readback verification and rollback. Output-level writes, UART, counter/PWM
programming, self-trigger parameterization, synchronized multi-camera
acquisition, GenTL routing, RAW demosaicing for visible preview, and a concrete
GPU renderer remain separate capabilities.
