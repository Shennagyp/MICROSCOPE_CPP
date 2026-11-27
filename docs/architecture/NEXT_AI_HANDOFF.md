# Camera v2.0 — Next AI Handoff

## 0. Current workstream — WSI acquisition integrity and GenTL completion (27 November 2026)

The owner requires remaining camera capabilities to be implemented in a staged sequence: finish GenTL-side functionality and tests first, keep documentation/handoff synchronized, then extend validated protocol-neutral semantics through other protocol adapters.

### Ownership classification

- **GenTL transport:** CTI/Producer/interface/device/stream handles, buffers/events, transport counters, frame metadata and multipart buffer-part APIs.
- **GenApi/GenICam:** typed feature nodes and register nodes; layered over transport, not a GenTL feature itself.
- **PFNC:** pixel-format identifiers and packed/unpacked raster layouts; not transport operations.
- **Camera.Common:** backend-neutral frame layout, part views, sequence/integrity telemetry and common byte-register values.
- **General implementation quality:** throughput, allocation/copy control, bounds validation, concurrency, teardown/drain and recovery. These are cross-protocol constraints rather than protocol features.

### Latest accepted source and CI

- Branch: `camera-v.2.0`.
- Latest accepted source commit: `23d8a670eea14928b4c011391523cb71f7343872`.
- GenTL bridge [workflow #45914739939](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45914739939): passed on Intel x86_64 and Apple Silicon arm64, including GenTL adapter CTests and official GenApi 3.5.1 integration.
- Package/external-consumer [workflow #45914739827](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45914739827): passed on Intel x86_64 and Apple Silicon arm64, including install/relocation and clean external consumers.
- AmScope driver compatibility workflow #45914566111 was also triggered by the additive Common enum change; this is a dependent-build check, not an AmScope feature rollout. Its current status should be checked if needed.
- No physical camera is available. The in-repository CTI remains software integration evidence, not vendor-CTI or hardware qualification.

### Implemented and verified

1. **Frame layout** — full payload, image offset, row padding and trailing padding are represented without stripping/rearrangement. `imageData()` is the no-copy image-start pointer; `strideBytes` includes X padding.
2. **Pixel formats** — supported PFNC single-raster formats include Mono8, unpacked Mono10/12/14/16, packed Mono10p/12p/14p, RGB8 and Bayer GR/RG/GB/BG in 8-bit, unpacked 10/12/14/16-bit and packed 10p/12p/14p. Mono14p and Bayer14p tests cover 14-bit packed row sizing, odd-width rows, invalid payload length, image offset and padding. The bytes remain unchanged; no demosaic or unpack is added.
3. **Acquisition integrity** — the Common tracker counts complete/incomplete/non-image/invalid buffers; optional sequence IDs; forward gaps/missing IDs; duplicate IDs; out-of-order and ambiguous backward transitions; configured wrap models; host wait timeouts; and Producer-reported starvation.
4. **GenTL stream statistics** — `DataStreamSession::statistics()` exposes optional typed Producer counters and the Common integrity snapshot. `EventGetData` timeout is distinct from `STREAM_INFO_NUM_UNDERRUN`. Only `TLTYPE=GEV` receives the documented one-based 16-bit GigE Vision counter model.
5. **Simulator fault qualification** — forward frame-ID gap; duplicate; out-of-order under GEV; ambiguous backwards transition under an unspecified transport; separate Producer underrun and host wait timeout; incomplete image buffer; empty image payload; timestamps/payload progression independent of transport IDs.
6. **Sustained recycling** — a 4,096-frame simulator test cycles four announced buffers and verifies IDs, timestamps, complete image delivery, zero host timeouts/Producer underruns and a fully replenished queue at completion.

### Accepted verification by increment

- Frame-layout commit `4e470af2e3d2bb069cf155ade168b975e44513da`: workflows #45910624279 and #45910624296, both architectures.
- Acquisition-integrity/statistics commit `0d3328ad4636436cf05f07eda02af0cf31577058`: workflows #45912574859 and #45912574857, both architectures.
- Sequence-fault commit `992d4162a4aa16fa8215370ba4ea1b8fd5673486`: workflows #45913589920 and #45913589940, both architectures.
- Incomplete/empty commit `7288ac678399e7d7b76fda69e8109f4cd702dee8`: workflows #45913972311 and #45913972309, both architectures.
- Sustained-buffer-recycling commit `259114843ceadff5e3583dbc362a5984d880633c`: workflows #45914231242 and #45914231159, both architectures.
- PFNC packed-14 commit `23d8a670eea14928b4c011391523cb71f7343872`: workflows #45914739939 and #45914739827, both architectures.

### Next implementation and test order

**A — Complete lifecycle/recovery qualification.** Add true queue depletion/starvation and recovery, repeated start/stop, stop/reopen, interrupted-stream restart and bounded queue stress under delayed Consumer delivery. Existing 4,096-frame test proves repeated buffer cycling only; it does not prove long-duration hardware acquisition or persistent WSI tile storage.

**B — GenTL multipart.** Add optional dispatch for `DSGetNumBufferParts` / `DSGetBufferPartInfo`. These entry points are optional in GenTL and may return `GC_ERR_NOT_IMPLEMENTED`; a Producer that lacks them must still work for ordinary single-part streams. For each reported part, retrieve typed metadata; validate its part base address and byte size against the filled payload using overflow-safe address/range checks; convert the pointer to an offset before buffer requeue; store offset/length views in the `DataStreamFrame` and Common `Frame`. Common part metadata should include the standardized data type, format namespace/ID, dimensions, offsets/padding, delivered height and source ID where available. Use one owned payload as the byte backing; never flatten or repack.

**C — GenApi public IRegister.** Implement a bounded byte-register access contract and optional GenApi adapter for public `IRegister`: length bounds, access mode, safe caller buffer validation, node-map serialization and XML-backed tests. Scalar `IInteger` is already supported.

**Terminology:** GenApi 3.5.1 exposes scalar `IInteger` and public `IRegister`; it does not define a standard public node interface named `IIntegerVector`. Do not invent a fake mapping. If camera XML includes a vector-like feature, model it as a vendor extension from actual schema evidence.

**D — Independent interoperability gate.** The in-repo CTI validates the consumer path but does not prove compatibility with an independent vendor CTI. Select a supported/appropriately licensed external Producer when available; do not assume redistribution or generic-load rights.

**E — Cross-protocol extension.** Only after A–C are implemented and pass exact-commit CI on both architectures should validated Common frame-layout/integrity semantics be mapped through AmScope/Amcam and other supported camera protocols. Backend transport counters, clock domains and frame-ID wrap semantics remain in their adapters. No Optika UI, slide/rack/tile scanning, stitching or image-processing code enters Camera.

### Performance/lifecycle invariants

- Copy each filled GenTL payload once into owned storage before its announced buffer is requeued; do not add another frame-sized copy to strip padding or flatten multipart data.
- Keep acquisition queues bounded and lifecycle drain/recovery observable; `FrameStreamMailbox` is preview handoff, not a lossless WSI tile archive.
- Update `Camera/GenTL/README.md`, the status ledger and this handoff with each accepted source/CI milestone. Separate source implementation, simulator tests, cross-architecture CI, independent CTI and physical hardware evidence.
> Read this file and `CAMERA_V2_DEVELOPMENT_STATUS.md` before changing source.
> The status ledger is the phase-history authority; this file is the operational continuation guide.

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Branch:** `camera-v.2.0`  
**Latest package implementation commit:** `75768c18ab77edad49373823b83e7d74797ab0db`  
**Latest package CI definition:** `bdd39bd218e1f10a80266c7ea677d4f416f15aba`  
**Package CI:** [workflow #45778844769](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45778844769) — both architectures passed 28/28 tests, relocation, all external consumer runtime checks, installed-link and architecture checks; the default-off vendor-runtime install contract passed on Apple Silicon.  
**Latest prior implementation CI:** [workflow #45770024248](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45770024248) — GenTL bridge and official GenApi 3.5.1 build/integration tests passed on Intel x86_64 and Apple Silicon arm64.  
**Phase status:** Phase 3 accepted; Phase 4 source/offscreen checks complete with visual acceptance pending and physical MU503 test postponed; Phase 5 complete by owner direction; Phase 6 open but blocked by the AmScope freeze; Phase 7 active by explicit owner authorization on 26 November 2026. Package software CI passes on both architectures; physical-camera acceptance remains pending.  
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

**Important boundary:** this is a presentation correction, not proof that the SDK supplied a model descriptor for MU503. If the UI summary reports that the descriptor is absent, resolving that requires a provider-level model-query path; the AmScope provider freeze remains in force. Do not fabricate the missing data or bypass the session owner. The changes have not been compiled or run by the assistant; obtain exact-commit CI and owner visual inspection before accepting them.


The project owner explicitly corrected the previous continuation attempt:
- Original Camera Module Phases 0–8 remain governing.
- Qt is optional as a dependency, not a removed deliverable. Original Phase 3 (Qt streaming) and Phase 4 (reusable Qt UI) remain in scope.
- Phase 3 is accepted. Phase 4 source/offscreen work is complete; visual/workspace acceptance remains pending and the physical MU503 test is postponed by owner direction. Phase 4 is not marked accepted, but its pending acceptance does not block Phase 6 software work.
- Phase 5 GenTL/GenApi implementation is complete by owner direction; do not restart it.
- Phase 6 remains open. Its provider-neutral GenTL/GenApi work is complete for the current public contract; AmScope provider-level feature/command/model tests remain blocked by the explicit AmScope freeze.
- The owner explicitly authorized Phase 7 packaging and external-consumer work on 26 November 2026, without lifting the AmScope freeze. Phase 7 may modify CMake packaging metadata, package config, consumer fixtures, CI and documentation.
- **AmScope provider source, tests, public contracts and runtime behavior remain frozen. Do not write to `OPTIKA-B293-DUALSLIDE-APP-v.0.9`. Continue only on `camera-v.2.0`.**
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
| 6 — Capability, feature, command and model coverage | **OPEN; BLOCKED AT FROZEN AMSCOPE TESTS** | Provider-neutral GenTL/GenApi work and discovery/vector decisions are complete for the current public contract. AmScope provider-level feature/command/model tests remain blocked. Do not modify AmScope provider source, tests, contracts or behavior; do not mark Phase 6 complete. |
| 7 — Packaging and external consumers | **ACTIVE BY OWNER AUTHORIZATION (26 NOV 2026)** | Commits `643319a`, `58f3790`, `49cc11e`, `fc3be40`, `22ee34f`, `c0f50da`, `75768c1` and `bdd39bd` add the package entry point, exports, component-aware config, relocation consumers, runtime API use, Qt/GenApi dependency isolation, an installed GenApi consumer, two-architecture runtime/architecture checks and vendor-runtime redistribution gating. Package workflow #45778844769 passed both architectures (28/28 CTest tests each), all relocated consumer/runtime checks, and the default-off vendor-runtime install contract on Apple Silicon. Installed-artifact physical AmScope and GenTL smoke tests remain required by Phase 7. Keep the AmScope freeze intact. |
| 8 — Hardware qualification and release | Open | Preserve as a separate evidence/release milestone. |

The immediate task is Phase 7 packaging and external-consumer verification on `camera-v.2.0`. Preserve the existing preview and reusable UI. Keep Phase 4 visual acceptance pending and physical MU503 testing postponed. Phase 6 remains open at the frozen AmScope provider-test boundary; do not modify AmScope provider source, tests, contracts or runtime behavior.

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
AmScope provider source, tests, public contracts and runtime behavior remain
frozen.


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
3. Keep Phase 6 open/blocked at the frozen AmScope provider-test boundary.
   Do not modify AmScope provider source, tests, public contracts or runtime
   behavior. Phase 7 CMake packaging metadata does not lift that freeze.
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

**Next:** inspect the CI result for the exact source commit. If it fails, diagnose from workflow logs only; do not run local builds/tests/debuggers. If it passes, record that the in-repository CTI integration path is validated, then decide whether to obtain written vendor permission and a current external CTI for separate interoperability testing. Do not modify AmScope, the Optika app, Phase 4/5, or physical-camera gates.

### Latest source increment — GenApi through CTI simulator

The GenApi integration executable receives both the native C ABI shim path and the CTI path. CMake points it at `gentl_test_simulator.cti`, so its comprehensive GenApi feature and command tests execute through `GenTLProducerLibrary` dynamic loading rather than the direct mock bridge. The simulator now also emits sequential frame IDs, increasing timestamps and changing Mono8 payloads; the smoke asserts five frames. Latest source commit `fc3132a62d59ca11aeb52026acaac70d3c85ddd5` passed the GenTL bridge workflow #45795223360 on Intel x86_64 and Apple Silicon arm64, including official GenApi integration. Package/external-consumer workflow #45795223288 remains queued. This is still in-repo integration evidence only.


## Simulator continuation checkpoint — two-device isolation

The current simulator increment adds a two-device fixture on one GenTL interface. The test-only CTI exposes device0/device1 with distinct display names and serial numbers, distinct remote port handles and register spaces, distinct stream handles, event handles and buffer ownership. The simulator tracks acquisition and event-unregistration state per stream and frame IDs independently per stream. The in-repository CTest invokes SimulatorSmoke with `--verify-two-device-isolation`; it opens both devices/streams concurrently, writes different data to the same remote register address, interleaves three frames from each stream and verifies independent IDs/timestamps/payloads, stops the first stream while asserting the second continues, then continues the original five-frame smoke. Relevant source commits are `71aafef0`, `8dc4dda6`, `f1e83e31`, `7fc3114e`, `c10163b7`, `3dc305f4`, `0bd38adb`, `6ae3e9c3`, and `83802772`; docs follow in `bf5e8524` and `27500718`. CI has not yet been confirmed for this increment. Do not claim it passes until the current branch's GenTL workflow passes on both architectures. Inspect any CI failure before making follow-up edits. No local build/test/debug was run. The ordinary mock fixture is intended to retain its one-device behavior and cancellation test; confirm this in CI. AmScope and Optika v0.9 remain untouched.
