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
- The Qt Widgets preview app demonstrates camera enumeration, open/start/stop,
  and rendering the latest immutable frame lease through QImage/QPixmap.

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

To run only the SDK camera-enumeration smoke check after building:

```sh
./scripts/run-amscope.sh --list-cameras
```

The preview UI lets you refresh the device list, open a selected camera, start
or stop acquisition, and close the session. It uses QLabel/QImage for a simple
CPU preview; it is not the final GPU renderer or a zero-copy presentation path.

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
The selected dylib is copied beside libamscope_driver.dylib and the driver
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

Do not call start, stop, or close from the acquisition callback. Queue lifecycle
changes to the owning application/control thread. Destroy QtAmcamCamera on
its owning Qt thread; its destructor stops and closes the session.

## Not included yet

Still capture, RAW output, trigger/GPIO control, synchronized multi-camera
acquisition, GenTL routing, ImageProcessing integration, and a concrete GPU
renderer remain separate capabilities.
