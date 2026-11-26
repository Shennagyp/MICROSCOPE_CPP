# Camera v2.0 — Simulator Qualification Plan

**Branch:** `camera-v.2.0`  
**Purpose:** Add vendor-independent software integration evidence for the GenTL consumer without claiming physical-camera qualification.  
**Status:** Candidate assessment and test-only harness implementation complete; no simulator execution or CI compile result is claimed yet.

## 1. Qualification boundary

A simulator-backed run may validate the real GenTL consumer path against a separately implemented Producer:
- CTI loading and required GenTL symbol compatibility;
- TL initialization/shutdown and interface/device discovery;
- device open/close and remote-port access;
- GenApi XML retrieval and feature/command access where the simulator exposes them;
- data-stream open/start/wait/stop, announced-buffer lifecycle, payload and metadata handling;
- frame projection into Common frame contracts;
- teardown, timeout and failure behavior.

It cannot establish physical USB/GigE transport behavior, camera firmware compatibility, hardware timing, real sensor output, physical disconnect/reconnect behavior, or AmScope SDK operation. Simulator and physical evidence must remain separate in the phase ledger.

## 2. Candidate assessment

### Allied Vision Vimba X Camera Simulator TL — technically suitable, licensing unresolved

Official documentation describes a genuine Camera Simulator Transport Layer, installed with Vimba X as `VimbaCameraSimulatorTL.cti`. It provides test images and configurable simulated camera models. Its feature set does not represent the full feature set of real camera models, and simulated timing does not exactly match hardware timing.

References:
- https://docs.alliedvision.com/vimba_x/camSimulator.html
- https://docs.alliedvision.com/vimba_x/CamSimTL.html

**Do not use this candidate for this project unless the license holder confirms the intended test use is permitted.** Allied Vision's published Vimba X terms restrict use to the stated intended-use scope, including restrictions concerning third-party cameras and using the software to create control software. A free download is not a general-purpose redistribution or test-use grant:
- https://www.alliedvision.com/de/protected-downloads/download/21

### The Imaging Source Virtual Camera GenTL Producer — first candidate for vendor clarification

The official package is named *GenTL Producer for Virtual Cameras*. It simulates one or more configurable camera devices and streams images. The current package page lists Windows 10/11, Ubuntu 22.04+, and macOS 26+; macOS support is described as an initial release.

References:
- https://dl-gui.theimagingsource.com/en_US/307fff26-bbdb-592d-a5f7-c1f8dba99e00/
- https://dl-gui.theimagingsource.com/en_US/packages/software/

The product page says it is used with IC Imaging Control 4 SDK or IC Capture 4. Unlike the TIS GigE/USB3 Producer pages, it does not expressly document third-party consumer compatibility. TIS's published EULA says the software may be used solely to operate TIS cameras, converters and frame grabbers:
- https://www.theimagingsource.com/de-de/service/end-user-license-agreement/

Before making this the automated simulator dependency, verify that the virtual Producer exposes a normal GenTL CTI usable by this project's consumer and that third-party consumer testing is permitted. Do not redistribute the vendor package or CTI.

### Aravis fake GigE Vision camera — open-source protocol-level alternative

`arv-fake-gv-camera` is a GigE Vision camera simulator that serves a GenICam-described fake device and can inject packet loss. It is not itself a GenTL CTI Producer. Its source also states that the optional XML argument is not an arbitrary GenICam XML loader: features must match the fake device's register implementation.

References:
- https://github.com/AravisProject/aravis
- https://github.com/AravisProject/aravis/blob/main/src/arvfakegvcamera.c

This is useful for GigE Vision protocol testing through a compatible GigE Vision transport implementation, but it is not a drop-in CTI for the current `Producer` API. Do not describe it as a CTI Producer.

### Basler pylon Camera Emulation — not a generic CTI assumption

Basler documents emulation through the pylon API and pylon Viewer, including test images and failed-buffer generation. The documentation does not establish that these emulation devices are visible to arbitrary third-party GenTL consumers. Do not use it as proof of generic CTI interoperability without a separate verified CTI route.

Reference:
- https://docs.baslerweb.com/camera-emulation

## 3. Existing project integration gap

The current C++ path is not yet a direct CTI consumer end-to-end:

- `camera::gentl::Producer::open` currently requires a `ConsumerBridge` and a CTI path.
- `ConsumerBridge` loads the external `libGenTLConsumerCAPI` ABI library; the C++ producer/device/stream code dispatches through that bridge.
- `GenTLProducerLibrary` is already present and directly loads GenTL exports from a CTI, but it is not wired into `Producer`.
- Existing automated tests use mock bridge/Producer fixtures. They do not prove that an independent vendor CTI can be loaded and streamed through the current project path.
- The separate Swift C ABI bridge's real dylib and CTI operation mapping remain unvalidated. Do not revive or modify the retired/private repository as part of this work.

This boundary must be resolved before claiming simulator-backed integration of the project's C++ GenTL path.

## 4. Bounded implementation proposal

1. **Implemented in source:** a test-only native C ABI shim around the existing `GenTLProducerLibrary`, exporting the five symbols expected by `ConsumerBridge`. It is not installed or exported and does not change production runtime selection.
2. **Implemented in source:** `camera_gentl_simulator_smoke`, which accepts explicit bridge and CTI paths, uses the existing `Producer` API, selects the first discovered device/stream, and checks GenApi XML, five complete frames, invocation of the Common frame
projection adapter, and orderly stop/close. Unsupported pixel layouts are
reported as opaque rather than treated as CTI failures. CMake registers it with CTest only when `CAMERA_GENTL_SIMULATOR_CTI` is supplied.
3. Exercise the actual `Producer`, `DeviceSession`, `DataStreamSession`, `GenTLFrameAdapter`, and, where the simulator's XML permits, the official GenApi adapter.
4. Record discovered device descriptors, Producer version, simulator version, CTI path/hash, feature descriptors, frame dimensions/format/payload length, frame count, and teardown result. Treat unsupported simulator features as capability observations, not test failures.
5. Prefer an opt-in/manual CI job or a separately provisioned runner if vendor download/license conditions prevent safe automatic installation. Do not make normal package CI depend on a vendor installer.
6. Keep AmScope provider source, tests, public contracts and runtime behavior frozen. Do not modify the Optika v0.9 application or restart Phase 5.

The source changes above have not yet been compiled or executed in this environment. The next evidence must come from the existing GitHub CI and an approved simulator CTI run. Do not report the harness as passing until those records exist.

## 5. Acceptance criteria

Simulator software qualification is accepted only when the exact branch commit has evidence for:
- successful CTI load through the test-only native shim;
- successful GenTL initialization, discovery and device open/close;
- successful remote-port access and GenApi XML retrieval when advertised;
- successful data-stream start, multiple complete frames, payload metadata checks and stop;
- no leaked handles or teardown failures;
- exact simulator/CTI/runtime versions and CI logs attached to the record.

If the simulator lacks a feature, pixel format, command, or failure mode, record that limitation. Do not infer coverage it did not exercise.

## 6. Phase accounting

Simulator-backed CTI integration is supplemental **software qualification** for Phase 7/8. It does not close:
- installed AmScope physical smoke;
- installed Qt preview physical smoke;
- physical GenTL camera smoke;
- Phase 4 visual acceptance;
- Phase 6 AmScope provider tests blocked by the freeze;
- SDK redistribution or release acceptance.

No local build, test, or debugger run is authorized in this environment. Use CI evidence or owner-run simulator evidence and update this document with exact results.
