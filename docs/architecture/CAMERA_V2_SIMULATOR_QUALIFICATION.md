# Camera v2.0 — Simulator Qualification Plan

**Branch:** `camera-v.2.0`  
**Purpose:** Add vendor-independent software integration evidence for the GenTL consumer without claiming physical-camera qualification.  
**Status:** Test-only harness source is implemented. CI passed for the initial harness baseline at `7d3a83b4ff87d5a9ee24f04a26f86d00ab6d6976`; the subsequent device-index selection change is awaiting its own CI result. No simulator has been executed.

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

### Hikrobot MVS Virtual Device / `MvProducerVIR.cti` — additional CTI candidate

Hikrobot's MVS documentation identifies `MvProducerVIR.cti` as its virtual-camera GenTL Producer. The MVS user guide describes adding virtual camera models and also selecting the virtual Producer CTI through the application's GenTL device list. The current MVS download page lists release 5.1.1 (build 20261015) and documents an expanded Virtual Device tool, including Linux32 and AArch64 support.

References:
- https://www.hikrobotics.com/en/machinevision/service/download/
- https://cncu.co.za/FlatCUT-Flatbed-Cutting-Machine/MDR%20Oscillating%20Blade%20CNC%20Router/Hikrobot%20MVS%20%28Machine%20Vision%20Software%29%20STD%204.0.0%20%28221208%29%20Installation/Applications/Win64/doc/User_Manual_English/pdf.pdf

This is now a stronger practical candidate than a simulator exposed only through a vendor SDK because the documented virtual device is represented by a CTI filename. However, the available documentation does not yet establish that `MvProducerVIR.cti` is supported by arbitrary third-party GenTL consumers, nor does it settle the license scope for this project's test use. It is also not established as a macOS simulator. Treat it as a **Linux/Windows candidate pending vendor confirmation**, not as a selected or validated dependency. Ask Hikrobot to confirm third-party GenTL consumer interoperability, supported MVS/CTI versions and permission for internal testing.

### Critical Link GenTL Producer simulation — secondary candidate

Critical Link documents a simulated-camera mode in its GenTL Producer and provides a `sim_camera.xml` configuration. Its documentation also states that other application software can use the Critical Link GenTL Producer rather than the vendor Viewer.

References:
- https://support.criticallink.com/redmine/projects/gentl_apps/wiki/Running_the_GenTL_producer_in_simulated_mode
- https://support.criticallink.com/redmine/projects/gentl_apps/wiki/Wiki

This is a plausible CTI-level test route, but platform availability, current GenTL version, simulator package availability and licensing must be confirmed. Do not assume its Windows-oriented Viewer setup means the Producer itself is available for the project's macOS runner.

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
2. **Implemented in source:** `camera_gentl_simulator_smoke`, which accepts explicit bridge and CTI paths, uses the existing `Producer` API, selects device index 0 by default (or a specified index for manual runs), uses the first stream on that device, and checks GenApi XML, five complete frames, invocation of the Common frame projection adapter, and orderly stop/close. Unsupported pixel layouts are
reported as opaque rather than treated as CTI failures. CMake registers it with CTest only when `CAMERA_GENTL_SIMULATOR_CTI` is supplied.
3. Exercise the actual `Producer`, `DeviceSession`, `DataStreamSession`, `GenTLFrameAdapter`, and, where the simulator's XML permits, the official GenApi adapter.
4. Record discovered device descriptors, Producer version, simulator version, CTI path/hash, feature descriptors, frame dimensions/format/payload length, frame count, and teardown result. Treat unsupported simulator features as capability observations, not test failures.
5. Prefer an opt-in/manual CI job or a separately provisioned runner if vendor download/license conditions prevent safe automatic installation. Do not make normal package CI depend on a vendor installer.
6. Keep AmScope provider source, tests, public contracts and runtime behavior frozen. Do not modify the Optika v0.9 application or restart Phase 5.

The initial harness baseline passed both GitHub Actions workflows at commit `7d3a83b4ff87d5a9ee24f04a26f86d00ab6d6976` (GenTL bridge CI and package/external-consumer CI). The optional simulator test was not registered because no CTI path was supplied. The follow-up device-index selection source change is a later commit and must pass its own CI. No local build/test/debug run or simulator execution has occurred.

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
