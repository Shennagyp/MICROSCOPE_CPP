# Camera v2.0 — Continuation Handoff

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Working branch:** `camera-v.2.0`  
**Documentation date:** 27 November 2026  
**Architecture reference:** [CAMERA_V2_ARCHITECTURE_AND_OOP.md](CAMERA_V2_ARCHITECTURE_AND_OOP.md)  
**Status authority:** [CAMERA_V2_DEVELOPMENT_STATUS.md](CAMERA_V2_DEVELOPMENT_STATUS.md)

This is the operational handoff. Read the architecture reference for the complete module map, class relationships, public API semantics, lifecycle diagrams, memory rules, code examples and limitations. The status ledger is the evidence history. Some older sections in historical documents describe earlier sequencing decisions; the current instructions in this handoff supersede them where stated.

---

## 1. Current branch state

Latest branch HEAD before this documentation update: `f2ec068ee0150a953b0749d856b8d3a72bd1152a`.

Recent commits:

| Commit | Change | Scope |
|---|---|---|
| `f2ec068` | Keep only dock launcher buttons floating over preview | Optika presentation only |
| `0e9a734` | Render dock launchers as separate floating buttons | Optika presentation only |
| `613fae3` | Keep dock content in layout; float launcher only | Optika presentation only |
| `12a832d` | Combined AmScope and GenTL discovery test | Camera preview diagnostic/build |
| `46aba06` | Expose Common acquisition integrity in AmScope adapter | Camera provider integration |
| `e03ddb0` | GenTL interruption/recovery qualification | GenTL core |

The latest three Optika commits do not change camera acquisition or the reusable C++ camera libraries.

## 2. Architecture in one page

- `Camera/Common`: backend-neutral identity/capability models, typed feature/command/register contracts, immutable frame representation, validation, acquisition-integrity accounting, latest-frame mailbox and host-side rate estimator.
- `Camera/Amscope`: native Amcam provider. `AmcamCamera` owns the vendor SDK handle and vendor-specific lifecycle.
- `Camera/GenTL`: native C++ GenTL 1.6 Consumer, CTI loader, Producer/device/data-stream sessions and Common frame adapter.
- Optional GenApi 3.5.1: XML-backed typed feature, command and public IRegister access over a GenTL device.
- Optional `Camera/Qt` and `Camera/QtWidgets`: generic Qt frame delivery and reusable UI.
- Optional AmScope Qt adapter and `amscope_preview`: separate AmScope preview application.
- `Application/OptikaB293`: separate SwiftUI/AppKit microscope application consumer and camera bridge.

**Detailed OOP diagrams, sequence diagrams and code samples:** [Camera v2.0 Architecture and OOP Reference](CAMERA_V2_ARCHITECTURE_AND_OOP.md).

Camera owns discovery, camera session lifecycle, acquisition, frame representation, camera features/commands and transport telemetry. It does not own microscope optics, stage/controller, slide/rack mapping, WSI tile scheduling, durable tile storage, stitching or image processing.

## 3. Verified software checkpoints

### GenTL / GenApi

At `e03ddb000a1d716b95510ff6c531647c85e1610a`:
- [GenTL bridge #45917806852](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45917806852) passed Intel x86_64 and Apple Silicon arm64.
- [Package/external consumers #45917806847](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45917806847) passed both architectures.
- Tested software includes GenTL lifecycle/recovery, frame layout and PFNC projection, acquisition-integrity accounting, multipart parts, GenApi public IRegister access, simulated queue starvation/host timeout distinction, stream interruption/restart and sustained buffer recycling.

### AmScope Common-contract integration

At `46aba0614d56f65629bc25a688adec0f1cfe8f3c`:
- Amcam acquisition is integrated with Common acquisition-integrity accounting.
- Amcam sequence semantics remain a zero-based 32-bit unsigned counter; timestamps retain microsecond ticks and unspecified clock origin.
- Host frame-pool exhaustion remains separate from transport/Producer underruns.
- [AmScope workflow #45918559518](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45918559518) passed five jobs across Intel and Apple Silicon.
- [Package workflow #45918559602](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45918559602) passed both architectures.

This completes the specifically authorized Common acquisition-integrity integration into AmScope. It does not complete every Phase 6 AmScope feature/command/model qualification item.

### Combined discovery

At `12a832de98b1c082de0c432ecbc10f24c6fdada3`:
- `amscope_preview --list-cameras` independently queries native AmScope enumeration and GenTL `ProducerCatalog`.
- A failure in one backend does not suppress the other backend's discovery.
- A first-party CTI fixture is used by the combined discovery CTest.
- [AmScope workflow #45927150635](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45927150635) passed five jobs.
- [Package workflow #45927150649](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45927150649) passed Intel and Apple Silicon.

The project owner separately reported a local Release run with 43/43 tests passing and physical MU503 discovery. The exact local SHA was not supplied in the conversation; record this as owner-reported, not independently reproduced.

### Latest Optika UI build

At `f2ec068ee0150a953b0749d856b8d3a72bd1152a`, [workflow #45937445092](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45937445092) passed all five jobs, including the SwiftUI/Qt frontend lane. This is build/test evidence for the UI source change, not human visual acceptance.

## 4. Invariants to preserve

### AmScope
- One `AmcamCamera` owns one `HAmcam` handle.
- Provider-specific state machine: Closed, Open, Starting, Streaming, Stopping, Faulted.
- SDK callback -> fixed-capacity 256-event FIFO -> one acquisition worker -> `Amcam_PullImageV4`.
- Callback only enqueues event codes; no SDK pull, lifecycle or UI call from callback context.
- Four reusable immutable frame slots; if all are retained, pull into a discard buffer and count a host drop rather than block.
- Generation gate prevents stale acquisition frames after a superseding session.
- Successful Stop creates a cache-flush obligation; failed flush is retried before the next Start.
- Recoverable Stop failure retains active resources; terminal Close is independent of Stop/flush success.
- RAW remains identified by `amcam.fourcc`; never relabel as PFNC or demosaic in the provider.
- Native trigger, GPIO/I/O, still capture and accessory operations retain Amcam-specific semantics.

### GenTL / GenApi
- Production route: `Producer::open(ctiPath)` -> `GenTLProducerLibrary` -> direct CTI GenTL exports.
- `ConsumerBridge` is optional explicit bridge injection, not a required production runtime.
- `ProducerCatalog` scans `GENICAM_GENTL64_PATH`, canonicalizes/deduplicates CTIs and isolates per-Producer failures.
- `DeviceDescriptor` is routing identity; open it through the exact Producer/catalog entry that discovered it.
- Bounded announced buffers; one waiter per stream; cancellation and active-call drain precede buffer revocation/stream close.
- Copy filled payload once before Producer buffer requeue.
- Unknown PFNC/payload formats remain opaque; offsets and padding are preserved.
- GenApi is optional and layered over GenTL; feature, command and raw byte-register access are separate.

### Common frames and preview
- `Frame::pixels` owns the complete payload.
- Image offset, row padding and trailing padding remain explicit.
- `imageData()` and `partData()` are pointer views, not copies.
- Significant precision differs from storage-container width.
- `FramePtr` is immutable shared ownership.
- `FrameStreamMailbox` is a latest-frame preview handoff, not a lossless WSI queue.
- Host-arrival FPS and vendor/device FPS are distinct measurements.

## 5. Work boundaries

- Do not move microscope optics, objective policy, stage/controller, slide/rack mapping, tile scheduling, WSI persistence, stitching or image processing into Camera.
- Do not make the camera know which slide is active or where a tile belongs.
- Do not force Amcam and GenTL into one lifecycle enum or share provider-specific errors/counters.
- Do not copy GenTL-specific types into Common.
- Do not replace the native C++ CTI loader with an unvalidated Swift runtime/bridge.
- Do not add a second camera session owner in a UI adapter.
- Do not add Qt dependencies to Common or the native AmScope driver.
- Do not infer support from a UI editor or model flag alone.
- Do not call the first-party CTI simulator a third-party Producer or physical camera.

## 6. Remaining gaps and acceptance status

### Phase 4 — Qt reusable UI
Source/offscreen tests and CI exist, but visual/workspace acceptance remains pending. The historical physical MU503 FrameView smoke is limited and does not prove full feature-control UI acceptance. The owner postponed the physical MU503 UI test. Do not mark Phase 4 accepted.

### Phase 6 — capability/feature/command/model coverage
GenTL/GenApi adapter coverage and provider-neutral contract decisions are implemented/tested for the current contract. The specifically authorized Common acquisition-integrity integration into AmScope is implemented and CI-tested.

The Phase 6 audit still identifies missing direct AmScope provider-level tests for feature descriptor/read/write behavior and command/accessory preconditions, rollback, readback and completion. Do not mark all Phase 6 complete. Reopen a precise gap only within explicit owner authorization; do not rewrite Amcam lifecycle to avoid writing focused tests.

### Phase 7 — package and external consumers
Package/install/relocation/external-consumer software CI passed both architectures. Installed-artifact physical AmScope and physical GenTL smoke tests remain. Vendor SDK redistribution terms are unresolved; package vendor-runtime installation defaults OFF.

### Phase 8 — hardware qualification and release
Still open: physical AmScope model/feature/accessory matrix, independent third-party CTI interoperability, physical GenTL camera qualification, long-duration acquisition/recovery, installed-artifact physical smoke, vendor redistribution review and release acceptance.

The owner has no physical camera supporting CTI. Do not ask them to test GenTL using the MU503. The first-party CTI simulator is deterministic software integration evidence only.

### Amcam callback lifetime caveat
The vendor header does not explicitly guarantee that no callback can begin after terminal `Amcam_Close` returns. An active callback counter drains callbacks already entered; zero count alone does not prove that a not-yet-entered callback cannot start. Keep this risk explicit.

## 7. Recommended next action

Do not start another broad feature expansion by assumption. First establish whether the next requested task is:
- a specific remaining AmScope Phase 6 provider test gap;
- a safe/relevant MU503 hardware qualification;
- installed-artifact package smoke or redistribution review; or
- continued Optika microscope application development with Camera held stable.

For any new source task, define the exact contract, provider, ownership, failure semantics and acceptance test before implementation. Run focused tests, then the relevant full CTest/build and both architecture CI workflows. Update the architecture document only when semantics change; update status and handoff for every evidence checkpoint.

## 8. Build and test commands

### Common + GenTL package build

```bash
cmake -S Camera -B build/camera-gentl \
  -DCAMERA_MODULE_BUILD_GENTL=ON \
  -DCAMERA_MODULE_BUILD_AMSCOPE=OFF \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-gentl --parallel
ctest --test-dir build/camera-gentl --output-on-failure
```

AmScope package builds require macOS and the bundled Amcam SDK. Qt targets require Qt 6. GenApi targets require the official GenApi 3.5.1 SDK/runtime. Use the options in the current `Camera/CMakeLists.txt`; do not assume every optional dependency is installed.

### Existing Optika build directory

```bash
cmake --build build/optika-swiftui \
  --target optika_b293_dualslide_app --parallel
```

The Optika app build is not a substitute for Camera package tests.

## 9. Required updates after future work

1. Update the affected module README with current API behavior and limitations.
2. Update [CAMERA_V2_DEVELOPMENT_STATUS.md](CAMERA_V2_DEVELOPMENT_STATUS.md) with exact source SHA, changed files, tests, CI workflow IDs/architectures and evidence class.
3. Update [CAMERA_V2_ARCHITECTURE_AND_OOP.md](CAMERA_V2_ARCHITECTURE_AND_OOP.md) only when architecture, public contract, lifecycle or ownership semantics change.
4. Update this handoff with the tested checkpoint and next bounded task.
5. Keep physical evidence, simulator evidence, CI and owner-reported local results distinct.

## 10. References

- [Complete architecture and OOP reference](CAMERA_V2_ARCHITECTURE_AND_OOP.md)
- [Phase/evidence ledger](CAMERA_V2_DEVELOPMENT_STATUS.md)
- [Phase 6 capability audit](CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md)
- [Common/provider-specific type ownership](CAMERA_COMMON_TYPE_OWNERSHIP.md)
- [Camera package README](../../Camera/README.md)
- [AmScope provider README](../../Camera/Amscope/README.md)
- [GenTL provider README](../../Camera/GenTL/README.md)
- [GenApi/GenTL assessment](AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md)
- [Amcam SDK test-seam and callback lifetime analysis](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md)
- [Original Phase 0–8 plan](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
- [Historical MU503 FrameView smoke](../testing/hardware/phase-4-mu503-frame-view-smoke-2026-11-24.md)
