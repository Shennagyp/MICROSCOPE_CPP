# Native AmScope Driver — Phase 1 SDK Test Seam Design

**Status:** Design prepared; implementation intentionally gated on Phase 0 physical-camera acceptance.  
**Source reviewed:** `4de57806a898799173adb04af1272e00ff00fb24` (native implementation snapshot `a3a03fbd1fe652fd4c1dfa8d2082ef86a2972f13`).  
**Purpose:** Define the smallest internal test boundary that can deterministically exercise the native session lifecycle and callback-driven acquisition failure paths.

---

## 1. Design constraints

The seam exists to make native SDK failure branches deterministic. It is not another camera abstraction and is not part of the public application API.

The implementation must preserve these invariants:

- One opened AmScope session owns exactly one `HAmcam` handle.
- Production calls still go directly to the bundled Amcam SDK through the default dispatch table.
- The vendor callback remains notification-only. It does not call the SDK, pull images, stop/close the camera, invoke application observers, or release callback-owned state.
- Acquisition remains SDK callback → bounded event FIFO → one C++ acquisition worker → `Amcam_PullImageV4`.
- All SDK calls continue to use the existing session serialization boundary.
- Recoverable Stop failure retains the active worker, callback context, event queue, frame pool, and stream state.
- Successful Stop drains callbacks and joins the worker before releasing resources.
- Terminal Close does not depend on Stop or cache-flush success.
- The post-Stop cache-flush obligation is cleared only after SDK success and is retried before a later Start.
- Test injection is per camera instance. No global mutable function overrides, process-wide test mode, environment-variable behavior switch, or public virtual SDK interface is permitted.

## 2. Current direct-call shape

The current AmcamCamera implementation calls the vendor SDK from one translation unit but across several responsibilities:

1. Discovery and session ownership: enumeration, Open, Close.
2. Acquisition lifecycle: StartPullModeWithCallback, Stop, PullImageV4.
3. Stream setup: final dimensions, exposure, RAW status/format/depth, and byte-order configuration.
4. Trigger health: hardware-event and heartbeat option reads/writes, with rollback and restoration.
5. Still capture: still-resolution queries, SnapN/SnapR, still-image pull.
6. Feature access: exposure, gain, pixel format, RAW, white balance, ROI, binning, processing, sensor, thermal, and frame-rate operations.
7. Accessory and I/O commands: trigger, GPIO/I/O, light source, filter wheel, ST4, AAF, and lens operations.

The first Phase 1 test seam should not attempt to virtualize every vendor operation. It should cover the SDK calls needed to execute and fault the session/acquisition path. Expand it only when a deterministic native test genuinely needs another operation family.

## 3. Proposed implementation boundary

Use an implementation-private SDK dispatch table stored by the native session implementation.

### 3.1 Type and ABI preservation

Each dispatch entry should derive its function-pointer type from the vendor declaration (for example, the equivalent of `decltype(&Amcam_Stop)`) rather than manually duplicating the signature. This preserves the SDK's exact argument types, callback type, and calling convention.

The production table is initialized to the original Amcam SDK functions. A test fixture supplies a complete table for the subset it exercises. Missing test entries should fail fixture construction rather than silently fall back to physical SDK calls.

The table is immutable after the camera instance is constructed. The same table instance is used by enumeration/open and the later session operations; tests must not install a process-wide replacement.

### 3.2 Visibility and construction

Keep the dispatch type and injection constructor out of public installed headers.

A private implementation header or test-support header may declare a narrow construction/access path for the CTest target. The public AmcamCamera constructor and public API remain unchanged. Public enumeration uses the production dispatch; the internal open path should use its owning instance's dispatch so a test can exercise fresh-ID validation without a real camera.

Do not expose an SDK interface pointer, function table, test tag, or test-only constructor through AmcamCamera.h or QtAmcamCamera.h.

### 3.3 Initial operation subset

The initial injected subset should be limited to what is required for enumeration, open, ordinary video start/stop, a frame pull, cache flush, and terminal close:

| SDK operation | Why the first seam needs it |
|---|---|
| EnumV2 | Supply a deterministic device descriptor and verify fresh opaque-ID selection |
| Open | Count opened handles and inject open failure |
| Close | Verify exactly one terminal close and cleanup after failed Stop |
| get_FinalSize | Supply the stream dimensions without a device |
| get_ExpoTime | Supply a deterministic exposure used by the video watchdog |
| get_Option / put_Option | Supply RAW/heartbeat option state and inject byte-order/cache-flush/heartbeat option results as required |
| get_RawFormat | Supply valid RAW layout if a RAW-path fixture is added; the first RGB fixture may report RAW disabled |
| StartPullModeWithCallback | Capture the SDK callback/context and control callback timing |
| Stop | Inject success/failure sequences and verify state/resource retention |
| PullImageV4 | Supply deterministic frame metadata/payload and distinguish live from still pulls |

Still-resolution/SnapN/SnapR entry points should be added when the still-capture failure/quarantine test fixture is implemented. Feature, accessory, and GPIO function families are outside the first lifecycle seam unless a lifecycle test proves they are required.

The table is intentionally not a replacement SDK. It covers only the test boundary needed for the first deterministic session tests.

### 3.4 Worker-start failure

Native SDK dispatch alone cannot force std::thread construction failure. Add a separate implementation-private worker-launch hook only for the worker-start failure case. The production hook must create the same std::thread as today. The test hook may return a controlled creation error before a worker is started.

Do not generalize this into a public scheduler/executor abstraction.

## 4. Deterministic test fixture behavior

The fake SDK fixture should:

- Own one stable fake handle value and count Open/Close calls.
- Return a stable camera descriptor with model storage valid for the entire EnumV2 call.
- Record each SDK invocation, arguments, return value, and calling thread.
- Retain the callback and context passed to StartPullModeWithCallback.
- Allow tests to emit image, heartbeat, disconnect, generic error, no-frame timeout, and no-packet timeout notifications at controlled points.
- Return configured frame metadata and payload bytes from PullImageV4.
- Script Stop and option results by call index, so failure/retry sequences are deterministic.
- Use condition variables/barriers rather than sleep-based race assertions.
- Ensure callbacks already entered have exited before the fixture or callback context is destroyed.
- Fail immediately if an unexpected vendor call falls through to a real SDK entry point.

The fake must model only the API contract required by each test. It must not simulate camera capabilities that the corresponding test is not exercising.

## 5. Required native-session tests

| Test | Controlled SDK behavior | Required assertions |
|---|---|---|
| Enumeration and Open | One fake device, successful Open | Opaque ID is used; one handle is opened; model facts are copied while descriptor storage is valid |
| Open failure | Open returns null | State remains Closed; no Close is issued for a non-opened handle |
| Idempotent Close | Successful Open followed by repeated Close | One handle is closed once; final state Closed |
| Start failure before SDK start | Stream setup or byte-order write fails | No worker survives; no frame pool/callback remains published; state returns to Open |
| StartPullMode failure | SDK start returns failure | Callback/event/session resources are quiesced; state returns to Open |
| Worker creation failure | SDK Start succeeds; worker launch fails; Stop succeeds | SDK Stop occurs once; callbacks drain; cache flush is attempted; state is Open unless cleanup itself fails |
| Worker creation + Stop failure | Worker launch fails; Stop fails | Handle and any callback-reachable resources remain owned; state is Faulted; terminal Close can still end ownership |
| Normal frame path | Start succeeds; fake emits image event; pull returns frame | One worker performs PullImage; frame metadata/layout are preserved; callback is not called from vendor callback thread |
| Recoverable Stop failure | Stop returns failure while Streaming | State returns to Streaming; worker, callback context, event path, pool and frame delivery remain active; no flush is attempted |
| Successful Stop | Stop succeeds; callbacks are in flight | Queue stops, worker joins, entered callbacks drain, resources release, state becomes Open |
| Flush failure and retry | Stop succeeds; flush fails; later Start retries and flush succeeds | Pending obligation remains after failure; Start does not start before successful retry; flag clears only on success |
| Terminal Close after Stop failure | Stop failure leaves stream active/Faulted | Close calls the vendor Close path and releases ownership without requiring Stop or flush success |
| Callback drain | Callback is paused after entry; Close returns from SDK call | Close does not release callback context until the entered callback exits |
| Fatal event vs user Stop | Coordinate both transitions at a barrier | Exactly one transition owner wins; no premature worker exit, stale state restoration, double Stop, or resource release |
| Still request timeout | Still waiter times out or receives failure | Subsequent still requests remain quarantined until the successful Stop/Start boundary; live event queue remains separate |

The existing queue, watchdog, profile, frame-gate, common feature, and Qt request unit tests remain useful and should not be duplicated by the native fixture. The seam is for the currently missing native session/SDK interaction coverage.

## 6. Scope boundaries

Do not use the seam to:
- Replace Amcam SDK acquisition with polling or another callback strategy.
- Move image retrieval into the vendor callback.
- Create a second handle for controls or Qt streaming.
- Mock feature support in the production model registry.
- Normalize Amcam feature units into GenICam values.
- Introduce GenTL/GenApi stubs or a synthetic CTI Producer.
- Create a public generic ICameraSdk or universal camera-lifecycle interface.
- Claim physical-camera verification.

## 7. Entry and exit conditions

### Entry condition

Phase 0 must first have a passing hardware record for the baseline source revision. It must include a successful real-camera enumeration/open/live-frame/metadata/safe-setting-readback/restore/Stop-Start/Close-reopen sequence and the required 15-minute acquisition interval.

### Exit condition

The first seam increment is accepted only when:
- The public camera headers and APIs are unchanged.
- The production dispatch calls the same vendor SDK functions as before.
- All new deterministic native-session tests pass on Intel and Apple Silicon CI.
- Existing eight tests continue to pass.
- The physical AmScope baseline and Phase 1 real-camera procedure both pass on the resulting source revision.
- The hardware record contains the exact tested commit and observations.

Until the entry condition is satisfied, this document is preparation only. Do not make behavior-changing source edits under Phase 1.
