# Amscope native camera driver

This is the application-side C++ driver for the bundled Amcam SDK. Vendor
headers, libraries, documentation, and original samples remain under
Camera/VendorSDK/AmscopeVendorSDK. This directory contains the application
integration and does not modify vendor sample code.

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
- Frames are packed RGB24 (rowPitch = -1, stride = width * 3) and include
  sequence, timestamp, exposure, gain, and black-level metadata.
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
  primitives. The common feature contract exposes the eleven Swift AmScope
  control features through runtime descriptors and typed reads/writes:
  ExposureAuto, ExposureTime, AmcamAnalogGainPercent,
  AmcamColorMatrixEnabled, AmcamPixelFormat, AmcamWhiteBalanceTemperature,
  AmcamWhiteBalanceTint, AmcamWhiteBalanceRGBGain, AmcamFixedFrameRate,
  AmcamFrameSpeedLevel, and AmcamResolutionIndex.
- The AmScope descriptors use model metadata and non-mutating SDK probes.
  One-shot auto white balance remains a command, not a Boolean feature.
- An injectable exact-model allow/deny registry narrows the probed feature
  capabilities; a missing profile leaves SDK-probed capabilities unchanged.
  Profiles are available through AmcamCamera and QtAmcamCamera constructors.
  The current registry covers the capability families used by the eleven
  implemented feature descriptors; the wider sensor/trigger/accessory taxonomy
  remains a separate extension.
- Auto white-balance-once remains a separate native command; device-measured
  frame rate remains telemetry, not a configurable FPS feature.
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
  editors. Readable values are fetched before writable controls are enabled;
  descriptors and values are refreshed after writes and lifecycle transitions.
- The preview renders the latest immutable frame lease through QImage/QPixmap;
  the QImage is detached before the lease is released. A host-monotonic
  30-interval moving-average estimate is shown as preview FPS; it is distinct
  from configured fixed FPS and SDK-reported frame-rate telemetry.

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

This branch does not currently contain GenTL or GenApi C++ headers, linked
libraries, imported CMake targets, Producer .cti files, or a CTI camera
adapter. The common ICameraFeatureAccess contract is ready to be implemented
by that backend, but CTI integration is not complete here. Connect the existing
GenTL/GenApi runtime and its documented C++ interfaces when that dependency is
made available to this build; do not add a replacement GenTL or GenApi
implementation. The AmScope path remains on the native Amcam SDK.

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

Still capture, RAW output, trigger/GPIO control, synchronized multi-camera
acquisition, GenTL routing, ImageProcessing integration, and a concrete GPU
renderer remain separate capabilities.
