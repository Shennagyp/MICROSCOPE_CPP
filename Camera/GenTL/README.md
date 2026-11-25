# GenTL Provider Adapter

This directory implements the Camera Module's optional GenTL provider path. It is a second backend alongside the native AmScope provider; it does not replace or wrap the AmScope SDK.

## Runtime boundary

`ConsumerBridge` loads the separately built `libGenTLConsumerCAPI.dylib`. It validates:
- bridge ABI version 1;
- GenTL header version 1.6.

The C++ adapter does not load CTI files itself and does not implement a second GenTL function table. The existing Swift Consumer owns CTI loading and GenTL function dispatch. The C++ adapter calls that Consumer through the versioned C ABI.

The current Swift C ABI source is in the private `Shennagyp/AMSCOPE_DRIVER` repository, branch `gentl-c-api-v.1.0` at `2f8f82d1e4b645a1191bf68021f9803ac6843121`. Its packages currently declare macOS 13.0 as the minimum deployment target. The C ABI tests cover typed pointers, port buffers, event registration/read/unregister and buffer-info forwarding, and the workflow now builds separate x86_64/arm64 products for a universal dylib. However, workflow [#45684107190](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45684107190) was cancelled before any build/test steps. The real dylib and C++ interoperability remain unvalidated; C++ CI currently uses a mock C ABI bridge.

## Build

Build the C++ adapter and mock tests independently of the AmScope target:

```sh
cmake -S Camera/GenTL -B build/gentl -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/gentl --parallel
ctest --test-dir build/gentl --output-on-failure
```

To enable the optional GenApi 3.5.1 integration, install the official GenApi SDK and set its root:

```sh
cmake -S Camera/GenTL -B build/gentl-genapi \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DCAMERA_GENTL_ENABLE_GENAPI=ON \
  -DGENAPI_ROOT=/path/to/GenApi-3.5.1-Mac64_Universal
```

The GenApi target is off by default. It requires GenApi and GCBase headers/libraries. It has not yet been compiled against the official SDK in CI. The current Swift Consumer packages also declare macOS 13.0 as the minimum runtime platform for the GenTL C ABI dylib.

## Current C++ API

```cpp
#include "camera/gentl/Producer.h"
#include "camera/gentl/GenApiDeviceAccess.h"

auto bridge = camera::gentl::ConsumerBridge::load(
    "/path/to/libGenTLConsumerCAPI.dylib");

auto producer = camera::gentl::Producer::open(
    bridge, "/path/to/vendor.cti");

const auto devices = producer.discover(); // timeout argument is milliseconds
auto device = producer.openDevice(devices.at(0));

const auto streamIds = device.dataStreamIds();
auto stream = device.openDataStream(streamIds.at(0), 4);
stream.start();

if (auto frame = stream.nextFrame(1000)) {
    // frame->payload is an independent copy; the GenTL buffer is requeued.
    // Optional metadata remains optional when the Producer does not report it.
    if (auto common = camera::gentl::makeCommonFrame(*frame)) {
        camera::FramePtr commonFrame = *common;
        // Common consumers receive the immutable backend-neutral frame.
    }
}

stream.stop();
device.close();
```

Ownership and lifecycle:
- `Producer` owns shared TL/Consumer state.
- `DeviceSession` retains its device handle and remote port; it can outlive the `Producer` wrapper.
- `DataStreamSession` retains the device state; it can outlive the `DeviceSession` wrapper.
- Each stream has a bounded announced-buffer set (1–64; the Producer's minimum is enforced).
- Acquisition uses `EVENT_NEW_BUFFER`, `EventGetData`, typed buffer metadata queries and explicit requeue.
- `nextFrame(timeoutMilliseconds)` permits one waiter per stream. It returns `nullopt` on timeout.
- Payload bytes are copied before the announced buffer is requeued. This avoids exposing a pointer whose contents the Producer may overwrite on the next acquisition.
- GenTL timeout arguments are nanoseconds; the public discovery and frame-wait APIs accept milliseconds and convert with overflow checks.
- Cleanup stops acquisition, unregisters the event (which drains active event reads), flushes the queue, revokes buffers and closes the stream.
- If cleanup fails, the native context and C ABI bridge are retained for process lifetime. They are not unloaded while CTI handles may still exist.

`DataStreamFrame` preserves payload bytes, frame ID, nanosecond timestamp, pixel-format identifier and namespace, dimensions, image-present/chunk/incomplete state, image offset and X/Y padding. `GenTLFrameAdapter` maps only complete image payloads with explicit zero offset/padding and no chunk data into `camera::Frame`. PFNC Mono8 is the first typed layout mapping; unknown formats retain their opaque identifier and payload but remain `PixelFormat::Unknown` with no invented stride. Packed, planar, multipart, compressed and padded formats remain unsupported by this projection.

## Device port and GenApi XML

The adapter exposes remote-port read/write and retrieves GenApi XML using:
- `LOCAL`: reads the register-map XML in bounded chunks from the advertised register address;
- `FILE`: reads the advertised local file after size validation.

HTTP and vendor-specific URL schemes are not implemented. XML retrieval is not equivalent to parsing the XML or validating the register map.

`GenApiPort`, `GenApiNodeMap`, `GenApiFeatureAccess`, `GenApiCommandAccess` and `GenApiDeviceAccess` are optional source integrations. `GenApiFeatureAccess` maps Boolean, Integer, Float, String and Enumeration nodes to `ICameraFeatureAccess`, including availability/readability/writability, numeric range/unit and currently available enumeration symbols. `GenApiCommandAccess` maps `ICommand` nodes to the separate `ICameraCommandAccess` contract: issuing a command and polling its completion are distinct operations. `GenApiDeviceAccess` is the preferred combined owner: it retains one DeviceSession and shares one GenApi node map, remote port and serialization boundary across feature and command access. The standalone feature/command adapters are also available when only one surface is needed. IntegerVector and IRegister nodes are not yet exposed. The official GenApi SDK build is now included in the macOS CI workflow and is being validated.



For a GenApi-capable device, construct the combined adapter after opening the device:

```cpp
auto genApi = camera::gentl::GenApiDeviceAccess(std::move(device));
auto featureList = genApi.features().featureDescriptors();
auto commandList = genApi.commands().commandDescriptors();

// Command issue is distinct from completion polling.
auto issued = genApi.commands().executeCommand("AcquisitionStart");
auto done = genApi.commands().isCommandDone("AcquisitionStart");
```

Use the combined owner when both surfaces are required. Do not open a second device session merely to separate feature and command access.

## Current implementation and evidence

Implemented:
- version-checked dynamic C ABI bridge;
- CTI initialization and TL/interface/device discovery;
- retained interface/device lifetime and device opening;
- stream ID discovery and stream acquisition lifecycle;
- bounded buffer announcement/queue/requeue/drain;
- frame payload copying and basic GenTL buffer metadata;
- safe backend-neutral frame projection for PFNC Mono8, with explicit rejection of unknown/unsafe layout metadata;
- backend-neutral command contract and separate GenApi `ICommand` adapter source;
- remote-port read/write and `LOCAL`/`FILE` GenApi XML retrieval;
- process-lifetime retention after cleanup failure.

Mock CI: [workflow #45686459352](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45686459352), commit `03f674aa7c39ac5b80d6f4a2329b930cb5d81e8c`. Both Intel x86_64 and Apple Silicon arm64 passed 9/9 CTest tests and architecture checks. The tests cover the mock bridge, discovery, device/stream ownership, buffer lifecycle, frame payload/metadata, timeout conversion, quarantine retention, Common frame projection/layout rejection and Common feature/command contracts.

Not yet validated:
- the latest frame-adapter and command-contract commits (CI run for the current branch is pending);
- the real universal Swift `libGenTLConsumerCAPI.dylib` (latest workflow [#45684107190](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45684107190) was cancelled before build/test steps);
- a vendor CTI Producer;
- a physical GenTL camera;
- actual GenTL payload/metadata behavior under acquisition;
- GenApi SDK compilation, XML parsing and feature/command mapping;
- external consumer packaging and deployment of the C ABI dylib/Swift runtime.

A passing mock workflow is software evidence only. It is not a real CTI or hardware qualification.
