# Camera v2.0 — Next AI Handoff

## 0. Current workstream — finish GenTL recovery, then cross-protocol reuse (27 November 2026)

The owner requires staged delivery: implement and test the meaningful GenTL functionality first, keep README/status/handoff current, then extend the validated protocol-neutral contracts through the other camera protocol adapters. Do not interleave AmScope implementation with unfinished GenTL work.

### Latest tested checkpoint

- Branch: `camera-v.2.0`.
- Latest tested implementation commit: `e03ddb000a1d716b95510ff6c531647c85e1610a`.
- GenTL bridge [workflow #45917806852](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45917806852): passed Intel x86_64 and Apple Silicon arm64, including official GenApi 3.5.1 build and integration tests.
- Package/external-consumer [workflow #45917806847](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45917806847): passed Intel x86_64 and Apple Silicon arm64, including package tests, relocation, clean external consumers and runtime loading/link checks.
- No physical camera is available. In-repository CTI tests are software integration evidence only, not independent vendor CTI or physical hardware qualification.

### Implemented and tested

1. **Frame layout:** `Frame` preserves complete payload, image offset, row padding and trailing padding. `imageData()` is no-copy; `strideBytes` includes row padding. GenTL layout accounting is overflow-safe and exact.
2. **PFNC:** Mono14p and all four Bayer14p patterns are mapped in addition to existing verified formats. Packed payloads remain byte-identical; no sample unpacking/demosaicing occurs.
3. **Acquisition integrity:** bounded-memory Common accounting distinguishes complete images, incomplete buffers, non-image payloads, empty payloads, host wait timeouts, missing IDs, forward gaps/missing IDs, duplicate IDs, out-of-order IDs, ambiguous backwards transitions, declared wraps and Producer starvation.
4. **Simulator faults and sustained recycling:** separate Producer underrun and host timeout injection; gap/duplicate/out-of-order/ambiguous ID scenarios; incomplete and empty payload scenarios; 4,096 frames recycled through four announced buffers.
5. **Multipart:** optional GenTL 1.5 `DSGetNumBufferParts` and `DSGetBufferPartInfo` entry points are resolved as optional. Part base/size are validated against the filled payload, converted to offsets before requeue, and represented as Common `FramePart` views over the one owned payload. The simulator exercises a 2D image part and a non-image part; image-bearing multipart buffers count as complete image deliveries. Producers without the optional functions remain usable for single-part acquisition.
6. **GenApi `IRegister`:** new Common `ICameraRegisterAccess` keeps raw byte-register operations separate from scalar `ICameraFeatureAccess`. `GenApiDeviceAccess::registers()` exposes only public Root-reachable `IRegister` nodes. The default transfer bound is 16 MiB, configurable at device-access construction; read/write access and exact byte length are enforced; reads use `IRegister::Get`, writes use `Set`; internal `IntReg` nodes remain hidden. Tests cover read-only read, read-only write rejection, RW write and byte-for-byte readback, wrong-length rejection, configurable transfer-limit rejection, scalar-as-register rejection and hidden internal register rejection.
7. **Lifecycle/recovery qualification:** queue starvation with explicit queue depletion and Producer counter increment, separate host timeout, buffer replenishment and recovery; three stop/reopen cycles; simulated stream interruption after two frames followed by stop/reopen and monotonic frame/timestamp continuation. Recovery CTests pass on both architectures.
### Protocol/feature ownership

- GenTL transport features: CTI/Producer and stream lifecycle, buffers/events, stream counters, multipart buffer parts.
- GenApi features: Boolean/Integer/Float/String/Enumeration, commands and public `IRegister`. GenApi is layered over GenTL; `IRegister` is not a GenTL API.
- PFNC features: pixel format identifiers and packed/unpacked sample layouts; not a transport protocol.
- Common contracts: frame payload/layout, part offset views, acquisition integrity, and opaque byte-register access.
- General optimization: bounded queues, one payload copy before buffer requeue, bounds validation, no repack/flatten copy, concurrency/lifecycle drain and recovery.

**Do not implement `IIntegerVector` as a standard GenApi node.** GenApi 3.5.1 does not define a public node interface with that name. The Common feature enum's `IntegerVector` member is not evidence of a standard GenApi interface. If an actual vendor XML exposes a vector-like feature, model it from that vendor schema only.

### Next work — strict order

**A. Cross-protocol reuse is now the next implementation stage.** Extend the validated Common frame-layout and acquisition-integrity contracts into AmScope/Amcam first, then other supported adapters. Preserve each backend's own buffer pool, lifecycle, timestamp unit/clock domain, sequence-counter width/wrap, provider counters and drop semantics. For Amcam, the SDK declares a 32-bit unsigned frame sequence and microsecond timestamp ticks; preserve those facts and do not invent a clock origin. Add fake-SDK tests for sequence gaps/duplicates/wrap, absent metadata, pool exhaustion/host drops and restart. Do not merge vendor lifecycle semantics into GenTL or Common.

**B. Independent Producer interoperability.** Keep it a separate external gate. The first-party CTI simulator proves our consumer path only. Use a current CTI Producer whose license permits generic third-party consumer testing; do not guess package URLs or claim independent compatibility without a passing run.

**C. Extend beyond AmScope/Amcam only after that adapter's Common-contract integration passes exact-commit CI.** Do not copy GenTL-specific types into Common. Each backend owns its counter/wrap semantics, timestamp base/frequency/clock domain and provider starvation counters. Keep UI, slides, stage and tile capture out of Camera.

### Invariants

- Exactly one GenTL filled-payload copy into owned storage before buffer requeue; no additional frame-sized padding normalization or multipart flattening.
- Bounded acquisition queues; `FrameStreamMailbox` remains latest-frame preview, not lossless WSI persistence.
- Camera owns acquisition, frame representation and transport telemetry only. No GUI, microscope configuration, slide/rack/tile scheduling, stitching or image-processing algorithms.
- Keep `Camera/GenTL/README.md`, `CAMERA_V2_DEVELOPMENT_STATUS.md` and this handoff synchronized with each verified milestone. Record source SHA, exact CI workflow IDs, architectures and evidence class.

> Read this file and `CAMERA_V2_DEVELOPMENT_STATUS.md` before changing source.
> The status ledger is the phase-history authority; this file is the operational continuation guide.

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Branch:** `camera-v.2.0`  
**Latest package implementation commit:** `75768c18ab77edad49373823b83e7d74797ab0db`  
**Latest package CI definition:** `bdd39bd218e1f10a80266c7ea677d4f416f15aba`  
**Package CI:** [workflow #45778844769](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45778844769) — both architectures passed 28/28 tests, relocation, all external consumer runtime checks, installed-link and architecture checks; the default-off vendor-runtime install contract passed on Apple Silicon.  
**Latest prior implementation CI:** [workflow #45770024248](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45770024248) — GenTL bridge and official GenApi 3.5.1 build/integration tests passed on Intel x86_64 and Apple Silicon arm64.  
**Phase status:** Phase 3 accepted; Phase 4 source/offscreen checks complete with visual acceptance pending and physical MU503 test postponed; Phase 5 GenTL/GenApi and simulator recovery CI-tested on both architectures; Phase 6 active for limited Common-contract integration into AmScope/Amcam by 27 November owner direction; Phase 7 package software CI passes on both architectures; physical-camera acceptance remains pending.  
**Latest AmScope preview UI-only corrections:** `1de091a1`, `fe897959`, `2b3c1a35`, `522d8648`, `6447996d` (main.cpp only; no provider source, tests, public contracts, Optika app or runtime behavior changed). These commits are not yet CI-verified.

---

## 1. Continuation authority

### Current UI correction checkpoint — 26 November 2026

The owner reported that the AmScope Qt preview had three defects: model capabilities appeared empty, preview resolution was presented as an index (`1`) instead of dimensions, and controls were grouped according to SDK function families.

The latest app-only correction:
- Adds a selected-camera model summary with reported model name, preview modes/dimensions, frame-speed range, I/O line count and fan range where supplied by the discovery descriptor.
- Makes the capability panel explain when the SDK did not provide a model descriptor. Unknown is not presented as Unsupported and no capability is fabricated.
- Displays preview-resolution enumeration choices as dimensions (for example `1280 × 960`) while retaining the canonical `index:widthxheight` symbol for backend writes/readback.
- Replaces direct `CameraFeatureGroup`-to-tab presentation with host-owned operator sections: Capture, Exposure, Image appearance, Sensor & hardware, and Advanced. The backend descriptor group remains unchanged for validation and lifecycle semantics.

**Important boundary:** this is a presentation correction, not proof that the SDK supplied a model descriptor for MU503. If the UI summary reports that the descriptor is absent, resolving that requires a provider-level model-query path; do not fabricate missing data. The provider freeze described in this historical 26 November note was superseded on 27 November only for the explicitly authorized Common frame-layout/acquisition-integrity integration; it does not authorize redesign of AmScope lifecycle or UI. The changes have not been compiled or run by the assistant; obtain exact-commit CI and owner visual inspection before accepting them.


The project owner explicitly corrected the previous continuation attempt:
- Original Camera Module Phases 0–8 remain governing.
- Qt is optional as a dependency, not a removed deliverable. Original Phase 3 (Qt streaming) and Phase 4 (reusable Qt UI) remain in scope.
- Phase 3 is accepted. Phase 4 source/offscreen work is complete; visual/workspace acceptance remains pending and the physical MU503 test is postponed by owner direction. Phase 4 is not marked accepted, but its pending acceptance does not block Phase 6 software work.
- Phase 5 GenTL/GenApi implementation is complete by owner direction; do not restart it.
- Phase 6 remains open and is now active for Common frame-layout/acquisition-integrity integration into AmScope/Amcam. The prior freeze was superseded on 27 November for this limited cross-protocol work.
- The owner explicitly authorized Phase 7 packaging and external-consumer work on 26 November 2026. On 27 November, the owner additionally authorized limited Common frame-layout/acquisition-integrity integration into AmScope/Amcam, superseding the earlier provider freeze only for that work.
- **The prior AmScope provider freeze is superseded only for Common frame-layout/acquisition-integrity integration.** Preserve vendor-specific lifecycle, callback/event queue, frame-pool leases, cache flush, SDK access and existing feature semantics. Do not modify Optika UI or `OPTIKA-B293-DUALSLIDE-APP-v.0.9`. Continue only on `camera-v.2.0`.
- The AmScope preview application exists in Camera/Amscope/app/main.cpp; do not treat it as obsolete or exclude it from the project.
- The retired AMSCOPE_DRIVER project/repository is not a current dependency or source of truth. Do not inspect, revive, clone, build against, or modify it.

If older documents conflict with this owner direction or the current status ledger, do not silently follow the older instruction. Record the conflict.

## 2. Phase status

| Phase | Status | Instruction |
|---|---|---|
| 0 — Baseline | Historically accepted | Do not reopen without a concrete regression. |
| 1 — Native AmScope driver | Historically accepted; hardening evidence remains | Do not rewrite. Reopen only for a reproduced defect or a specific Phase 6 gap. |
| 2 — Common camera contracts | Implemented substantially; residual hardware evidence remains | Reuse existing contracts. Change only for a demonstrated semantic defect. |
| 3 — Qt streaming separation | **Accepted by owner** | Preserve the one-session adapter and backend-neutral frame-stream boundary. |
| 4 — Reusable Qt UI | **SOURCE/OFFSCREEN COMPLETE; OWNER ACCEPTANCE PENDING** | FrameView, CameraControlPanel, ImageInfoBar, CameraFeatureEditor, CameraCommandPanel and CameraCapabilityPanel exist; the AmScope preview uses the feature editor, telemetry strip and model capability panel. CI passes 24/24 tests per architecture, including the application-level offscreen composition/dock-state smoke. Visual/workspace acceptance remains pending; physical MU503 UI test is postponed. Do not mark Phase 4 accepted, but do not block Phase 6 software work on it. |
| 5 — GenTL/GenApi integration | **Complete by owner direction** | Do not restart, duplicate, or replace the integration. External device qualification is separate Phase 8 evidence. |
| 6 — Capability, feature, command and model coverage | **ACTIVE — COMMON-CONTRACT INTEGRATION AUTHORIZED** | GenTL/GenApi coverage and recovery qualification are CI-tested. Extend Common frame-layout/acquisition-integrity semantics into AmScope/Amcam while preserving vendor lifecycle, callback, pool and cache ownership. The 27 Nov owner direction supersedes the earlier freeze only for this integration. |
| 7 — Packaging and external consumers | **ACTIVE BY OWNER AUTHORIZATION (26 NOV 2026)** | Commits `643319a`, `58f3790`, `49cc11e`, `fc3be40`, `22ee34f`, `c0f50da`, `75768c1` and `bdd39bd` add the package entry point, exports, component-aware config, relocation consumers, runtime API use, Qt/GenApi dependency isolation, an installed GenApi consumer, two-architecture runtime/architecture checks and vendor-runtime redistribution gating. Package workflow #45778844769 passed both architectures (28/28 CTest tests each), all relocated consumer/runtime checks, and the default-off vendor-runtime install contract on Apple Silicon. Installed-artifact physical AmScope and GenTL smoke tests remain required by Phase 7. Preserve AmScope vendor lifecycle/ownership semantics; the earlier source freeze is superseded only for Common frame-layout/acquisition-integrity integration. |
| 8 — Hardware qualification and release | Open | Preserve as a separate evidence/release milestone. |

The immediate software task is Phase 6 Common-contract integration into AmScope/Amcam on `camera-v.2.0`. Preserve the existing preview and reusable UI. Keep Phase 4 visual acceptance pending and physical MU503 testing postponed. Do not redesign AmScope lifecycle, callback/event queue, frame pool or cache ownership.

## 3. Repository boundaries

- `Camera/Common`: backend-neutral identity, capabilities, frame/layout types, feature/command contracts, validation and common frame-delivery primitives.
- `Camera/Amscope`: native AmScope C++ provider. `AmcamCamera` owns the AmScope SDK session and acquisition lifecycle.
- `Camera/GenTL`: GenTL 1.6 Consumer-side implementation, Producer/device/data-stream lifecycle, frame projection, remote-port/XML access and optional GenApi 3.5.1 adapters.
- Camera/Qt: optional Qt Core frame-stream library.
- Camera/QtWidgets: optional backend-neutral reusable widgets, including CameraFeatureEditor.
- Camera/Amscope/src/QtAmcamCamera.cpp and Camera/Amscope/app/main.cpp: AmScope Qt adapter and preview composition. The adapter composes one native session owner; the application does not open a second camera handle.
- Root CMake and CI: configure/build/test/install boundaries and cross-architecture validation.
- `docs/architecture/CAMERA_V2_DEVELOPMENT_STATUS.md`: current phase ledger and evidence history.
- `docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md`: historical v0.7 inventory, not current phase authority. Do not trust its old UI/test counts without re-verification.
- `Camera/GenTL/README.md`: API usage and implementation limitations; compare with current source before relying on individual details.

The separate Optika application owns its own presentation, UI, workspace, microscope stage/controller integration, slide mapping/scanning and image-processing workflows. This is separate from the AmScope Qt preview application in this repository, which is a Phase 4 deliverable and must be preserved.

## 4. Invariants to preserve

1. Common abstractions must represent genuinely shared semantics. Keep provider-specific errors, state enums, commands, feature names, units and identifiers provider-specific unless another backend proves the same meaning.
2. Capabilities are evidence, not optimism. An SDK function, GenApi node or UI editor does not prove support on a specific device.
3. Model policy may narrow provider support; it must never invent SDK functionality. Combine provider capability, model policy and runtime availability explicitly.
4. Features and commands are different contracts. Settings have typed value/read/write semantics; commands have issue/completion semantics.
5. Availability, readability and writability are distinct. A successful read does not prove writability.
6. Preserve exact units and numeric domains. Integer ranges/increments remain exact `int64_t`; do not round through `double`.
7. Readback is authoritative where promised. Do not report a requested value as applied before provider acceptance and required readback.
8. Asynchronous command issue and completion polling remain distinct. Do not silently claim completion.
9. Unknown is not true or false. Preserve unknown, unsupported and not-available distinctions; do not fabricate metadata.
10. Preserve provider ownership and lifecycle. Do not create duplicate camera sessions, GenApi node maps or serialization boundaries.
11. Preserve immutable frame ownership, payload lifetime, layout validation, overflow checks and opaque handling of unsupported pixel formats.
12. Keep Qt optional to Camera/Common and Amscope::Driver, but preserve Phase 3/4 Qt targets and the AmScope preview application. Do not move Qt dependencies into the native core.

## 5. Phase 4 acceptance and Phase 6 boundary

Phase 4 now includes CameraFeatureEditor in the preview feature tabs,
ImageInfoBar in the status bar, CameraCapabilityPanel in Camera Properties,
and the generic CameraCommandPanel for ICameraCommandAccess commands. The
AmScope preview's parameterized commands remain host-owned. The current Qt CI
checkpoint builds amscope_preview and passes 24/24 CTest tests on both
architectures. An application-level offscreen test also checks the document,
dock, status-bar, Output and capability-panel composition plus dock-state
save/restore. Only human visual inspection and current-revision physical
MU503 UI smoke remain; CI is not a substitute.

Owner direction on 26 November 2026: postpone the physical MU503 test and
proceed with Phase 7 packaging. Phase 4 remains pending acceptance, not
accepted. Phase 6 remains open at the frozen AmScope provider-test boundary;
its provider-neutral GenTL/GenApi work is complete for the current public
contract. See [CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md](CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md)
for the recorded audit. Do not repeat the inventory or reopen Phase 5.
AmScope provider work is now authorized only for Common frame-layout/acquisition-integrity integration. Preserve its vendor-specific lifecycle, callback/event queue, frame-pool leases, cache-flush and SDK ownership semantics.


Phase 4 includes CameraFeatureEditor in the preview feature tabs,
ImageInfoBar in the status bar, CameraCapabilityPanel in Camera Properties,
and CameraCommandPanel for no-argument ICameraCommandAccess commands. The
AmScope preview's parameterized commands remain host-owned. The current Qt CI
checkpoint builds amscope_preview and passes 24/24 CTest tests on both
architectures, including the application-level offscreen composition and
dock-state restore test. Human visual inspection remains pending; the owner
postponed the physical MU503 UI smoke. Do not mark Phase 4 accepted.

The Phase 6 audit is retained in
[CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md](CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md).
Provider-neutral GenTL/GenApi coverage and the discovery/vector decisions are
complete for the current public contract. AmScope provider-level feature,
command and model tests remain blocked by the explicit AmScope freeze. The
owner authorized Phase 7 on 26 November 2026; this does not waive or complete
Phase 6. Do not repeat the inventory, reopen Phase 5 or modify AmScope
provider source, tests, public contracts or runtime behavior.

## 6. GenTL implementation note — not permission to reopen Phase 5

At reviewed code baseline `808779f8`, both of these exist:
- `ConsumerBridge`, which is used by `Producer::open(...)`;
- `GenTLProducerLibrary`, a direct CTI symbol loader with its own CMake target/tests.

`Producer` still uses `ConsumerBridge`; the direct loader is not wired into the Producer/device/stream lifecycle. This is a known isolated source/documentation inconsistency. It does **not** authorize restarting Phase 5.

Do not automatically replace the bridge, add a second Producer, remove existing code, change the public API, or claim the direct loader is the active runtime path. If a later explicit task requires resolving this, first compare GenTL 1.6 function coverage, ABI correctness, initialization/shutdown, ownership, error handling, library lifetime and test evidence. Do not depend on the retired `AMSCOPE_DRIVER` repository.

Workflow #45733875474 proves the tested code at `808779f8` passes the configured cross-architecture mock/XML suite. It does not prove operation with a vendor CTI Producer or physical GenTL camera.

## 7. Evidence labels and qualification

Use these labels consistently:
- **Source implemented:** present in inspected source.
- **Automated test present:** a test exists; inspect its assertions and limits.
- **CI passed:** the exact commit and relevant job passed.
- **Hardware verified:** a physical-device record exists for the exact relevant implementation/configuration.
- **Owner accepted:** explicitly accepted by the project owner.
- **Deferred / unknown:** not established or postponed.

Phase 7 requires baseline physical-camera smoke using the installed AmScope and GenTL packages. Phase 8 expands this to exact-commit qualification across the declared provider/model matrix, long-duration and recovery evidence, complete deployment validation, vendor SDK redistribution review and release acceptance. Keep Phase 8 open until its criteria are met; do not use it to reopen Phase 5 implementation.

## 8. Safe working protocol

1. Confirm current branch and HEAD before every session; the branch may have advanced.
2. Read this handoff, the status ledger, relevant source and tests before proposing a change.
3. Continue Phase 7 by owner direction. Keep Phase 6 marked open/blocked by the AmScope freeze. Phase 4 acceptance remains pending; the physical MU503 test is postponed. Preserve the existing preview application and reusable widgets.
4. Do not modify unrelated projects, branches, hardware-control code or historical snapshots. Qt Phase 3/4 source is in scope; keep it in its existing targets and preserve the native core's Qt-free boundary.
5. Do not run local builds/tests/debugging in this environment. Use existing CI evidence; do not claim tests ran if they did not. Use/trigger CI only when authorized and appropriate.
6. Never force-push, rewrite history, delete branches or replace whole files without inspecting current contents and preserving unrelated work.
7. Make small isolated commits with descriptive messages. Update this handoff/status only when verified state changes.
8. After each change report exact files, package/target behavior, test/CI evidence, relocation result, remaining risk and next unfinished Phase 7 item. Keep this handoff and the status ledger synchronized.
9. If source contradicts this handoff, stop before destructive changes; inspect history and document the discrepancy.
10. Do not ask the owner to repeat information already recorded here. Continue the next concrete item in the existing plan.

## 9. Immediate next action — Phase 7

**Automated package acceptance is complete.** Workflow [#45778844769](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45778844769) passed at CI definition commit `bdd39bd218e1f10a80266c7ea677d4f416f15aba` on Intel x86_64 and Apple Silicon arm64. Each architecture passed 28/28 CTest tests, installation and prefix relocation, clean Common/GenTL/GenApi/AmScope/Qt consumer configure/build/run, runtime-link inspection and architecture checks. The separate default-off vendor-runtime installation check passed on Apple Silicon.

The next Phase 7 work is **not another package implementation pass**. It is the remaining release evidence:

1. Obtain owner-run installed-artifact physical smoke records for the non-Qt
   AmScope enumerate/open/read-setting/start/frame/stop/close path, the installed
   Qt preview and descriptor-driven controls, and an installed GenTL consumer
   with a real vendor CTI Producer and physical camera. These tests require
   physical hardware and are not represented by hosted CI results. Simulator
   runs may provide separate GenTL software-integration evidence, but must not
   be represented as physical-camera evidence. The separate Phase 4 MU503 UI
   test remains postponed by owner direction.
2. Resolve AmScope SDK redistribution rights from the actual SDK license terms
   or written vendor permission. The checked-in SDK tree has no license,
   notice or EULA. The public downloads page points users to software licensing
   terms, but the applicable SDK redistribution grant has not been obtained.
   Keep `CAMERA_MODULE_INSTALL_AMSCOPE_VENDOR_RUNTIME=OFF` for releases until
   this is resolved. CI's opt-in is limited to ephemeral internal validation.
3. Keep Phase 6 active for the 27 November authorized Common-contract integration
   into AmScope/Amcam. Preserve the vendor-specific lifecycle, callback/event
   queue, frame-pool leases, cache flush and SDK ownership semantics.
4. Keep Phase 4 visual/workspace acceptance pending and physical MU503 UI
   testing postponed. Do not mark Phase 4 accepted.
5. Phase 8 remains the broader exact-commit provider/model qualification,
   long-duration/recovery, deployment and release-acceptance phase.

Do not repeat the completed Phase 7 package build/consumer work unless package
inputs change or a regression requires it. The next software task is the
simulator-backed GenTL qualification plan in
[CAMERA_V2_SIMULATOR_QUALIFICATION.md](CAMERA_V2_SIMULATOR_QUALIFICATION.md).
First resolve the existing C++ CTI-consumer integration gap with a test-only
shim around the already-present `GenTLProducerLibrary`; do not alter the
AmScope provider or production public contracts. The Imaging Source virtual
Producer is the first vendor candidate to qualify, subject to CTI compatibility
and license confirmation. Do not use Vimba X until its published license
restrictions are confirmed compatible with this intended test use. Aravis is
a protocol-level fake GigE Vision camera, not a CTI Producer. Simulator runs
supplement but never replace physical hardware evidence.


## Simulator qualification status — 26 November 2026

The simulator work is now an authorized supplemental software-qualification
track. Read [CAMERA_V2_SIMULATOR_QUALIFICATION.md](CAMERA_V2_SIMULATOR_QUALIFICATION.md)
before continuing.

Source changes now present:
- `Camera/GenTL/tests/NativeConsumerBridge.cpp`: test-only C ABI shim over
  the existing direct `GenTLProducerLibrary` CTI loader.
- `Camera/GenTL/tests/SimulatorSmoke.cpp`: opt-in smoke using the existing
  `ConsumerBridge` and `Producer` APIs; it checks discovery, device open,
  GenApi XML retrieval, five complete frames, Common frame-projection invocation, stream stop and device close.
- `Camera/GenTL/CMakeLists.txt`: builds the test-only targets and registers
  the smoke only when `CAMERA_GENTL_SIMULATOR_CTI` is supplied.
- `Camera/GenTL/README.md`: records setup, coverage and evidence limits.

This does not modify AmScope, the production public API, or installed package
exports. It does not resolve or replace the separate Swift bridge in production.

**No local build/test/debug run has been performed and no simulator has been
executed.** The device-index selection source passed both replacement CI
workflows at commit `337bc58fc25de903b1c8e103261d9e89bc7d9c60`:
GenTL bridge #45781919824 and package/external-consumer #45781919810, each on
Intel x86_64 and Apple Silicon arm64.

Do not use a vendor simulator without explicit license clearance. TIS Virtual
Camera is the clearest dedicated CTI simulator product, but its published EULA
limits use to operating TIS products; obtain written permission for this
project's generic GenTL test use. Hikrobot's virtual-camera CTI is documented
in an older MVS manual, but current package/architecture support and third-party
consumer permission are unconfirmed. Vimba X is also license-blocked. Aravis
fake GV is not a CTI Producer and cannot be passed directly to this harness.

The smoke selects device index 0 by default, accepts an optional device index
for manual runs, and uses the first stream on the selected device. It remains
an initial integration probe, not a complete multi-device, feature/command,
fault-injection or performance qualification. Simulator
results remain separate from the outstanding physical AmScope and GenTL
hardware gates.


## Simulator continuation decision

The first-party, test-only CTI simulator is now implemented in source:
- `MockConsumerBridge.c` is built as `gentl_test_simulator.cti` with a simulator-only compile definition.
- The ordinary mock bridge target retains its strict timeout and pending-wait cancellation behavior.
- CMake registers `camera_gentl_inrepo_simulator_smoke` whenever `BUILD_TESTING=ON`.
- The smoke uses the native test shim and actual `GenTLProducerLibrary` dynamic loader, then calls the existing `ConsumerBridge` and `Producer` API.
- It currently represents one deterministic mock camera and repeated Mono8 frames; it is not vendor interoperability evidence.

**Next:** inspect the CI result for the exact source commit. If it fails, diagnose from workflow logs only; do not run local builds/tests/debuggers. If it passes, record that the in-repository CTI integration path is validated, then decide whether to obtain written vendor permission and a current external CTI for separate interoperability testing. Do not modify the Optika app, Phase 4/5 or physical-camera gates. AmScope is now in scope only for Common frame-layout/acquisition-integrity integration.

### Latest source increment — GenApi through CTI simulator

The GenApi integration executable receives both the native C ABI shim path and the CTI path. CMake points it at `gentl_test_simulator.cti`, so its comprehensive GenApi feature and command tests execute through `GenTLProducerLibrary` dynamic loading rather than the direct mock bridge. The simulator now also emits sequential frame IDs, increasing timestamps and changing Mono8 payloads; the smoke asserts five frames. Latest source commit `fc3132a62d59ca11aeb52026acaac70d3c85ddd5` passed the GenTL bridge workflow #45795223360 on Intel x86_64 and Apple Silicon arm64, including official GenApi integration. Package/external-consumer workflow #45795223288 remains queued. This is still in-repo integration evidence only.


## Simulator continuation checkpoint — two-device isolation

The current simulator increment adds a two-device fixture on one GenTL interface. The test-only CTI exposes device0/device1 with distinct display names and serial numbers, distinct remote port handles and register spaces, distinct stream handles, event handles and buffer ownership. The simulator tracks acquisition and event-unregistration state per stream and frame IDs independently per stream. The in-repository CTest invokes SimulatorSmoke with `--verify-two-device-isolation`; it opens both devices/streams concurrently, writes different data to the same remote register address, interleaves three frames from each stream and verifies independent IDs/timestamps/payloads, stops the first stream while asserting the second continues, then continues the original five-frame smoke. Relevant source commits are `71aafef0`, `8dc4dda6`, `f1e83e31`, `7fc3114e`, `c10163b7`, `3dc305f4`, `0bd38adb`, `6ae3e9c3`, and `83802772`; docs follow in `bf5e8524` and `27500718`. CI has not yet been confirmed for this increment. Do not claim it passes until the current branch's GenTL workflow passes on both architectures. Inspect any CI failure before making follow-up edits. No local build/test/debug was run. The ordinary mock fixture is intended to retain its one-device behavior and cancellation test; confirm this in CI. AmScope and Optika v0.9 remain untouched.
