# GenTL Provider Adapter

This module implements the GenTL 1.6 camera backend for the Camera Module. It is independent of the native AmScope backend; both project their frames and features into the backend-neutral `Camera.Common` contracts.

## Runtime architecture

The default production path is a native C++ GenTL Consumer:

`Producer::open(ctiPath)` -> `GenTLProducerLibrary` -> vendor CTI exports -> `Producer` -> device / data-stream sessions -> `Camera.Common`.

`GenTLProducerLibrary` loads the vendor CTI with the platform dynamic loader, resolves the required GenTL 1.6 C exports, and dispatches typed operations through a checked function table. It is part of this repository and is the default production route. It does not require a separately deployed Swift runtime or `libGenTLConsumerCAPI.dylib`.

The `Consumer` object owns the dispatch boundary and retains the underlying implementation for the complete lifetime of producer, device and stream sessions. It supports two backends:
- **Native CTI backend (default):** in-process C++ dispatch to the CTI's GenTL exports.
- **Versioned C ABI bridge (optional):** `ConsumerBridge` can load a separately built consumer implementation when an application explicitly injects one. It is not required by `Producer::open(ctiPath)`.

Installed Producer discovery follows the GenTL environment convention. `ProducerCatalog` reads `GENICAM_GENTL64_PATH` (colon-separated directories on macOS/UNIX), scans each directory for `.cti` files, canonicalizes and deduplicates paths, and attempts each Producer independently. One broken or incompatible CTI is reported in `failures` and does not suppress devices discovered from other Producers. Successful catalog entries retain the exact `Producer` instance and interface handles used for their device discovery, so their descriptors can be passed directly to that entry's `producer.openDevice()`. A scan may successfully return zero devices when no camera is connected.

AmScope remains a separate native backend. Neither backend leaks vendor-specific SDK calls through `Camera.Common`.

## Build and test

Build the GenTL module and its deterministic tests independently of the AmScope application:

```sh
cmake -S Camera/GenTL -B build/gentl -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/gentl --parallel
ctest --test-dir build/gentl --output-on-failure
```

The ordinary GenTL suite includes:
- C ABI bridge compatibility and Consumer/Producer lifecycle tests;
- direct CTI symbol loading and dispatch tests;
- installed CTI path discovery, canonical-path deduplication, partial-failure isolation, and retained descriptor ownership;
- frame metadata and PFNC projection tests;
- a loadable in-repository `.cti` simulator exercised through the **same native C++ path used by production**;
- two-device isolation, concurrent streaming, monotonic frame IDs/timestamps, payload progression, and independent stream shutdown.

To build the optional GenApi 3.5.1 integration, install the official GenApi SDK and set its root:

```sh
cmake -S Camera/GenTL -B build/gentl-genapi \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DCAMERA_GENTL_ENABLE_GENAPI=ON \
  -DGENAPI_ROOT=/path/to/GenApi-3.5.1-Mac64_Universal
cmake --build build/gentl-genapi --parallel
ctest --test-dir build/gentl-genapi --output-on-failure
```

GenApi is optional at build time because it depends on the official GenICam SDK. The macOS CI workflow downloads and hash-verifies the official EMVA GenICam Package, builds GenApi 3.5.1 for Intel x86_64 and Apple Silicon arm64, then runs XML-backed tests through the native CTI Consumer and the in-repository simulated Producer. Those tests exercise Root/nested-category traversal, dynamic availability, access modes, Boolean/Integer/Float/String/Enumeration features, numeric validation, commands, register-node rejection and failure/recovery behavior.

## Production C++ API

No separate Consumer library is necessary. To discover installed CTIs and devices, use the catalog:

```cpp
auto catalog = camera::gentl::ProducerCatalog::discoverDevices();

for (const auto& failure : catalog.failures) {
    // Report failure.producerPath and failure.message without hiding
    // cameras discovered from other CTI Producers.
}

for (auto& entry : catalog.producers) {
    for (const auto& descriptor : entry.devices) {
        // entry.producer owns the retained interface handles for descriptor.
        auto device = entry.producer.openDevice(descriptor);
        // Use device.dataStreamIds(), GenApi access and frame acquisition.
    }
}
```

Each successful entry keeps its Producer alive. Do not detach its `DeviceDescriptor` from `entry.producer` and attempt to open it through another Producer instance.

No separate Consumer library is necessary. For an application that already knows the CTI path, the direct API remains:

```cpp
#include "camera/gentl/Producer.h"
#include "camera/gentl/ProducerCatalog.h"
#include "camera/gentl/GenTLFrameAdapter.h"
#include "camera/gentl/GenApiDeviceAccess.h"

#include <utility>

auto producer = camera::gentl::Producer::open("/path/to/vendor.cti");
const auto devices = producer.discover(); // public timeout is milliseconds
auto device = producer.openDevice(devices.at(0));

const auto streamIds = device.dataStreamIds();
auto stream = device.openDataStream(streamIds.at(0), 4);
stream.start();

if (auto frame = stream.nextFrame(1000)) {
    // frame->payload is independent of the requeued GenTL buffer.
    // Optional metadata remains optional when the Producer omits it.
    if (auto common = camera::gentl::makeCommonFrame(std::move(*frame))) {
        camera::FramePtr commonFrame = *common;
        // Common consumers receive the immutable backend-neutral frame.
    }
}

stream.stop();
device.close();
```

For a GenApi-capable device:

```cpp
auto genApi = camera::gentl::GenApiDeviceAccess(std::move(device));
auto featureList = genApi.features().featureDescriptors();
auto commandList = genApi.commands().commandDescriptors();

auto issued = genApi.commands().executeCommand("AcquisitionStart");
auto done = genApi.commands().isCommandDone("AcquisitionStart");
```

Use the combined GenApi owner when both feature and command access are required. It retains one device session and shares one node map, remote port and serialization boundary; do not open a second session for the command adapter.

`discover()` returns provider-specific `DeviceDescriptor` values. Preserve the descriptor returned by discovery and pass it to `openDevice`; `producerPath`, `interfaceId` and `deviceId` are routing identity, not display metadata. `openDevice` rejects descriptors from a different Producer, descriptors whose interface is no longer retained, and unknown device IDs. Do not reduce a descriptor to `deviceId` or reconstruct it from `CameraDeviceInfo` fields.

## Ownership, acquisition and teardown

- `Producer` owns shared TL/Consumer state.
- `DeviceSession` retains its device handle and remote port; it can outlive the `Producer` wrapper.
- `DataStreamSession` retains device state; it can outlive the `DeviceSession` wrapper.
- Each stream has a bounded announced-buffer set (1–64; the Producer's minimum is enforced).
- Acquisition uses `EVENT_NEW_BUFFER`, `EventGetData`, typed buffer metadata queries and explicit requeue.
- `nextFrame(timeoutMilliseconds)` permits one waiter per stream. It returns `nullopt` on timeout. `GCUnregisterEvent` terminates pending `EventGetData` waits with `GC_ERR_ABORT`; stream shutdown uses this cancellation path before waiting for active calls to drain. If unregistration wins the race before `EventGetData` enters the Consumer, `GC_ERR_INVALID_HANDLE` is also treated as cancellation, but only after the stream has entered its stopping state.
- Payload bytes are copied once before the announced buffer is requeued. Offset and padding metadata are retained without an additional repacking copy. The returned frame therefore does not expose a pointer that the Producer may overwrite during the next acquisition.
- GenTL timeout arguments are nanoseconds; public discovery and frame-wait APIs accept milliseconds and convert with overflow checks.
- Cleanup stops acquisition, unregisters the event, waits for active calls to drain, flushes the queue, revokes buffers and closes the stream. If event unregistration fails, `EventKill` is attempted as a fallback for the single permitted waiter; the native context is quarantined because normal teardown did not complete. GenTL 1.6 only aborts one pending waiter per `EventKill` call.
- If cleanup fails, the Consumer backend and CTI library are retained for process lifetime. They are not unloaded while CTI handles or callbacks may still exist.

`DataStreamFrame` preserves payload bytes, frame ID, nanosecond timestamp, pixel-format identifier and namespace, pixel endianness, dimensions, image-present/chunk/incomplete state, image offset and X/Y padding. `GenTLFrameAdapter` maps complete non-composite image payloads with explicit image offset and padding metadata into `camera::Frame`. The complete filled payload is retained byte-for-byte; `Frame::imageData()` addresses the first image byte, `strideBytes` includes per-row X padding, and trailing Y padding is represented separately. A known PFNC layout is projected only when image offset, row pitch, height and trailing padding account for the complete filled payload exactly. Verified PFNC mappings include Mono8, unpacked Mono10/12/14/16, packed Mono10p/Mono12p, RGB8, and Bayer GR/RG/GB/BG in 8-bit, unpacked 10/12/14/16-bit, and packed 10p/12p forms. Packed Mono and Bayer payloads remain byte-for-byte unchanged; `PixelPacking` identifies the packing convention and `strideBytes` describes the byte-aligned row size. The adapter does not unpack these samples. Bayer mosaic identity is stored separately from the RAW8/RAW16 sample container; no demosaicing or sample-value transformation occurs in this backend. Multi-byte mappings require reported byte order. Planar, multipart and compressed layouts remain opaque. Padded layouts are projected only when their byte layout is known and validated; padding is never stripped or rearranged.

### WSI acquisition integrity telemetry

DataStreamSession::statistics() provides a point-in-time view of the current stream. producerDeliveredBuffers, producerBufferUnderruns, producerFramesStarted, announcedBuffers, queuedBuffers, and buffersAwaitingDelivery are optional because a GenTL Producer may not implement every information command. Their meanings follow the GenTL stream-information contract; they are not inferred from host-side frame counts.

The integrity member is the protocol-neutral accounting view: observed buffers, complete images, incomplete buffers, non-image payloads, empty payloads, host wait timeouts, buffers without frame IDs, sequence gaps and missing-frame count, duplicates, out-of-order IDs, ambiguous backwards transitions, counter wraps, and provider-reported buffer-starvation events.

A nextFrame() wait timeout is counted separately from a producer-reported underrun. An underrun is accumulated from the GenTL STREAM_INFO_NUM_UNDERRUN counter when statistics() is queried; the snapshot uses counter deltas and tolerates a provider counter reset. Therefore call statistics() at the points where acquisition telemetry is required. It does not add a polling thread or change the frame-delivery path.

Sequence continuity is conservative. A monotonically increasing counter with no known wrap rule can prove forward gaps, but a backwards transition is reported as ambiguous rather than guessed to be a wrap or a loss. The GenTL TLTYPE GEV model uses the GenTL-defined one-based 16-bit GigE Vision frame ID rule; no wrap width is assumed for other transport types. Sequence accounting is per DataStreamSession and resets at each start().

These counters are evidence for acquisition diagnosis and WSI capture validation; they do not decide tile acceptance, retry capture, or make storage lossless. The application remains responsible for a durable tile-write path and for deciding how to respond to detected gaps or incomplete frames.

## Device port and GenApi XML

The adapter exposes remote-port read/write and retrieves GenApi XML using:
- `LOCAL`: reads the register-map XML in bounded chunks from the advertised register address;
- `FILE`: reads the advertised local file after size validation.

HTTP and vendor-specific URL schemes are not implemented. XML retrieval is not equivalent to parsing the XML or validating the register map.

`GenApiPort`, `GenApiNodeMap`, `GenApiFeatureAccess`, `GenApiCommandAccess` and `GenApiDeviceAccess` are optional source integrations. Feature access maps Boolean, Integer, Float, String and Enumeration nodes to `ICameraFeatureAccess`, including availability/readability/writability, numeric range/unit and currently available enumeration symbols. Command access maps `ICommand` nodes to the separate `ICameraCommandAccess` contract: issuing a command and polling its completion are distinct operations.

Descriptor enumeration and direct feature/command lookup recursively traverse nodes reachable from the GenApi `Root` category, including nested categories, while deduplicating node references. Implementation nodes such as `IntReg` and `Port` are not public features. A public GenApi `Register`/`IRegister` node reachable from `Root` is not projected as a scalar feature; direct feature read/write return `Unsupported`. IntegerVector and IRegister values are not yet exposed. The official GenApi 3.5.1 SDK and simulator-backed XML integration tests run in CI; vendor-provided XML remains an external interoperability test.

## In-repository simulator smoke (CI)

With `BUILD_TESTING=ON`, CMake builds the deterministic mock GenTL Producer as a loadable `.cti` shared library in simulator mode. The smoke test invokes:

`GenTLProducerLibrary` -> `Consumer` -> `Producer` -> device/stream -> `Camera.Common` frame projection.

There is no intermediary test shim: this exercises `Producer::open(ctiPath)`, which is also the production entry point.

The simulator exposes two deterministic cameras on one interface. Each has a distinct device ID, display name, serial number, remote-port/register space, data-stream handle, event handle, buffer ownership and frame-sequence counter. The smoke opens both devices and both streams concurrently, writes different values to the same register address on each remote port, and interleaves frame retrieval. It verifies that each stream independently starts at frame ID 1, timestamps increase, and Mono8 payload bytes advance with the frame sequence. It then stops one stream while the peer remains active and verifies that the peer continues delivering frames. The ordinary smoke also verifies discovery, device open, GenApi XML retrieval, stream start, complete image buffers, Common frame projection, stream stop and device close.

Run it with the ordinary test build:

```sh
cmake -S Camera/GenTL -B build/gentl -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/gentl --parallel
ctest --test-dir build/gentl -R camera_gentl_inrepo_simulator_smoke --output-on-failure
```

This is dynamic loading of a CTI-shaped GenTL Producer and software integration coverage of the native Consumer path. It is not proof of interoperability with an independent vendor Producer, a real transport layer, or physical camera hardware.

## Vendor CTI / independent simulator smoke (opt-in)

The same smoke executable can test an independently supplied vendor or virtual-camera CTI directly, with no test bridge:

```sh
cmake -S Camera/GenTL -B build/gentl-simulator \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DCAMERA_GENTL_SIMULATOR_CTI=/absolute/path/to/vendor-or-simulator.cti
cmake --build build/gentl-simulator --parallel
ctest --test-dir build/gentl-simulator \
  -R camera_gentl_simulator_smoke --output-on-failure
```

The smoke selects device index 0 by default and accepts an optional non-negative device index when the Producer exposes several devices. It uses the first data stream on that device. It checks CTI loading, TL/interface/device discovery, device open, GenApi XML retrieval, stream start, five complete image buffers, Common frame projection, stream stop and device close. It prints device identity, projection outcome and available frame metadata. Unsupported layouts remain opaque rather than failing the smoke; it does not assume vendor-specific feature names or pixel formats.

The vendor CTI is supplied externally and is not downloaded or redistributed by this project. Use only a Producer whose license permits generic third-party camera-control software to load and operate it. A passing simulator test is software-interoperability evidence, not physical camera qualification.

## Implementation and validation boundaries

Implemented in this module:
- native C++ GenTL 1.6 CTI loading and function dispatch;
- optional version-checked C ABI Consumer injection;
- TL initialization and interface/device discovery;
- retained interface/device lifetime and device opening;
- data-stream discovery, acquisition lifecycle and bounded buffer management;
- buffer queue/requeue/drain and frame metadata extraction;
- frame payload ownership and backend-neutral projection for verified PFNC layouts;
- remote-port read/write and `LOCAL`/`FILE` GenApi XML retrieval;
- optional GenApi 3.5.1 feature and command access;
- stream shutdown cancellation, failed-cleanup quarantine and process-lifetime retention;
- deterministic single- and multi-device CTI simulator integration tests.

What simulator/CI can establish:
- C++ compilation and ABI/function-table dispatch on x86_64 and arm64;
- CTI dynamic loading and standard GenTL call sequencing against the in-repository Producer;
- discovery, concurrent device/stream isolation, port IO, buffer/acquisition lifecycle, frame metadata and Common frame projection;
- official GenApi 3.5.1 parsing and XML-backed feature/command semantics against the controlled fixture.

What still requires independent external qualification:
- interoperability with a separately produced vendor CTI;
- vendor transport-layer behavior, vendor XML quirks and optional GenTL features;
- physical link negotiation, camera firmware behavior, hardware timing and sustained acquisition performance.

A physical camera is **not required** to complete the software implementation or run the in-repository simulator/CI. Hardware qualification is a separate acceptance stage once a device is available.
