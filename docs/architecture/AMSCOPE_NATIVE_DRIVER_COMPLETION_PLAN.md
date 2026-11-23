# AmScope Native Driver and Camera Module Completion Plan

**Target branch:** `amscope-native-driver`  
**Baseline reviewed:** `a3a03fbd1fe652fd4c1dfa8d2082ef86a2972f13`  
**Baseline date:** 22 November 2026  
**Language/runtime:** C++17, Qt 6  
**Status:** Development plan; phases are not declared complete by this document.

---

## 1. Purpose

This document defines the work required to finish the AmScope native camera driver and deliver the intended reusable Camera Module architecture in `MICROSCOPE_CPP`.

The current continuation state is maintained in [DEVELOPMENT_HANDOFF.md](DEVELOPMENT_HANDOFF.md), and the source-to-test mapping is maintained in [AMSCOPE_IMPLEMENTATION_INVENTORY.md](AMSCOPE_IMPLEMENTATION_INVENTORY.md). Use [the physical-camera test record template](../testing/hardware/PHASE_HARDWARE_TEST_RECORD_TEMPLATE.md) for each phase's real-camera evidence.

The objective is not merely to finish the preview application. The deliverable is a coherent camera subsystem in which:

- AmScope cameras use the native Amcam SDK for discovery, session lifecycle, acquisition, image retrieval, and recovery.
- GenTL cameras use the existing GenTL/GenApi implementation and the Producer associated with each discovered device.
- Both backends expose common camera identity, capability, feature, value, error, and frame semantics where those semantics genuinely match.
- Backend-specific acquisition and lifecycle semantics remain backend-specific. A universal lifecycle interface must not erase meaningful differences.
- Programmatic camera control, Qt streaming delivery, and reusable camera UI are separate library responsibilities.
- All interfaces connected to one physical camera session operate on the same session and native handle. Separating libraries must not create a second camera owner.

This is an implementation plan and acceptance contract. It does not assert that work described in future phases has already been implemented or verified.

## 2. Governing architecture and authorities

The implementation must follow these authorities in order:

1. The authoritative Camera Module and UI Libraries architecture paper defines the intended C++ target architecture.
2. The Swift `AMSCOPE_DRIVER` implementation and engineering documentation define preserved semantics and observable behavior.
3. AmScope vendor headers, documentation, and samples define native SDK behavior and ABI requirements.
4. The current C++ implementation defines the present development state, not the target architecture.
5. C++17 and Qt 6 define implementation mechanisms, ownership, and threading rules.

When the source, vendor SDK, or architectural paper disagree, record the discrepancy and resolve it explicitly. Do not silently choose whichever implementation is easiest.

### 2.1 Invariants that must survive every phase

- Exactly one `HAmcam` handle belongs to one opened AmScope session.
- Feature access and acquisition use that same handle.
- AmScope acquisition remains SDK callback -> bounded event queue -> one application-owned acquisition worker -> `Amcam_PullImageV4`.
- The vendor callback does not pull frames, stop acquisition, close the device, call UI code, or release callback-owned resources.
- All native calls that require serialization use the session's SDK-call synchronization policy.
- Successful Stop drains callbacks and joins the acquisition worker before releasing the resources they may access.
- Recoverable Stop failure preserves the active session, callback context, frame buffers, and event path.
- Terminal Close does not depend on Stop or cache-flush success.
- Every successful Stop creates the required SDK frame-cache flush obligation. The obligation is cleared only after SDK success and is retried before a later Start when necessary.
- Old callbacks and frames cannot become visible after the acquisition/session generation has been superseded.
- The control library and Qt streaming library never open separate handles for the same selected camera.
- Accepted control operations and lifecycle operations preserve the existing serialized ordering semantics.
- Feature settings, commands, and telemetry remain distinct concepts.
- A model/SDK function flag alone does not establish a usable capability. Capability availability requires model evidence, non-mutating runtime evidence, profile policy, and implemented backend support.
- RAW sensor payloads are never represented as RGB merely to simplify preview rendering.
- GenTL and GenApi are integrated from the existing implementation. They are not reimplemented, and AmScope is not represented by a synthetic CTI Producer.

## 3. Mandatory real-camera verification policy

**Every phase in this plan must be exercised against a real physical camera before that phase can be accepted or marked complete.** This is a hard project gate, not a final-release-only activity.

Unit tests, mocks, CI, static analysis, and compile checks are required supporting evidence where appropriate. None of them substitutes for a phase's real-camera test.

### 3.1 Real-camera gate for every phase

For every phase:

1. Build the current phase implementation for the supported development architecture.
2. Run the automated tests relevant to the phase.
3. Connect a physical camera and record its exact model identity and SDK-reported model code.
4. Run the baseline physical-camera regression procedure in Section 3.2.
5. Run the phase-specific physical-camera procedure defined in that phase.
6. Record actual observations, failures, unsupported capabilities, and the exact tested revision.
7. Fix regressions and repeat the physical-camera procedure before accepting the phase.

A phase remains **OPEN** when its implementation is complete but its real-camera test has not passed. It must not be represented as complete because CI is green.

For changes to the CTI backend, the phase must additionally be tested with a real camera discovered through a real CTI Producer. A simulated camera or a synthetic AmScope Producer does not satisfy this requirement. The existing AmScope reference camera must also pass the regression test to show that the second backend has not broken the first.

If suitable physical hardware is temporarily unavailable, development may continue, but the affected phase remains **BLOCKED: REAL-CAMERA VERIFICATION PENDING**. Do not waive the requirement.

### 3.2 Baseline physical-camera regression

Run this procedure against the designated AmScope reference camera in every phase:

1. Enumerate devices and record the camera identity, model name/code, and model flags.
2. Open the selected device using the enumerated opaque device ID.
3. Read the current state and feature descriptors.
4. Read the supported baseline settings and preserve their original values before changing anything.
5. Start video acquisition and confirm that actual frames arrive and are displayed.
6. Confirm frame dimensions, pixel format, stride, timestamp/sequence progression, and exposure metadata are internally consistent.
7. Change one safe, reversible setting supported by the camera; verify native readback; restore the original value and verify restoration.
8. Stop acquisition, start it again, and confirm that the new acquisition delivers frames.
9. Close the device, reopen it, and confirm that acquisition can start again.
10. Run the still-capture check when the camera supports still capture.
11. Record frame drops, unexpected timeouts, disconnects, stale frames, and resource/lifecycle errors.

At least one repeatable acquisition interval must be recorded. For phases affecting acquisition, lifecycle, frame handling, or thread ownership, use a 15-minute continuous acquisition regression unless the phase defines a stricter test. Record the actual duration; do not report a shorter run as a 15-minute pass.

Capability-specific operations are conditional on the tested camera's verified capabilities. A feature absent from the physical model is recorded as unsupported for that model; it must not be forced or treated as a failure of the model. When a feature is claimed as supported by the implementation, test it on a physical model that actually supports it.

Do not run potentially damaging GPIO/output, trigger, thermal, motion, or accessory operations without checking the connected hardware and electrical/mechanical setup. For hardware outputs, use a safe test load or verified unconnected state. Restore settings after tests where restoration is supported.

### 3.3 Additional tests for difficult failure paths

Some failures (for example, a failed native Stop or a callback race at a precise point) may not be safely or reliably reproducible on demand with physical hardware.

For these cases, use both forms of evidence:

- A real-camera run verifies the relevant normal lifecycle and recovery boundary on the actual SDK/device.
- A deterministic SDK-call seam or controlled fault-injection test verifies the exact failure branch.

The fault-injection test is additional evidence, not a replacement for the real-camera run. Do not deliberately damage hardware or rely on physically disconnecting a camera during an unsafe operation merely to force a test result.

### 3.4 Hardware test record

Each accepted phase must create a test record. Store records under:

`docs/testing/hardware/`

Use this filename form:

`<phase-id>-<camera-model>-<YYYY-MM-DD>.md`

Each record must include:

- Phase ID and name.
- Tested branch and exact commit SHA.
- Camera manufacturer, model name, exact SDK model code, serial identifier where appropriate, firmware, and hardware revision where available.
- Amcam SDK version and selected vendor library.
- macOS version, CPU architecture, compiler, CMake, and Qt versions.
- Connected accessories and test wiring relevant to the test.
- Test procedure and actual duration.
- Per-test result: PASS, FAIL, NOT SUPPORTED BY THIS MODEL, or NOT RUN.
- Logs/diagnostics and relevant frame metadata.
- Observed defects, recovery steps, and unresolved limitations.
- Tester and test date.

A summary statement without the model identity, exact revision, and test observations is not sufficient evidence.

## 4. Baseline development assessment

The baseline is commit `a3a03fbd1fe652fd4c1dfa8d2082ef86a2972f13` on `amscope-native-driver`.

### 4.1 Implemented at the source level

The current branch contains:

- A native AmScope driver using the bundled Amcam SDK.
- Single-session `HAmcam` ownership.
- Native device enumeration, open/close, start/stop, callback-driven event notification, and worker-driven image pulling.
- A bounded event queue and acquisition watchdog behavior for video and triggered acquisition.
- Recoverable Stop handling, terminal Close handling, callback draining, and SDK frame-cache flush tracking.
- Reusable immutable frame leases and a bounded frame pool.
- RGB and RAW frame representation, with sensor FourCC/significant depth kept distinct from the storage container.
- Typed common feature descriptors, values, results, errors, and write validation.
- Runtime/model capability checks and an injectable exact-model allow/deny profile registry.
- Exposure, white balance, resolution, pixel-format, RAW, frame-rate, image-processing, sensor, thermal, ROI, and binning controls as supported by the implementation and camera model.
- Processed and RAW still-capture paths.
- Trigger configuration, heartbeat-based triggered health, and finite software-trigger requests.
- Native hardware-I/O read/configuration APIs and Qt request wrappers.
- Light-source, filter-wheel, ST4, astronomy auto-focuser, and lens focus/aperture operations.
- A Qt request worker, session/feature generations, latest-frame mailbox, and descriptor-driven feature editing.
- A Qt Widgets preview application and macOS build/launch script.

The feature inventory in `Camera/Amscope/README.md` must be kept synchronized with source and tests.

### 4.2 Verified evidence at the baseline

The GitHub Actions run associated with the baseline commit reports successful Intel x86_64 and Apple Silicon arm64 jobs. The recorded run compiled the driver, Qt adapter, and preview executable; ran eight CTest tests successfully; checked the expected artifacts and architectures; and ran the one-command launcher smoke check.

The launcher reported `0 camera(s) found` in the CI environment. This does **not** constitute a real-camera test.

Therefore:

- macOS Intel/Apple Silicon CI build: **PASS at baseline commit**.
- Eight automated CTest tests: **PASS at baseline commit**.
- Preview application build and no-device launcher smoke check: **PASS at baseline commit**.
- Real AmScope camera qualification: **NOT ESTABLISHED BY THAT CI RUN**.
- GenTL/GenApi C++ integration: **NOT PRESENT IN THE BRANCH TREE**.
- Separate reusable control, Qt streaming, and UI libraries: **NOT YET DELIVERED AS THE TARGET LIBRARY BOUNDARIES**.
- Installed consumer package validation: **NOT ESTABLISHED**.

The above status is an evidence snapshot, not a claim that the branch cannot have changed after the reviewed commit.

### Latest branch-head CI evidence (documentation-only revision)

The latest reviewed source/documentation branch head is `48be6c9b635fe9fa40760f66b2efec024bf4fec4`. GitHub Actions workflow [#45059333867](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45059333867) completed successfully on Intel x86_64 and Apple Silicon arm64. All eight registered CTest tests passed on both architectures. The launcher reported zero cameras in the hosted runner. The run verifies the code at that branch head; it does not close the Phase 0 physical-camera gate.


## 5. Phase 0 — Freeze and verify the baseline

**Objective:** establish a real-camera-tested reference before changing architecture or implementation.

### Work

1. Preserve the current green source revision as the comparison baseline.
2. Reconcile the branch's divergence from `main`; review the commits on both sides and integrate deliberately rather than resetting or blindly merging.
3. Verify whether the branch's model-code identity correction and current vendor SDK files are the intended versions.
4. Read the current AmScope source, CMake, tests, README, and Swift authority together.
5. Reconcile the architecture papers and `SWIFT_TO_CPP_TRANSLATION_PARADIGM.md` with the source.
6. Produce the feature/command/telemetry inventory described in Section 6.
7. Establish the designated physical AmScope reference camera and a repeatable baseline test setup.

### Real-camera test required for Phase 0

Run the complete baseline regression in Section 3.2 against the designated AmScope camera. Save its actual model identity and observations. Record unsupported features rather than inferring support from another model.

### Initial physical-camera smoke-test feedback — 22 November 2026

The user reports that the camera successfully opens and produces a live preview. The observed preview FPS is much lower than the approximately 100 FPS previously obtained at 640p with the earlier minimal application. This is a real-device smoke-test observation, not a completed Phase 0 hardware record: the exact camera/model code, current live dimensions, numerical current FPS, acquisition duration, setting readbacks, Stop/Start cycles, and Close/reopen result have not yet been recorded here.

A source comparison of `amscope-native-driver` with `minimal-amscope-gui-mac` found:

- Both use the callback-driven `Amcam_StartPullModeWithCallback` path and a single worker calling `Amcam_PullImageV4`; no fixed acquisition FPS limiter was found in the current loop.
- The old and current preview applications both copy each RGB frame into a detached `QImage`, convert it to a `QPixmap`, and scale it to the preview widget.
- The current displayed `preview FPS` is sampled in `presentLatestFrame()` after those presentation operations. It is therefore a host-side presentation rate, not an independent measurement of sensor or SDK acquisition rate.
- The current `start()` reads the already-configured preview dimensions; it does not select 640p. The toolbar resolution selector is for still capture, not live preview.

Before changing acquisition semantics, establish the actual live width/height and distinguish SDK frame progression from UI presentation throughput. Preserve the callback-to-pull mechanism. Phase 0 remains OPEN until the complete physical regression is performed and recorded.

### Maximum-rate and zero-copy acquisition optimization — 22 November 2026

The user's objective is maximum supported camera throughput, with approximately 100 FPS at a 640×480 preview mode as the measured target. No arbitrary fixed FPS is imposed.

The current branch changes are:

- The preview now retains the immutable `FramePtr` lease and wraps the existing pixel buffer in a non-owning `QImage`. `FrameView` paints directly with `QPainter`; the per-frame `QImage::copy()`, `QPixmap` creation, and stored/scaled pixmap were removed.
- `QtAmcamCamera` uses a latest-frame mailbox based on C++17 atomic `shared_ptr` operations and an atomic notification flag. It replaces stale frames rather than creating a backlog. Frame notifications bypass the feature-request queue mutex. The acquisition-generation invalidation check remains.
- Successful `Amcam_PullImageV4` calls are counted with a relaxed atomic. `AMCAM_EVENT_IMAGE` callbacks have a separate relaxed atomic counter. Neither requires an SDK call or the SDK-call mutex.
- The UI measures **event FPS**, **pull FPS**, and **paint FPS** separately at 1 Hz. It also shows frame dimensions, sequence, exposure time, and pool-exhaustion drops. Frame metadata is stored as scalars; the status label is formatted once per second, not once per frame.
- Full feature descriptor discovery is cached per camera state. A single feature read no longer repeats the entire SDK feature-probe inventory. The cache is invalidated by feature writes and session changes. The UI does not refresh the full feature inventory on entering Streaming, and setting editors remain stopped-state-only.
- Frame notifications no longer take the feature-request queue mutex. Feature/lifecycle operations still use the existing serialized request worker.
- Every Start writes `AMCAM_OPTION_FRAMERATE=0` (no generic FPS cap). After `Amcam_StartPullModeWithCallback` succeeds, the driver selects the vendor-documented maximum for the model: maximum precise frame rate and 100% bandwidth for `AMCAM_FLAG_PRECISE_FRAMERATE` devices, otherwise the maximum model `maxspeed` through `Amcam_put_Speed`. When `AMCAM_FLAG_LOW_NOISE` is present, the slower low-noise mode is disabled and read back. This follows the vendor [frame-rate procedure](https://touptekphotonics.com/FAQ/73), including its requirement that the precise-rate/speed setting be applied after Start. No arbitrary FPS target is applied.

The three measured rates localize the next bottleneck:
- Low event FPS means frames are not arriving from the camera at the desired rate; inspect exposure, live preview dimensions, model speed mode, bit depth, and transport.
- High event FPS but lower pull FPS indicates contention or throughput loss between callback and `Amcam_PullImageV4`.
- High pull FPS but lower paint FPS indicates a presentation/UI bottleneck.
- A large exposure time can itself limit frame rate. The vendor states that the achievable rate is bounded by both sensor output speed and exposure time; exposure in milliseconds implies a theoretical exposure limit of approximately `1000 / exposure_ms` FPS. The vendor also notes that its catalog rates are based on 8-bit operation and higher bit depth may reduce frame rate. Hardware ROI can improve sensor output speed; software cropping does not. See [vendor FAQ Q16](https://touptekphotonics.com/FAQ/78) and [FAQ Q52](https://touptekphotonics.com/FAQ/).

These are performance changes to the application/UI and Amcam Start configuration; they preserve the callback -> bounded event queue -> single `Amcam_PullImageV4` worker. They do not yet establish 100 FPS on the user's hardware.

Implementation commits include:
- `f25a27d7f4cd31fd328ed606a401b92f39cb1a73` — zero-copy FrameView and pulled-frame count plumbing in the preview.
- `5f0d24065de3707b890cd1fd1378c4e8221ed38d` — cache feature descriptor inventory across reads.
- `0e4d8432093a5ce1181b6c971924a842eb3d8af8` — decouple frame notification from feature request queue lock.
- `18ba7a1b6c3b0ed6e354338bd2482a0077ae8e77` — atomic latest-frame mailbox.
- `341579c7409bd36de7b57bcbafbd0f4fcd8a6229` — measure actual frame painting.
- `ea6c5933af7c9d87a1c5da833262a3108c43ae3c` — select model-specific maximum frame rate on Start.
- `80a3cdce61d05611012599d91b02d9eb983f449e` — reduce per-frame UI diagnostic work.
- `ee700a1016e4bf108255daab67cedb93b96bee83` — separate SDK image events, pulls, and paints; display exposure.
- `41d74718fa9a0be37060889b5d2bfcdbd6892e0f` — retain the SDK error code if maximum-rate setup fails.

No assistant-side local build or CTest run was performed for these changes. The user subsequently reported a successful clean local rebuild after deleting the stale `build/amscope` generated tree and rerunning `AMSCOPE_BUILD_TESTING=ON ./scripts/run-amscope.sh`. The application launched, and the live `Resolution` feature in the feature panel successfully changed camera resolution. The earlier `Operation not permitted` failure while replacing `AmcamCamera.cpp.o` did not recur after the clean build.

This targeted physical-camera smoke result establishes that the live resolution setting path works in the user's setup. The exact tested local commit, camera/model identity, selected resolution index, resulting frame dimensions, still-capture dimensions, and event/pull/paint FPS values were not supplied. It does not establish the 640×480 throughput target, the still-resolution toolbar path, other feature setters, or lifecycle/recovery behavior.

### First MU503 hardware rate result — 22 November 2026

The user ran the dedicated utility on the camera enumerated by the SDK as `MU503(USB2.0)`. The selected live-resolution symbol was `2:640x480`; the original `1:1280x960` setting was restored successfully after Stop. The measured interval was 15.090 seconds:

| Metric | Result |
|---|---:|
| SDK image-event rate | 91.253 FPS |
| Successful `Amcam_PullImageV4` rate | 91.253 FPS |
| Driver callback delivery rate | 91.253 FPS |
| Actual frame dimensions | 640×480 |
| Frame-pool drops | 0 |
| Stop / resolution restore / Close | All succeeded |

Equal event, pull, and callback rates with zero pool drops indicate no measured loss between the camera-event, pull-worker, and driver-callback stages during this interval. The observed camera-delivery rate was below the 100 FPS target.

The initial report's `paint_fps=0` and `result=FAIL` were invalid presentation evidence. The MU503 returned SDK frame sequence zero for both the first and last observed frames. The first hardware-test implementation used this SDK sequence field to reject repeated frames and count newly painted frames; the production preview's paint estimator also used sequence changes as its uniqueness condition. The test preview therefore stopped submitting new frames after the first sequence-zero frame, and the paint counter did not measure the actual paint path.

The production preview and test now count a newly submitted/delivered frame when it is actually painted, without depending on SDK sequence metadata. The corrected test uses a monotonically increasing host-side callback revision for each delivered frame. It also reports the SDK timestamp range, reads the configured `ExposureTime` feature separately from per-frame `expotime` metadata, computes the theoretical exposure-limited FPS, and reads back the model-selected frame-speed level outside the timed interval. The first run's 91.253 FPS acquisition observation remains valid; its paint rate must be discarded.

The SDK's per-frame `expotime` metadata was zero in the first run, so the configured exposure duration is not yet known. Do not infer an exposure limit from that field. The device name includes `USB2.0`; this is a relevant transport clue because the published [AmScope MU503 specification](https://amscope.com/products/mu503) lists 101 FPS at 640×480 and USB 3.0. ToupTek's [USB 3.0 recognition guidance](https://www.touptekphotonics.com/FAQ/) notes that a USB 3.0 camera may be recognized as USB 2.0 due to port/cable/connection problems. The configured exposure can independently limit FPS; see [ToupTek FAQ Q16](https://www.touptekphotonics.com/FAQ/78). These are investigation leads, not yet a proven cause of the 91.253 FPS result.

Do not change the native acquisition path or force an FPS value based on this run. First rerun the corrected measurement, record `configured_exposure_us`, `exposure_fps_limit`, `applied_speed_level`, `last_timestamp_us`, and the corrected `paint_fps`, and check the actual macOS USB link speed.

Phase 0 remains OPEN. The second hardware run again measured approximately 91 FPS through the native acquisition path, but the 100 FPS target and the exposure/USB/presentation explanations remain unresolved.


### Second MU503 hardware rate result — 22 November 2026

After the first corrected-test compile issue was fixed, the user locally built and ran the hardware test from source revision `2da3f5d`. The run used `MU503(USB2.0)`, requested and received 640×480, and lasted 14.498 seconds:

| Metric | Result |
|---|---:|
| SDK image-event rate | 91.183 FPS |
| Successful `Amcam_PullImageV4` rate | 91.183 FPS |
| Driver callback delivery rate | 91.183 FPS |
| Preview paint rate | 14.140 FPS (205 frames) |
| Delivered/pulled frames | 1,322 |
| Frame-pool drops | 0 |
| Applied speed level | 6 of 6 |
| Pre-Start `ExposureTime` readback | 70,000 µs |
| Arithmetic rate from that pre-Start value | 14.286 FPS |
| Per-frame SDK exposure metadata | No nonzero samples |
| SDK timestamps | No nonzero samples |
| Stop / resolution restore / Close | All succeeded |

Equal event, pull, and callback rates again show no measured loss through the native acquisition stages. Speed level 6 is the model-reported maximum. The rate remains below the official MU503 USB 3.0 640×480 catalog rate of approximately 101 FPS; this does not by itself establish that the USB connection is actually negotiated at USB 2.0.

The `70,000 µs` exposure value was read before Start, and this test revision did not record automatic-exposure mode or active exposure during the timed interval. The 14.286 FPS arithmetic result is therefore **not a validated exposure limit**. The paint rate of 14.140 FPS happens to be close, but the run lacks preview-poll/submission/paint-event counters needed to localize why painting is slower than acquisition.

The next diagnostic source revision adds:
- `ExposureAuto` and `ExposureTime` readbacks before Start, after Start, and at measurement start/end.
- An exposure FPS ceiling only when automatic exposure is Off at both measurement boundaries and the readback is stable.
- Counts for preview timer polls, frame submissions, Qt paint events, and actually painted frames.

These changes do not modify the native callback -> bounded event queue -> single `Amcam_PullImageV4` worker. They are committed after the source used for this second physical run; a fresh local build and hardware run are required. Also capture macOS `system_profiler SPUSBDataType` output for the MU503 to establish the negotiated USB speed.

The next hardware step is to run the updated diagnostic revision of the dedicated physical-camera rate measurement executable.

From the repository root, run:

```sh
AMSCOPE_BUILD_TESTING=ON ./scripts/run-amscope.sh \
  --hardware-rate-test \
  --resolution 640x480 \
  --warmup 3 \
  --duration 15 \
  --target-fps 100
```

The test opens the selected camera (device index 0 by default), saves the current live-resolution selection, selects 640×480 via `AmcamResolutionIndex`, verifies feature readback, and starts the normal native acquisition path. Its small visible Qt preview measures newly delivered frames when they are actually painted, using a host-side frame revision rather than the device sequence field. The JSON report includes event/pull/callback/paint FPS, actual dimensions, SDK sequence and timestamp values, the configured `ExposureTime` readback and its theoretical FPS ceiling, per-frame exposure metadata samples, applied model speed level, dropped frames, and Stop/restore/Close outcomes. It restores the original live resolution after Stop. The test requires RGB24 live output; it refuses to run while RAW sensor output is enabled. Keep its preview window visible during measurement.

Result classification: `PASS` means operational checks passed and pulled FPS met the requested target; `BELOW_TARGET` means the test ran but pulled FPS was below target; `FAIL` means an operational condition failed. Exit codes are 0, 10, and 8 respectively for those three result classes (setup failures use separate non-zero codes). This executable is a physical-camera test, not a CTest target, and is not run by hosted CI.

After reviewing this short measurement, complete the full Section 3.2 baseline regression, including the 15-minute continuous acquisition run, safe setting readback/restoration, Stop/Start, Close/reopen, and still capture where supported. Save the exact-commit hardware record under `docs/testing/hardware/`.

### Exit criteria

- A selected baseline commit is recorded.
- Branch reconciliation has no unresolved source loss or unexplained conflict.
- The implementation inventory identifies source, test, UI, and hardware status for every exposed capability.
- The baseline physical-camera test record passes.
- All known gaps and deviations are recorded.

**Do not start behavior-changing refactoring until the baseline hardware behavior is recorded.**


### Owner acceptance — 22 November 2026

**Phase 0 is ACCEPTED by the project owner.** The acceptance is based on successful real-camera open/preview, successful 640×480 live-resolution selection and restoration, two MU503 physical rate runs (15.090 s and 14.498 s), equal event/pull/callback rates of 91.253 FPS and 91.183 FPS respectively, zero reported frame-pool drops, and successful Stop/Close. The 100 FPS objective is a performance target, not a baseline-phase acceptance threshold.

The complete 15-minute regression, a saved exact-commit hardware record, all safe-feature restoration cases, and still capture are not evidenced in the recorded run data. They remain useful regression coverage, but are not blockers to the owner's Phase 0 acceptance. The historical OPEN statements above describe the state before this explicit decision.

## 6. Phase 1 — Stabilize and qualify the native AmScope driver

**Objective:** make the existing AmScope session and acquisition implementation dependable under both normal operation and failure.

### Work

1. Review `AmcamCamera.cpp` as a session/lifecycle implementation, not as an arbitrary collection of SDK wrappers.
2. Identify the exact ownership boundaries for the SDK handle, callback context, event queue, acquisition worker, frame pool, still waiter, and pending cache-flush obligation.
3. Add an internal injectable SDK-call seam where needed to deterministically test vendor-call failures. Keep this seam private to implementation/testing; do not expose it as another public camera abstraction.
4. Add deterministic tests for:
   - Open/Close idempotency and invalid transitions.
   - One-handle ownership and no second handle from feature access.
   - Callback lifetime races with Stop, Close, and destruction.
   - Event queue capacity and overflow.
   - `E_PENDING` versus fatal SDK errors.
   - No-frame/no-packet timeout paths.
   - Video watchdog and triggered heartbeat watchdog.
   - Recoverable Stop failure while Streaming.
   - Stop failure while Faulted.
   - Successful Stop callback drain and worker join.
   - Cache flush success, failure, retained obligation, and retry before Start.
   - Terminal Close despite Stop/flush failure.
   - Frame-pool exhaustion and retained immutable frame leases.
   - Still-capture completion, timeout, quarantine, and the recovery boundary.
5. Review lock order and SDK-call serialization for deadlocks, including the rule that the vendor callback cannot block on the SDK-call mutex.
6. Preserve the existing SDK callback -> bounded queue -> single pull worker -> `Amcam_PullImageV4` acquisition mechanism.
7. Fix defects discovered by the tests without changing the acquisition mechanism or silently weakening its lifecycle rules.

### Real-camera test required for Phase 1

Run the Section 3.2 regression and an extended acquisition/lifecycle test on the physical AmScope camera:

- Complete the 15-minute video acquisition interval.
- Repeat Stop/Start and Close/Open cycles.
- Verify successful recovery after an ordinary Stop/Start cycle.
- Verify that frame delivery stops after successful Stop and resumes only for the new acquisition.
- Verify that no stale frames appear after Close/reopen.
- Test still capture on a model that supports it.
- Test RAW mode only on a model whose runtime capability and profile checks report support.

Failure branches that cannot safely be induced on hardware must additionally pass the deterministic fault-injection tests. Record both kinds of evidence.

### Exit criteria

- The existing automated test suite passes.
- New deterministic lifecycle tests pass.
- The physical-camera test record passes.
- No unresolved data race, callback lifetime, deadlock, stale-frame, or resource-release defect remains in the tested paths.
- Acquisition mechanism and the AmScope-native lifecycle exception are preserved.


### Owner acceptance and post-acceptance hardening — 22 November 2026

**Phase 1 is ACCEPTED by the project owner.** The existing native session and acquisition implementation is accepted as the working AmScope backend. The two physical MU503 runs demonstrate stable 640×480 delivery at approximately 91 FPS, zero reported pool drops, and successful Stop, resolution restoration, and Close. The callback → bounded event queue → single acquisition worker → `Amcam_PullImageV4` mechanism remains unchanged.

Additional deterministic tests are being added as post-acceptance hardening, not as a reopening of Phase 1. The first set exercises the production callback-drain tracker, cache-flush obligation/retry semantics (including preservation of a newer obligation), and retained frame-pool leases. These tests do not yet constitute a full injected native-SDK test seam: forced native Open/Start/Stop/Pull/Close errors, still-capture timeout/quarantine, and watchdog-to-session fault-injection remain separate future test work. The full 15-minute and repeated lifecycle hardware regression was not included in the supplied run output and remains recommended regression coverage.

## 7. Phase 2 — Finalize backend-neutral camera models

**Objective:** move genuinely shared camera semantics out of the AmScope namespace and establish the stable common contract used by all backends.

### Delivery slices and current implementation

Phase 2 is delivered incrementally so that common semantics are extracted without forcing vendor-specific settings, codes, or lifecycle rules into a false universal model.

**2.1 — Common discovery and capability models**

The first implementation slice adds `Camera/Common/include/camera/CameraModels.h`:

- `ImageResolution` represents dimensions independently of a backend's resolution-table index.
- `CameraIdentity` carries optional manufacturer, model, serial, firmware, and device-version facts. An opaque enumeration ID is not assumed to be a serial number.
- `CameraDeviceInfo` holds the opaque device ID, display label, common identity, model capabilities, and common preview resolutions.
- `CameraCapabilities` reports `Unknown`, `Unsupported`, or `Supported`. This avoids treating missing discovery metadata as proof that hardware lacks a capability.
- The AmScope `CameraInfo` derives from `camera::CameraDeviceInfo`; its raw SDK model flags, model code, speed/fan levels, and I/O line count remain AmScope-specific.
- AmScope model flags map to common capability names only where the semantic relationship is direct. The common capability snapshot describes model-advertised hardware support; it does not replace runtime feature probes, model profiles, or per-session feature availability.

The discovery model tests verify optional identity, opaque device IDs, dimensions, and the three-state capability semantics. Phase 2.1 was validated locally by the project owner with a clean build and 10/10 CTest tests at source revision `03f35bf`. GitHub Actions run [#45067873506](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45067873506) also passed on Intel x86_64 and Apple Silicon arm64. This is compile/automated evidence, not physical-camera evidence.

**2.2 — Common frame and image-layout model (implemented; validation pending)**

`Camera/Common/include/camera/CameraFrames.h` now defines the backend-neutral `camera::Frame` and immutable `camera::FramePtr` contract:

- Dimensions, row stride, payload bytes, still/live marker, and data/byte-size access are common.
- `PixelFormat` identifies the known buffer layout (`RGB24`, `Raw8`, `Raw16`); `PixelFormatIdentifier` independently carries an opaque value and its scheme. Amcam RAW uses scheme `amcam.fourcc`; no FourCC is translated into PFNC. RGB24 does not invent a native code.
- `significantBitsPerPixel` and `storageBitsPerPixel` remain separate. The first describes source-format depth; the second describes the payload's per-pixel storage width.
- Sequence, timestamp, and exposure are optional. Timestamp carries ticks, ticks-per-second, and an optional clock-domain label. Exposure carries duration ticks and its timebase. A reported zero remains valid metadata.
- The Amcam adapter consults the SDK's `AMCAM_FRAMEINFO_FLAG_*` validity bits before populating each optional value. Amcam timestamps and exposure durations use a 1,000,000-tick-per-second timebase (microseconds); the clock origin is left unknown. SDK flags, exposure gain percent, and black level remain in `amscope::AmcamFrame`, a typed extension of the common frame.
- The native frame pool still owns mutable `AmcamFrame` slots and emits the same immutable shared ownership lease through `camera::FramePtr`. Still capture uses the same common frame contract. The callback -> bounded queue -> single pull worker and four-slot lease/drop behavior are unchanged.

A dedicated common frame test covers optional-vs-zero metadata, timebase representation, opaque scheme/value preservation, buffer layout, and the typed backend extension. The AmScope type test verifies that the public callback lease is the common frame pointer and that native metadata remains available via the extension. The hardware-rate utility now consumes optional common metadata without treating zero as unavailable.

**2.3 — Common errors and remaining shared value types**

Classify the remaining error, measured frame-rate, and image value models. Move only semantics that have matching meaning and units across backends. Preserve native error codes and operation context as backend details. Add round-trip tests only for types that actually cross a persistence or external API boundary; do not invent serialization solely to satisfy a generic checklist.

**Implementation started — common operation errors.** `CameraOperationError.h` now defines backend-neutral operation error categories, operation context, and optional native error identity (`domain` plus signed code). `CameraOperationResult<T>` carries either a successful value or that error. Feature access retains its separate `CameraFeatureAccessError`, because it has feature-specific identity and expected/actual value-kind semantics. The new contract has focused tests for success, typed failure, optional native details, and preservation of native error identity. The AmScope `CameraError` and its public call sites have not yet been migrated; conversion must be explicit and must not discard SDK error values.

### Work

1. Classify every current public type by semantic ownership.
2. Move genuinely common types into `Camera/Common`, including:
   - Camera identity and discovery information.
   - Common capability representation.
   - Resolution and common image-layout concepts.
   - Common frame representation and metadata.
   - Common feature kinds, values, descriptors, errors, and results.
   - Common feature validation and frame-rate estimation.
3. Keep AmScope SDK values and semantics backend-specific:
   - Native SDK codes and FourCC values.
   - AmScope trigger and I/O enumerations.
   - AmScope accessory structures.
   - AmScope-specific model flags and SDK probes.
   - AmScope-specific exposure/gain and format semantics when they do not match common standards.
4. Define explicit conversion boundaries between native SDK data and common frame/feature representations.
5. Preserve enum identity and stable values. Do not map unknown SDK codes to an apparently valid common enum.
6. Preserve feature units and semantic distinctions. In particular, do not equate AmScope gain percent with GenICam gain dB, configured FPS with measured FPS, or Amcam pixel codes with PFNC codes without verified mappings.
7. Ensure the common library has no Qt, Amcam SDK, GenTL implementation, or vendor-header dependency.
8. Add common model tests and serialization/round-trip tests where values cross a persisted or external API boundary.

### Real-camera test required for Phase 2

After extraction, rerun the Section 3.2 baseline on the physical AmScope camera. Compare discovery identity, feature descriptors, read/write results, frame layout, metadata, and lifecycle behavior against the Phase 0 baseline.

At least one RAW-capable physical camera must be used to verify that the extracted common frame model still retains native pixel-format code, significant sensor bit depth, and storage width independently.

### Exit criteria

- Common models compile without vendor or Qt dependencies.
- AmScope adapters preserve all baseline hardware-observed semantics.
- Common validation and model tests pass.
- Physical-camera regression passes with no unexplained behavior change.
- The common contract is sufficient for an independent GenTL adapter without introducing AmScope-specific types.

## 8. Phase 3 — Separate driver, programmatic control, and Qt streaming libraries

**Objective:** establish separate library responsibilities without creating separate camera sessions or competing native SDK owners.

### Target responsibilities

| Library | Responsibility | Ownership boundary |
|---|---|---|
| `camera_amscope_driver` | Native Amcam SDK integration, AmScope discovery/lifecycle/acquisition/recovery, native frame production | The only owner of the AmScope `HAmcam` session |
| `camera_amscope_control` | Programmatic feature reads/writes, validated setting operations, camera commands, and telemetry access | References the existing session/control endpoint; never opens another handle |
| `camera_amscope_streaming_qt` | Qt-facing frame delivery, latest-frame mailbox, QObject-thread handoff, stream notifications | Observes frames from the same driver session; does not own a second acquisition |
| `camera_ui_qt` | Reusable UI components | Consumes common models and control/streaming interfaces; contains no SDK calls |

The first three rows may be implemented as separate CMake targets or libraries according to the final dependency graph, but their ownership semantics are mandatory.

### Work

1. Identify the session owner and the internal serialized operation boundary.
2. Keep one authoritative SDK-call serialization policy. Do not add independent locks/queues that permit acquisition and feature access to race.
3. Extract the non-Qt programmatic control surface from `QtAmcamCamera`.
4. Extract the Qt frame-delivery behavior: frame mailbox, QObject affinity delivery, frame notification coalescing, and delivery shutdown.
5. Preserve the single native handle and ensure control and streaming adapters refer to the same opened session.
6. Preserve FIFO ordering between accepted feature writes/commands and lifecycle operations.
7. Preserve session-generation invalidation, feature-state-generation invalidation, stale callback rejection, and callback dispatch synchronization during executor shutdown.
8. Keep synchronous direct access and Qt queued access from interleaving on the same facade/session when the current execution-mode rule requires a single mode.
9. Keep still capture, trigger configuration, accessory commands, and telemetry in their correct command/control categories; do not put them into the frame delivery interface.
10. Update CMake so the targets can be consumed independently without changing the AmScope native SDK dependency boundary.

### Real-camera test required for Phase 3

Run the Section 3.2 baseline with the new library boundaries in place. In addition:

- Use the non-Qt control library to read and modify a supported setting on the physical camera and verify readback.
- Use the Qt streaming library to receive frames from that same opened camera session.
- Perform a feature write followed by Stop and Start; verify FIFO ordering and absence of stale session results.
- Retain frames while acquisition continues and verify the documented bounded-pool/drop behavior.
- Verify that destroying the Qt streaming adapter does not close or reopen the native camera session unexpectedly.
- Verify that disposing the control facade does not create or destroy an additional SDK handle.

### Exit criteria

- The control API is usable without Qt.
- The streaming adapter is Qt-specific and does not expose a second control API.
- Both adapters use one native session/handle and one serialized SDK operation boundary.
- Existing lifecycle and generation semantics are preserved by tests.
- The real AmScope camera passes the regression and phase-specific tests.

## 9. Phase 4 — Extract the reusable Qt camera UI library

**Objective:** transform the monolithic preview window into a composition of reusable camera UI components.

### Work

1. Extract generic widgets for:
   - Camera discovery and selection.
   - Open/close/start/stop controls.
   - Descriptor-driven feature editors.
   - Command/action controls.
   - Telemetry displays.
   - Live frame display.
   - Still-capture interaction.
   - Capability/availability presentation.
2. Keep feature editors driven by the common `CameraFeatureDescriptor`; do not create a second UI-specific feature schema.
3. Keep commands separate from settings and telemetry. A one-shot auto-white-balance operation, trigger burst, filter-wheel movement, or focuser movement must not be represented as a normal setting write.
4. Keep UI widgets independent of Amcam headers, `HAmcam`, GenTL internals, and backend-specific C++ classes.
5. Define extension points for backend-specific panels without embedding vendor operations in common widgets.
6. Make the preview application a composition root that wires camera selection, control, streaming, and reusable UI together.
7. Preserve value loading rules: a writable editor remains disabled until a current readable value has been acquired; a default widget value is never treated as a camera read.
8. Preserve refresh/invalidation behavior after settings, commands, and lifecycle transitions.
9. Complete presentation for already-implemented controls that currently have no corresponding UI panel, including hardware I/O where the connected device and wiring permit safe operation.
10. Keep RAW rendering honest: until a RAW/Bayer decoder is attached, show the RAW format/state rather than interpreting sensor bytes as RGB.

### Real-camera test required for Phase 4

Run the Section 3.2 regression through the refactored UI. In addition:

- Discover, select, open, start, stop, close, and reopen the physical camera through the reusable widgets.
- Read and change at least one supported feature through the generic editor, verify readback, and restore it.
- Exercise a real supported command, such as auto white balance once or still capture, when the model and acquisition state permit it.
- Verify telemetry labels and units against native reads.
- Verify that unavailable or unsupported controls are hidden/disabled based on actual camera evidence.
- Verify live preview and UI responsiveness during a 15-minute acquisition interval.

### Exit criteria

- The UI library is reusable without AmScope-specific dependencies.
- The preview is composition rather than the owner of camera logic.
- Descriptor, command, and telemetry semantics remain correct.
- The physical camera passes the UI regression.

## 10. Phase 5 — Integrate the existing GenTL/GenApi implementation

**Objective:** add a real CTI backend to the same Camera Module without reimplementing GenTL or GenApi and without changing AmScope's native acquisition mechanism.

### Dependency gate

The reviewed branch does not contain GenTL/GenApi C++ headers, linked libraries, imported CMake targets, Producer `.cti` files, or a CTI camera adapter. This phase cannot be implemented correctly until the existing C++ runtime dependency and its distribution model have been identified.

The missing dependency is an integration blocker, not permission to recreate:

- GenTL ABI/function tables or dispatchers.
- A CTI loader or Producer.
- A GenApi XML parser, node graph, or register engine.
- A synthetic CTI Producer for AmScope.
- Fabricated AmScope GenApi nodes/registers/chunks/capabilities.

### Work

1. Use the official EMVA GenApi 3.5.1 Runtime/SDK package and official GenTL 1.6 header as the primary C++ GenICam dependency candidates. Verify the macOS package binaries, CMake config, dependency closure, license files, and both required architecture slices.
2. Resolve the Consumer implementation choice in AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md: Swift GenTLNative behind a C ABI, an explicitly approved C++ port of its Consumer behavior, or an identified existing external C++ Consumer.
3. Acquire the actual camera-vendor CTI Producer and verify its version, ABI, license, architecture, device support, GenApi XML, and port-access behavior.
4. Add CMake integration using the official GenApi package configuration and the selected existing Consumer interface; do not write a GenApi implementation or a CTI Producer.
5. Implement the CTI backend adapter against the common camera identity, capability, feature, value, error, and frame models.
6. Preserve actual Producer, interface, device, and stream ownership in the existing GenTL implementation.
7. Bind every discovered device candidate to the backend/Producer context that discovered it. Never cross-route an open request to another Producer.
8. Keep GenApi feature access conditional on an actual supported GenApi XML description and usable GenTL port interface. Keep AmScope's native Amcam SDK feature access separate; do not fabricate XML, registers, or a CTI Producer.
9. Preserve opened sessions across discovery refresh.
9. Route common feature access through the common contract while retaining the CTI/GenApi backend's native device and feature semantics.
10. Keep CTI acquisition and lifecycle on the existing GenTL device/acquisition path. Do not force it into AmScope's callback/pull mechanism.
11. Integrate both backend discovery paths through one Camera Module selection/routing surface.

### Real-camera test required for Phase 5

This phase requires physical cameras for **both** backend paths:

- At least one real camera discovered and acquired through a real GenTL Producer/CTI.
- The designated AmScope reference camera using the native Amcam SDK.

Test enumeration, candidate-to-backend identity, open, feature descriptors/reads, supported feature writes with readback, acquisition, Stop/Close, and rediscovery. Verify that a GenTL candidate cannot be opened through another Producer and that discovery refresh does not invalidate an active session.

Then run the AmScope Section 3.2 regression again. The CTI integration is not accepted if it breaks AmScope discovery, its native handle ownership, or its native acquisition lifecycle.

If no real CTI camera or Producer is available, this phase stays blocked; CI-only or simulated CTI tests do not close it.

### Exit criteria

- The existing GenTL/GenApi runtime is integrated without reimplementation.
- A real CTI camera passes the backend-specific hardware test.
- The real AmScope camera still passes its hardware regression.
- Both backends use common application-facing models for genuinely common semantics.
- Candidate/backend ownership, discovery refresh, and opened-session identity are preserved.

## 11. Phase 6 — Complete capability, feature, command, and model coverage

**Objective:** reconcile the native SDK surface, common feature model, programmatic command API, model policy, and UI into an evidence-based capability inventory.

### Work

1. Use the phase-0 inventory as the authoritative checklist.
2. For each SDK-supported function, classify it as:
   - Common setting/feature.
   - Backend-specific setting/feature.
   - Command/action.
   - Telemetry/state.
   - Internal acquisition/lifecycle operation.
   - Unsupported, undocumented, or out of scope.
3. Verify every exposed feature's name, kind, unit, numeric range, increment, enumeration symbols, vector cardinality, availability, readability, and writability.
4. Verify coupled setting behavior and native readback.
5. Verify every command's preconditions, accepted result, asynchronous completion semantics, timeout, failure behavior, and cancellation/retry policy where applicable.
6. Verify telemetry is represented as read-only measured state, not as a writable configuration setting.
7. Verify model-profile behavior: exact SDK model-code identity, allow-list policy, deny precedence, duplicate handling, missing-profile behavior, and the rule that profiles narrow but never fabricate capability.
8. Complete the hardware-I/O UI only for physically supported and safely testable lines.
9. Resolve remaining explicitly deferred functions only after checking the SDK documentation and physical hardware:
   - Self-trigger parameterization.
   - Continuous-trigger/cancellation behavior.
   - Any additional trigger output programming.
   - Remaining accessory operations.
   - Lens autofocus mode switching only if required SDK working-distance semantics become documented.
10. Maintain explicit distinctions for configured fixed FPS, device telemetry FPS, and host-observed preview FPS.
11. Maintain explicit distinctions for AmScope pixel-format codes and PFNC, and AmScope gain percent and GenICam gain dB.
12. Do not mark an SDK feature implemented merely because it appears in a header.

### Real-camera test required for Phase 6

For each supported feature family, test a physical camera model that actually advertises the capability. The hardware matrix must cover the supported model set, not just one convenient device.

At minimum:

- Exposure and white-balance controls.
- Resolution/pixel format and RAW configuration where supported.
- Image-processing and sensor-mode features where supported.
- Thermal controls and telemetry where supported.
- ROI and binning/skipping where supported.
- Still capture in processed and RAW modes where supported.
- Trigger modes, external source, heartbeat, and software trigger on supported trigger-capable hardware.
- Hardware I/O with safe physical wiring.
- Light source, filter wheel, ST4, astronomy focuser, and lens controls on systems where those accessories are attached.

For each setting, preserve the initial value, change a safe value, verify native readback or the documented asynchronous completion state, then restore the original value. For actions whose completion is device-asynchronous, verify state transition rather than treating command acceptance as mechanical completion.

### Exit criteria

- Every exposed capability has a complete semantic definition and a model-evidence path.
- Each claimed feature family has physical-camera evidence on suitable hardware.
- Unsupported or unverified behavior is accurately reported.
- The common UI does not fabricate support or expose an unavailable operation.
- The inventory and architecture documents match the implemented code.

## 12. Phase 7 — Release packaging and external-consumer integration

**Objective:** make the libraries consumable outside the repository's own preview application.

### Work

1. Define stable CMake targets and namespaces for each delivered library.
2. Export public CMake targets and generate a package configuration and version file.
3. Install public headers with correct dependency visibility.
4. Ensure the target dependency graph does not leak Amcam SDK or Qt into consumers that do not need them.
5. Preserve the `camera_` prefix on application-owned deliverable artifacts. Do not rename the vendor `libamcam` library or CTI Producer files.
6. Validate runtime library loading and install-name/RPATH behavior.
7. Document required vendor SDK components, supported macOS architectures, Qt components, and configuration options.
8. Define vendor SDK redistribution requirements before publishing or redistributing the bundled 223 MB SDK/sample tree. Preserve vendor notices and original SDK identity.
9. Create a clean external consumer project that uses `find_package` and links only the public targets it needs.
10. Verify both Qt and non-Qt consumer configurations.
11. Keep build-tree use and installed-package use separate in CMake tests.

The initial supported platform target should remain macOS Intel and Apple Silicon unless Windows/Linux are explicitly added to the release scope. The presence of vendor binaries for another platform is not proof that this project supports it.

### Real-camera test required for Phase 7

Install the package into a clean prefix, build the external consumer without repository-relative include/library paths, then connect the physical AmScope camera.

The installed non-Qt consumer must enumerate/open/read a setting/start/receive frames/stop/close as appropriate to its linked interface. The installed Qt consumer must show the physical camera's frames and descriptor-driven controls. Repeat the baseline regression against the installed artifacts, not only the build-tree binaries.

For the GenTL package, repeat the installed-consumer test with a physical CTI camera as well.

### Exit criteria

- Exported packages work from a clean consumer project.
- Runtime dependencies resolve after installation.
- Real hardware tests pass using installed artifacts.
- Vendor redistribution policy is documented.
- Supported platforms and architectures are stated accurately.

## 13. Phase 8 — Final hardware qualification and release

**Objective:** establish repeatable evidence that the released libraries work with the supported camera models and deployment configurations.

### Work

1. Freeze release candidates and record exact source revisions.
2. Run the complete automated suite on Intel and Apple Silicon.
3. Run the hardware matrix for all declared supported AmScope models and all declared CTI/Producer combinations.
4. Run normal lifecycle, long acquisition, feature read/write, frame format, still capture, triggers, hardware I/O, and accessory tests as applicable to each model.
5. Run disconnect/reconnect and recovery procedures only in a safe, controlled environment.
6. Verify no camera session leak, stale callback, stale frame, duplicate handle, or unbounded frame queue.
7. Verify supported RAW frames remain correctly identified and are never misrepresented as RGB.
8. Verify the preview, installed consumers, and API-level clients use the same common feature and camera model semantics.
9. Publish a compatibility matrix and the hardware test records.
10. Resolve all release-blocking defects before tagging the release.

### Real-camera test required for Phase 8

Every supported model/backend combination must pass its declared acceptance matrix. No single camera can certify a model-specific feature for another model.

At least one complete end-to-end test must run from the installed release package on each supported backend, and the baseline regression must pass on every declared reference AmScope model.

### Exit criteria

- All phase hardware records are present and pass.
- Every supported model/backend combination has a documented compatibility result.
- Automated CI passes on supported build architectures.
- No release-blocking lifecycle, acquisition, feature, packaging, or UI defect remains.
- Public documentation describes only behavior that has been implemented and evidenced.

## 14. Phase 9 — Deferred low-level SDK and transport investigation

**Objective:** isolate hardware/vendor-specific diagnostics that are not prerequisites for the Camera Module architecture or its common contracts.

This phase is deliberately scheduled after Phase 8. It must not interrupt Phases 2–8 or pull low-level SDK probing into the backend-neutral model work.

### Deferred scope

1. Detailed vendor SDK call/ABI investigation beyond what is required to maintain the accepted native driver.
2. USB link negotiation, host-controller, cable/port, and transport-level throughput diagnosis.
3. Vendor-specific RAW acquisition probes, payload freshness experiments, and SDK buffer-overwrite diagnostics.
4. Model-specific acquisition tuning and hardware-in-the-loop experiments that do not define common camera semantics.
5. Additional vendor SDK fault injection and recovery qualification not required by the accepted Phase 1 contract.

### Boundary

- No vendor SDK type, constant, header, or transport-specific assumption may enter `Camera.Common`.
- Findings may inform backend adapters and hardware records, but must not silently redefine common contracts.
- Work in this phase is not a prerequisite for Phase 2 model extraction, Phase 3 library separation, or the generic UI/library architecture.
- Existing accepted driver behavior remains maintained; this phase defers exploratory low-level investigation, not necessary bug fixes.

### Exit criteria

- Every retained hardware-specific claim is tied to an exact device/model, SDK revision, host/transport configuration, and reproducible record.
- No low-level diagnostic abstraction leaks into the backend-neutral API.
- Any resulting backend change is separately reviewed and regression-tested against the common contract.

## 14. Dependency order and parallel work

The intended sequence is:

| Phase | Main deliverable | Depends on |
|---|---|---|
| 0 | Verified baseline, branch reconciliation, implementation inventory | Physical AmScope camera |
| 1 | Qualified native AmScope session and acquisition implementation | Phase 0 |
| 2 | Backend-neutral common models and contracts | Phase 1 |
| 3 | Separate native driver, control, and Qt streaming libraries | Phases 1–2 |
| 4 | Reusable Qt camera UI library and composed preview | Phases 2–3 |
| 5 | Existing GenTL/GenApi integration and CTI adapter | Phase 2 and availability of the existing runtime; AmScope regression from Phase 1 |
| 6 | Evidence-backed feature, command, telemetry, and model coverage | Phases 2–5 |
| 7 | Exported packages and clean external consumers | Phases 3–6 |
| 8 | Final hardware qualification and release | Phases 0–7 |
| 9 | Deferred low-level SDK/transport investigation | Phase 8; explicitly non-blocking for Phases 2–8 |

The GenTL/GenApi dependency discovery (Phase 5, steps 1–3) can proceed in parallel with Phases 1–3. The official GenApi 3.5.1 runtime and GenTL 1.6 ABI are identified; the remaining C++ Consumer integration choice is recorded in AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md. The adapter and hardware acceptance still depend on the finalized common model, a target vendor CTI Producer, and a real CTI camera.

Do not begin by splitting `QtAmcamCamera` into independent classes that each own a worker or SDK handle. First preserve and test the current session semantics; then extract interfaces around the single session owner.

## 15. Phase status ledger

Update this table as implementation progresses. A phase is not complete until both its implementation exit criteria and its real-camera gate pass.

| Phase | Implementation status | Automated tests | Real-camera test | Acceptance |
|---|---|---|---|---|
| 0 — Baseline | Accepted by project owner on 22 Nov 2026; repository/source review, camera open/preview, live 640×480 mode, resolution restoration, and two MU503 rate runs recorded | Existing CI baseline passed; current follow-up tests pending | Two 15-second-class physical runs: 91.253 and 91.183 event/pull/callback FPS, zero drops, Stop/restore/Close succeeded; full 15-minute regression and exact-commit hardware record remain unrecorded | ACCEPTED BY OWNER; residual regression checks non-blocking |
| 1 — Native driver | Accepted by project owner on 22 Nov 2026; single-session lifecycle and callback/queue/worker/pull architecture retained | Existing suite plus new callback/cache-flush/frame-lease primitive tests; latest revision CI pending | MU503 640×480 runs delivered approximately 91 FPS with zero reported drops; Stop/restore/Close passed; extended 15-minute and failure-injection matrix remains recommended | ACCEPTED BY OWNER; deterministic SDK fault-injection is post-acceptance hardening |
| 2 — Common models | IN PROGRESS. Slices 2.1 and 2.2 implemented; slice 2.3 started with backend-neutral operation errors. AmScope error conversion and remaining shared-value classification are pending | Discovery, frame, feature, frame-rate, and new operation-error contract tests; current branch CI must verify the new test | Full AmScope regression after all common model slices; RAW-capable camera required for frame-layout acceptance | OPEN |
| 3 — Library separation | Target boundaries not yet delivered | Required | Required | OPEN |
| 4 — Reusable UI | Preview exists; reusable UI library not yet delivered | Required | Required | OPEN |
| 5 — GenTL/GenApi | Official GenApi 3.5.1 and GenTL 1.6 package identified; existing Consumer found in Swift, but no direct C++ interface; CTI adapter absent | Swift Consumer/mock Producer CI green; C++ integration tests pending | Real CTI camera and AmScope regression required | BLOCKED — Consumer integration choice and target Producer required |
| 6 — Capability coverage | Broad implementation; inventory reconciliation required | Required | Physical model matrix required | OPEN |
| 7 — Packaging | Initial install rules exist; exported package/consumer qualification required | Required | Installed artifacts on physical camera required | OPEN |
| 8 — Release | Not started | Required | Complete hardware matrix required | OPEN |
| 9 — Low-level SDK/transport investigation | Deferred until after Phase 8; not part of the current execution track | Defined after core/release work | Device-specific diagnostic records required if resumed | DEFERRED |

The ledger is intentionally conservative. A green CI run is not a hardware acceptance record.

## 16. Definition of done

The project is complete only when all of the following hold:

1. The native AmScope driver has tested session, lifecycle, callback, acquisition, frame, failure, and recovery behavior.
2. AmScope feature and acquisition operations use the same single `HAmcam` session.
3. The common model accurately represents shared camera identity, capabilities, feature values, errors, and frames.
4. Programmatic controls are available independently of Qt.
5. Qt streaming delivers frames without creating another camera session or exposing a duplicate control API.
6. The Qt UI library is reusable and backend-neutral at its common component boundary.
7. The existing GenTL/GenApi implementation is integrated without reimplementation.
8. Both physical backend paths are tested on real cameras.
9. Every phase has a passing real-camera test record.
10. The libraries are consumable through exported CMake packages from a clean external project.
11. Supported camera models and capability claims are backed by hardware evidence.
12. Source, automated tests, hardware records, README, translation documentation, and authoritative architecture paper agree.

**No phase may be closed on compilation or mocks alone. Every phase requires a real-camera test against the implementation produced by that phase.**
