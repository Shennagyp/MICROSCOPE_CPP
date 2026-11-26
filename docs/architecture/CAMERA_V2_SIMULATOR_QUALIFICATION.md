# Camera v2.0 — Simulator Qualification Plan

**Branch:** `camera-v.2.0`  
**Purpose:** Add vendor-independent software integration evidence for the GenTL consumer without claiming physical-camera qualification.  
**Status:** The external-CTI harness and a first-party test-only CTI simulator target are implemented in source. The device-index baseline passed both workflows at `337bc58fc25de903b1c8e103261d9e89bc7d9c60`. The new in-repository CTI simulator smoke is awaiting CI validation. No vendor simulator has been executed.

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

### The Imaging Source Virtual Camera GenTL Producer — technically strong, legally blocked pending written clarification

The official package is explicitly named *GenTL Producer for Virtual Cameras* and describes configurable one-or-more virtual devices with image streaming. Version 1.1.1.158 (16 November 2026) lists Windows 10/11, Ubuntu 22.04+ and macOS 26+. This is the clearest current product description of a dedicated simulator delivered as a GenTL Producer. The package page does not spell out the CTI filename or promise third-party consumer compatibility.

References:
- https://dl-gui.theimagingsource.com/en_US/307fff26-bbdb-592d-a5f7-c1f8dba99e00/
- https://dl-gui.theimagingsource.com/en_US/packages/software/

The product page says it is used with IC Imaging Control 4 SDK or IC Capture 4. Unlike the TIS GigE/USB3 Producer pages, it does not expressly document third-party consumer compatibility. TIS's published EULA says the software may be used solely to operate TIS cameras, converters and frame grabbers:
- https://www.theimagingsource.com/de-de/service/end-user-license-agreement/

The EULA grants use solely for operating TIS cameras, video signal converters, frame grabbers and related TIS products. Therefore, do not use this package to test this generic camera-control application without a written exception from TIS. Written confirmation must explicitly cover third-party GenTL consumers, virtual cameras, internal CI/test use and redistribution or installation on CI runners. Do not redistribute the vendor package or CTI. This is technically a strong candidate but not an approved dependency.

### Hikrobot MVS Virtual Device / `MvProducerVIR.cti` — additional CTI candidate

Hikrobot's MVS 4.0 user manual identifies `MvProducerVIR.cti` as a virtual-camera CTI and describes `MyProducerVIR.cti` as a selectable virtual Producer. The manual is from 2022, not current. MVS 5.1.0 release notes mention expanded Virtual Device support on Linux32 and AArch64; the current MVS 5.1.1 download page lists Windows, Linux x86 and AArch64 packages, but does not establish that the virtual CTI is available in each package or on Linux x86_64.

References:
- https://www.hikrobotics.com/en/machinevision/service/download/
- https://cncu.co.za/FlatCUT-Flatbed-Cutting-Machine/MDR%20Oscillating%20Blade%20CNC%20Router/Hikrobot%20MVS%20%28Machine%20Vision%20Software%29%20STD%204.0.0%20%28221208%29%20Installation/Applications/Win64/doc/User_Manual_English/pdf.pdf

The CTI filename and GenTL listing make this worth vendor clarification, but the old manual does not establish current package availability, third-party GenTL interoperability, Linux x86_64 support or license permission for this project's use. It is not established as a macOS simulator. Treat it as an unverified Windows/Linux candidate, not a selected dependency. Ask Hikrobot to confirm the exact current CTI file, host architectures, third-party consumer interoperability and permission for internal CI testing.

### Critical Link GenTL Producer simulation — secondary candidate

Critical Link documents a simulated-camera mode in its GenTL Producer and provides a `sim_camera.xml` configuration. Its documentation also states that other application software can use the Critical Link GenTL Producer rather than the vendor Viewer.

References:
- https://support.criticallink.com/redmine/projects/gentl_apps/wiki/Running_the_GenTL_producer_in_simulated_mode
- https://support.criticallink.com/redmine/projects/gentl_apps/wiki/Wiki

This is a plausible CTI-level test route, but platform availability, current GenTL version and simulator package availability must be confirmed. Critical Link's published terms describe a limited, non-transferable license to use its General Software and Third Party Software solely with respect to its Products. That wording is not a generic grant for testing an unrelated camera-control application; do not use this Producer for this project without written permission covering the intended test. Do not assume its Windows-oriented Viewer setup means the Producer itself is available for the project's macOS runner.

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

The device-index source change in `Camera/GenTL/tests/SimulatorSmoke.cpp` passed both replacement GitHub Actions workflows at tested commit `337bc58fc25de903b1c8e103261d9e89bc7d9c60`: [GenTL bridge #45781919824](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45781919824) and [package/external-consumer #45781919810](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45781919810). Both workflows passed Intel x86_64 and Apple Silicon arm64. The optional simulator test was not registered because no CTI path was supplied. No local build/test/debug run or simulator execution has occurred.


### GenApi feature/command coverage now uses the CTI loader

The official GenApi integration test was changed to accept the CTI path as an argument. CMake now supplies `gentl_native_consumer_bridge_fixture` plus `gentl_test_simulator_cti`, rather than the direct mock C ABI bridge. Consequently the existing broad GenApi integration suite now runs its feature/command operations through the same direct CTI dynamic loader and `Producer` path used by the simulator smoke. Coverage includes feature discovery, nested categories, unavailable selectors, integer/float/string/enum reads and writes, range/increment validation, command enumeration/execution, read-only access, backend port failures, and malformed XML recovery. This source change is awaiting the current CI run; it is not yet validated.

### First-party in-repository CTI simulator — implemented, CI pending

The existing `tests/MockConsumerBridge.c` exports the GenTL Producer C
symbols already resolved by `GenTLProducerLibrary::load`. The existing
`camera_gentl_cti_loader_tests` proves that this shared library can be opened
as a CTI and that the required exports resolve. The same fixture is now built
a second time as `gentl_test_simulator.cti` with
`CAMERA_GENTL_SIMULATOR_MODE=1`.

Simulator mode relaxes only two assumptions that were deliberately strict in
the unit-test mock:
- the fixed 2-second discovery timeout check;
- the long EventGetData wait branch used to test cancellation.

The ordinary mock bridge target is compiled without this definition, so its
existing cancellation tests retain their prior behavior. CMake registers
`camera_gentl_inrepo_simulator_smoke` unconditionally when
`BUILD_TESTING=ON`. It uses the native test shim and the actual
`GenTLProducerLibrary` dynamic loader to load the test CTI, then exercises
the public `ConsumerBridge` and `Producer` path: discovery, device open,
GenApi XML retrieval, data-stream acquisition, five frame deliveries,
Common-frame projection and orderly teardown.

The in-repository simulator is deterministic and license-independent. It
currently models one mock camera, one interface, one data stream and repeated
Mono8 image buffers. It is not an independent vendor implementation and does
not qualify multiple physical transports, vendor-specific features, real
hardware timing, or physical cameras. Its purpose is to turn the C++ CTI
loading and consumer path into a continuously executed integration test.

The new target and test have not been built or executed locally. GitHub Actions
must validate compilation and the simulator smoke. A CI pass will establish
in-repository CTI integration only; a separate approved vendor CTI remains
necessary for third-party interoperability evidence.

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


## Candidate selection conclusion — 26 November 2026

The current vendor evidence does not justify downloading or executing any external simulator under the project's existing constraints:
- TIS Virtual Camera is a genuine dedicated GenTL Producer with a current package, but the published EULA restricts use to operating TIS products. Obtain written permission before using it as a generic test simulator.
- Hikrobot documents a virtual-camera CTI in an older MVS manual, but current CTI availability, Linux x86_64 support, third-party interoperability and license permission are unresolved.
- Vimba X has a genuine simulator CTI, but its license restrictions remain unresolved for this purpose.
- Aravis fake GV is a network GigE Vision simulator, not a CTI. It can only qualify a separate GEV transport implementation, not this direct CTI loader.

Do not create a green CI gate that silently skips the simulator. Once a vendor approves a CTI, run it on a compatible isolated runner and preserve the exact package/version/hash and test logs. The first-party, test-only simulated CTI fixture is now implemented and awaits CI validation. Keep its evidence separate from vendor interoperability and do not use it to replace vendor or physical qualification.
