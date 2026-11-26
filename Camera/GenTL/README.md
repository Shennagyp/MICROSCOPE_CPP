# GenTL Provider Adapter

This directory implements the Camera Module's optional GenTL provider path. It is a second backend alongside the native AmScope provider; it does not replace or wrap the AmScope SDK.

## Runtime boundary

`ConsumerBridge` loads the separately built `libGenTLConsumerCAPI.dylib`. It validates:
- bridge ABI version 1;
- GenTL header version 1.6.

The C++ adapter does not load CTI files itself and does not implement a second GenTL function table. The existing Swift Consumer owns CTI loading and GenTL function dispatch. The C++ adapter calls that Consumer through the versioned C ABI.

The current Swift C ABI source is in the private `Shennagyp/AMSCOPE_DRIVER` repository, branch `gentl-c-api-v.1.0` at `70d69d1c93327dfcd62eb7aa6e279af9407bc6f1` (the pending-wait test itself is in `0edf2d7580553111665b283656b07ced949c6025`). Its packages declare macOS 13.0 as the minimum deployment target. Source tests cover typed pointer/buffer marshalling, event registration/read/unregister, buffer-info forwarding, invalid invocation-shape rejection and a deterministic blocked-`EventGetData` cancellation test. The GenTL Consumer's `GCUnregisterEvent` calls `EventKill`, waits for active reads to drain, then unregisters the event. The universal macOS workflow [#45703282802](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45703282802) also failed before any job steps (after switching to the Intel macOS 15 runner) and has no logs, so these Swift tests remain unexecuted. The real dylib and C++ interoperability remain unvalidated; C++ CI currently uses a mock C ABI bridge.

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

The GenApi target is off by default. It requires GenApi and GCBase headers/libraries. The official EMVA GenICam Package 2026.07 (GenApi 3.5.1) SDK and runtime are downloaded, hash-verified and compiled in the macOS CI workflow for both Intel x86_64 and Apple Silicon arm64. XML-backed runtime tests use the official GenApi implementation and a mock GenTL port to exercise recursive Root-category traversal (including nested categories), internal-register rejection, dynamic availability, Boolean/Integer/Float/String/Enumeration reads and writes, floating-point increment validation, nested command enumeration/execution, command completion transitions, and GenTL read-only access semantics. Workflow [#45703819447](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45703819447) passed on both architectures. This does not validate behavior against a vendor CTI Producer or physical camera.

## Current C++ API

```cpp
#include "camera/gentl/Producer.h"
#include "camera/gentl/GenApiDeviceAccess.h"

#include <utility>

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
- `nextFrame(timeoutMilliseconds)` permits one waiter per stream. It returns `nullopt` on timeout. `GCUnregisterEvent` terminates pending `EventGetData` waits with `GC_ERR_ABORT`; stream shutdown uses this specified cancellation path before waiting for active calls to drain. If unregistration wins the race before `EventGetData` enters the Consumer, `GC_ERR_INVALID_HANDLE` is also treated as cancellation, but only after the stream has entered its stopping state. A deterministic regression holds `EventGetData` pending while another thread closes the stream.
- Payload bytes are copied before the announced buffer is requeued. This avoids exposing a pointer whose contents the Producer may overwrite on the next acquisition.
- GenTL timeout arguments are nanoseconds; the public discovery and frame-wait APIs accept milliseconds and convert with overflow checks.
- Cleanup stops acquisition, unregisters the event (which aborts pending event waits), waits for active calls to drain, flushes the queue, revokes buffers and closes the stream. If event unregistration fails, `EventKill` is attempted as a fallback for the single permitted waiter; the native context is quarantined because normal teardown did not complete. The GenTL 1.6 contract only aborts one pending waiter per `EventKill` call.
- If cleanup fails, the native context and C ABI bridge are retained for process lifetime. They are not unloaded while CTI handles may still exist.

`DataStreamFrame` preserves payload bytes, frame ID, nanosecond timestamp, pixel-format identifier and namespace, pixel endianness, dimensions, image-present/chunk/incomplete state, image offset and X/Y padding. `GenTLFrameAdapter` maps only complete image payloads with explicit zero offset/padding, no chunk data and exact payload-size/stride checks into `camera::Frame`. Verified PFNC mappings are Mono8, unpacked Mono10/12/14/16 and RGB8. Multi-byte mappings require reported byte order; packed, Bayer, planar, multipart, compressed and padded layouts remain opaque and are not assigned a fabricated stride.

## Device port and GenApi XML

The adapter exposes remote-port read/write and retrieves GenApi XML using:
- `LOCAL`: reads the register-map XML in bounded chunks from the advertised register address;
- `FILE`: reads the advertised local file after size validation.

HTTP and vendor-specific URL schemes are not implemented. XML retrieval is not equivalent to parsing the XML or validating the register map.

`GenApiPort`, `GenApiNodeMap`, `GenApiFeatureAccess`, `GenApiCommandAccess` and `GenApiDeviceAccess` are optional source integrations. `GenApiFeatureAccess` maps Boolean, Integer, Float, String and Enumeration nodes to `ICameraFeatureAccess`, including availability/readability/writability, numeric range/unit and currently available enumeration symbols. `GenApiCommandAccess` maps `ICommand` nodes to the separate `ICameraCommandAccess` contract: issuing a command and polling its completion are distinct operations. `GenApiDeviceAccess` is the preferred combined owner: it retains one DeviceSession and shares one GenApi node map, remote port and serialization boundary across feature and command access. Both descriptor enumeration and direct feature/command lookup recursively traverse nodes reachable from the GenApi `Root` category, including nested categories, while deduplicating node references; implementation nodes such as `IntReg` and `Port` are not public features. A generic public `Node` reachable from `Root` is not projected as a scalar feature; direct feature read/write return `Unsupported`. The standalone feature/command adapters are also available when only one surface is needed. IntegerVector and IRegister nodes are not yet exposed. The official GenApi 3.5.1 SDK and XML-backed integration tests pass on both macOS architectures in CI. Real CTI/physical-device validation remains outstanding.



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
- safe backend-neutral frame projection for verified PFNC Mono8, unpacked Mono10/12/14/16 and RGB8 layouts, with explicit byte-order and payload-size checks;
- backend-neutral command contract and separate GenApi `ICommand` adapter source;
- remote-port read/write and `LOCAL`/`FILE` GenApi XML retrieval;
- process-lifetime retention after cleanup failure.

GenTL/GenApi baseline CI: [workflow #45703819447](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45703819447), code commit `b1d0a61de841736f76cf4ec005f807e60310d71b`. Both Intel x86_64 and Apple Silicon arm64 passed the C++ bridge suite, official GenApi 3.5.1 build, XML-backed GenApi integration tests and architecture checks. Latest Phase 6 negative-path CI: [workflow #45767832186](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45767256996), commit `ea356d5d2cc412e8c6aa5a3600e65c650bf9d284`, passed both architectures including the official GenApi integration build/tests. The frame adapter accepts `DataStreamFrame` by value and moves its owned payload into the Common frame; pass `std::move(frame)` to avoid a second full-frame copy. The tests cover the mock bridge, discovery, device/stream ownership, pending-wait cancellation, unregister-failure `EventKill` fallback/quarantine, buffer lifecycle, frame payload/metadata, timeout conversion, PFNC layout projection, pixel byte order, Root and nested-category feature enumeration/lookup, internal register rejection, dynamic availability, Boolean/Integer/Float/String/Enumeration access, nested command issue/completion and read-only access semantics. The latest GenApi slice additionally covers malformed XML rejection followed by successful node-map recovery, unsupported public generic-node omission and `Unsupported` read/write classification, injected feature-read failure and recovery, failed feature-write no-side-effect verification, command-execution failure followed by successful execution, and command-completion polling failure mapped to `BackendFailure` followed by successful completion polling.

Not yet validated:
- actual frame acquisition and metadata behavior with a vendor CTI Producer/device;
- the real universal Swift `libGenTLConsumerCAPI.dylib` (latest workflow [#45695610813](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45695610813) failed before any job steps and produced no job logs);
- a vendor CTI Producer;
- a physical GenTL camera;
- actual GenTL payload/metadata behavior under acquisition;
- GenApi behavior with vendor-provided XML and a real device;
- external consumer packaging and deployment of the C ABI dylib/Swift runtime.

A passing mock workflow is software evidence only. It is not a real CTI or hardware qualification.
