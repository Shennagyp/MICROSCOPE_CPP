# Amscope native camera driver

This is the application-side C++ driver for the bundled Amcam SDK. Vendor
headers, libraries, documentation, and original samples remain under
Camera/VendorSDK/AmscopeVendorSDK. This directory contains the application
integration and does not modify vendor sample code.

## Implemented

- One AmcamCamera owns one HAmcam handle for the complete open session.
- Enumeration copies SDK device data into CameraInfo; open uses the opaque
  device id returned by enumeration. Re-enumerate after a disconnect/replug.
- Acquisition starts pull mode and calls Amcam_WaitImageV4 on a dedicated
  C++ worker. The SDK event callback only records disconnect/error flags.
- Stop and Close are never called from an SDK callback. Handle operations are
  serialized; WaitImageV4 uses a 50 ms timeout to bound control/stop waiting.
- Frames are packed RGB24 (rowPitch = -1, stride = width * 3) and include
  sequence, timestamp, exposure, gain, and black-level metadata.
- Four reusable buffers are exposed as immutable FramePtr leases. If every
  buffer is retained, the worker drains into a private discard buffer and
  increments droppedFrames rather than blocking acquisition.
- Resolution can be changed while open and stopped. Start reads the final
  image dimensions and sizes the pool for the selected configuration.
- Exposure auto/manual, exposure time/gain, Temp/Tint white balance, one-shot
  auto white balance, and SDK frame-rate statistics are implemented.
- The Qt Core adapter has a one-frame latest mailbox. It coalesces notifications
  and schedules Qt signals onto the QObject affinity thread.

## Build on macOS

```sh
cmake -S Camera/Amscope -B build/amscope
cmake --build build/amscope
```

The default build includes the Qt 6 Core adapter and requires Qt 6. To build
only the non-Qt driver:

```sh
cmake -S Camera/Amscope -B build/amscope -DAMSCOPE_BUILD_QT_ADAPTER=OFF
cmake --build build/amscope
```

The SDK contains two macOS libraries:

- mac/x64+arm64/libamcam.dylib: Intel x86_64 and Apple Silicon arm64, macOS 11+.
- mac/x64+x86/libamcam.dylib: Intel universal binary for older macOS support.

CMake selects based on the requested/native architecture. Override with
-DAMSCOPE_VENDOR_MAC_ARCH=x64+arm64 or -DAMSCOPE_VENDOR_MAC_ARCH=x64+x86.
The selected dylib is copied beside libamscope_driver.dylib and the driver
uses @loader_path for runtime resolution.

## Isolate the native acquisition lifecycle (without Qt)

A standalone probe is available to determine whether frame acquisition stops
inside the native Amcam driver or only in the Qt handoff/rendering path. It
does not link Qt and does not create a GUI.

Run from the repository root:

```sh
bash scripts/run-amscope-native-probe.sh --seconds 60
```

When exactly one camera is connected, the probe selects it automatically. If
more than one camera is connected, pass the enumerated opaque id:

```sh
bash scripts/run-amscope-native-probe.sh --device-id "CAMERA_ID" --seconds 60
```

The probe prints one-second frame counts, sequence values, dropped frames,
native camera state, last-frame age, and any acquisition error.

Interpretation:

- If this probe also stops or enters Faulted, Qt is excluded: investigate the
  native acquisition/lifecycle path or the vendor SDK/device interaction.
- If this probe remains Streaming with continuous frame counts while the Qt
  preview stops, the fault is above the native driver: isolate the Qt bridge
  from the Widgets presentation path next.

Do not run the probe and the preview simultaneously; only one process should
own the camera.

## Isolate our driver from the vendor SDK

The vendor-only baseline bypasses `AmcamCamera`, the Qt adapter, and the
preview application. It reproduces the bundled vendor `demowait` sample:
a null event callback, `Amcam_WaitImageV3`, and the vendor-default row pitch.

Run from the repository root:

```sh
bash scripts/run-amscope-vendor-wait-probe.sh --seconds 60
```

For multiple connected cameras, pass the current enumerated id:

```sh
bash scripts/run-amscope-vendor-wait-probe.sh --device-id "CAMERA_ID" --seconds 60
```

Run only one camera-owning process at a time.

The direct vendor probe also records read-only SDK telemetry once per second:
received packet count and delta, SDK-dropped frame count and delta, and the
current frontend/backend queue depths. It reports the configured no-frame and
no-packet timeout thresholds (zero means disabled). These measurements do not
change camera settings or the vendor `demowait` acquisition path.

Interpret the telemetry at the point frames stop:

- If packet count stops increasing with frame count, investigate the camera,
  USB cable/port, power, and transport path.
- If packets continue increasing but frames stop, transport is still delivering
  data; investigate SDK frame assembly, internal queues, and camera settings.
- If SDK-dropped frames or queue depths grow, preserve those values when
  reporting the failure; they identify a different boundary from USB packet loss.

Interpret the comparison with `run-amscope-native-probe.sh`:

- If the vendor-only probe also stalls, the problem is below our driver:
  vendor SDK, camera firmware/hardware, USB/transport, or camera configuration.
- If the vendor-only probe remains stable while the native-only driver probe
  stalls, the failure belongs to our native `AmcamCamera` acquisition setup.
  Qt remains excluded in either case.

## Qt frame handoff

Connect frameAvailable() to a slot on the intended consumer/render thread with
Qt::QueuedConnection. In that slot call takeLatestFrame(). Keep the returned
FramePtr alive for the complete period in which the renderer reads its pixels,
then release it.

The mailbox is preview-oriented and freshness-first; it is not an ordered
recording/processing queue. A later recording path needs its own bounded queue
and explicit backpressure/drop policy.

The bridge does not create a GPU context, convert to QImage, or claim zero-copy
GPU upload. Those responsibilities belong to the application renderer and
must be implemented against the selected Qt rendering API.

## Lifecycle

1. Call AmcamCamera::enumerate() and present the returned devices.
2. Open the selected CameraInfo::id.
3. Configure exposure and, while stopped, resolution.
4. Start acquisition with a frame callback.
5. Stop before changing resolution or closing the session.
6. Re-enumerate after disconnect/replug because a camera id can change.

Do not call start, stop, or close from the acquisition callback. Queue lifecycle
changes to the owning application/control thread. Destroy QtAmcamCamera on
its owning Qt thread; its destructor stops and closes the session.

## Not included yet

Still capture, RAW output, trigger/GPIO control, synchronized multi-camera
acquisition, GenTL routing, ImageProcessing integration, and a concrete GPU
renderer remain separate capabilities.
