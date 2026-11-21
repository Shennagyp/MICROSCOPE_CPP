# Swift-to-C++ Translation Paradigm

**Target:** `MICROSCOPE_CPP` / `amscope-native-driver`  
**Semantic authority:** `AMSCOPE_DRIVER` / `macos-version-0.4`  
**Language baseline:** C++17 and Qt 6

## 1. Governing principle

This is a semantic migration, not a syntax translation.

Preserve the Swift system's meaning, observable behavior, capability truth, ownership, lifecycle guarantees, error distinctions and API contracts. Re-express those requirements using idiomatic C++ and Qt. A C++ implementation is not equivalent merely because it compiles or exposes similarly named methods.

Swift determines what the software means. C++ determines how that meaning is implemented safely and efficiently.

## 2. Authority and boundaries

The migration uses three authorities:

1. Existing Swift source and the AmScope engineering paper define application contracts, feature semantics, validation, capability resolution and behavior.
2. Vendor headers, documentation and samples define native SDK behavior and ABI requirements.
3. C++17/Qt 6 define implementation mechanisms, ownership and threading constraints.

When authorities appear to conflict, record the discrepancy and determine whether it is a source defect, backend limitation or language/runtime difference. Do not silently replace the existing semantics.

### GenTL and GenApi are dependencies

The existing GenTL implementation is used; it is not to be reprogrammed. C++ integrates with the existing GenTL and GenApi implementations and their documented interfaces.

Do not create another:
- GenTL ABI/function table or dispatcher;
- CTI loader or Producer implementation;
- GenApi XML parser, node graph or register engine;
- synthetic CTI Producer for AmScope;
- fabricated AmScope GenApi XML, registers, chunks or capabilities.

Original GenTL Producer `.cti` files and third-party binaries retain their names and identities.

**Current branch dependency status:** the checked MICROSCOPE_CPP tree contains no GenTL/GenApi C++ headers, linked libraries, imported CMake targets, Producer .cti files or CTI adapter. Therefore the GenTL integration phase remains blocked on locating and linking the already-existing C++ runtime. This is an integration dependency, not an invitation to reimplement GenTL or GenApi.

### Shared features do not mean shared lifecycle

All camera backends expose one application-level feature contract: descriptors, reads and writes.

Operational lifecycle remains backend-specific:
- AmScope open/close, acquisition start/stop and image retrieval use the native AmScope SDK.
- CTI cameras use the existing GenTL device and acquisition path.

Do not introduce a universal acquisition abstraction just to make the backends look identical.

## 3. Swift constructs translated into C++

| Swift | C++17 translation | Required preservation |
|---|---|---|
| Value `struct` | Value C++ `struct` / `class` | Preserve copy/value semantics; avoid unnecessary heap allocation. |
| `class` | C++ class | Define ownership, copy/move policy and destruction explicitly. |
| `protocol` | Abstract base class when runtime polymorphism is needed | Runtime-selectable camera backends need a stable interface. |
| `any Protocol` | Interface pointer/reference or deliberate type erasure | Hide concrete SDK types from application code. |
| Raw-value enum | `enum class` | Preserve names and explicit native values; never rely on accidental ordinal values. |
| Associated-value enum | `std::variant` | Alternatives remain semantically distinct even when payload types overlap. |
| `Optional<T>` | `std::optional<T>` | Absence is not represented by undocumented sentinels. |
| Arrays/dictionaries | `std::vector`, `std::map`, `std::unordered_map` | Choose ordering and identity behavior deliberately. |
| Typed `throws` | Typed result/error contract or controlled exception boundary | Do not collapse errors to Boolean; never throw across C ABI. |
| `Codable` | Explicit serialization layer | Preserve stable symbolic values, not compiler enum ordinals. |
| `actor` | Serialized executor/owner or synchronized object | A mutex alone does not reproduce actor ordering or task isolation. |
| `async/await` | Qt queued work, worker, future or coroutine | Preserve interruption, cancellation, ordering and lifetime. |
| `@Sendable` | Explicit C++ thread-safety contract | Document allowed calling threads and shared state. |
| Closure | Template callable or `std::function` | Account for captures, allocation, reentrancy and callback lifetime. |

C++17 already provides `std::variant`, `std::optional`, smart pointers and the required concurrency primitives. Do not raise the language standard just to imitate Swift syntax. C++17 has no standard `std::expected`; use an explicit project result type or `std::variant<Value, Error>`.

`std::string_view` is non-owning. It is suitable for synchronous lookup only; queued work must copy data into owned storage first.

### Synchronous driver boundary and Qt scheduling

The first C++ feature-access interface is synchronous and serialized by the owning backend's SDK-call lock. This is an explicit driver-boundary choice, not a claim that Swift's async call syntax maps directly to blocking C++ calls.

The application must not perform potentially blocking CTI/GenApi operations on the GUI thread. The Qt application layer is responsible for scheduling feature requests on an appropriate worker/executor and delivering results back through queued Qt delivery. It must also preserve selected-device identity and reject stale completions after camera selection or session changes. Asynchronous wrappers do not change the shared feature value/descriptor/error contract.

The current AmScope Qt implementation uses one bounded FIFO camera-operation worker (256 pending requests). It serializes lifecycle transitions (Open/Start/Stop/Close), feature descriptor/read/write operations, commands, and telemetry before they reach the driver's SDK-call lock. GUI lifecycle calls are submitted asynchronously; the preview does not perform potentially blocking native lifecycle methods on the GUI thread. A scheduled session generation is assigned when a lifecycle request is accepted, then becomes the active session generation only when that request reaches the head of the worker. This preserves FIFO: feature writes accepted before Stop/Close execute first, and requests queued after a lifecycle operation bind to the resulting session. A separate feature-state generation invalidates older descriptor/read snapshots after a state-changing write or command. Accepted writes and commands themselves remain in FIFO order and retain their operation results; one mutation cannot silently cancel another mutation that was already accepted. Completions are posted to the QObject affinity thread, and callback posting is synchronized with executor shutdown. The application maintains a second UI-generation token for descriptor/value refreshes. It creates editors from descriptors and keeps readable/writable controls disabled until a current value has been fetched; a failed read does not turn a default widget value into an implied camera value. A successful write causes descriptors and values to be refreshed because writeability can depend on another feature (for example, manual versus automatic exposure).

This AmScope worker is an application-layer implementation, not part of the backend-neutral ICameraFeatureAccess contract. A future CTI Qt adapter must use the same execution policy without introducing a second feature value model. Synchronous calls remain available to non-GUI consumers; callers must not invoke potentially blocking providers on the GUI thread.

The Qt application-facing camera surface must not publish a second set of vendor-specific setting methods. Native SDK setters/readers remain backend implementation details; application settings flow through `ICameraFeatureAccess`. Non-setting operations remain separate: one-shot auto white balance is a command and camera-measured FPS is telemetry, not a setting named as configured FPS.


## 4. Ownership and runtime translation

### Native camera session

One opened AmScope session owns exactly one `HAmcam`. Feature access and acquisition use that same handle. No feature adapter opens a second handle. Qt controls do not own native handles.

Use RAII where destruction is safe and deterministic. If vendor cleanup can fail while callbacks or native resources may still be owned by the SDK, do not free their memory optimistically.

### Frames and buffers

The current immutable frame lease (`std::shared_ptr<const Frame>`) lets consumers retain completed frames while the acquisition worker reuses other pool slots. Preserve buffer layout, pixel format, drop policy and metadata semantics. Do not replace this with per-frame copying or an unbounded queue without a demonstrated requirement.

### Callbacks and Qt

Vendor callbacks must not call UI code or perform operations that can deadlock the SDK. Qt objects are thread-affine. Worker-to-UI delivery must use queued invocation/signals, and queued callbacks must not dereference destroyed objects. Capturing raw `this` is not a lifetime guarantee.

## 5. Application feature contract

The Swift model is the semantic authority for:
- feature kinds: Boolean, integer, integer vector, floating point, string and enumeration;
- typed values, with enumeration symbols distinct from ordinary strings;
- inclusive numeric ranges and optional positive increments;
- descriptors containing name, display name, unit, availability, readability, writability, range, vector length and enumeration symbols;
- typed errors: unsupported, not found, unavailable, not readable, not writable, type mismatch, invalid value and backend failure.

### Names and units

Use SFNC names only when meaning and units genuinely match. Keep vendor-qualified names where they do not.

Examples:
- `ExposureAuto`: `Off`, `Once`, `Continuous`.
- `ExposureTime`: microseconds in the AmScope mapping.
- `AmcamAnalogGainPercent`: percentage, not GenICam `Gain` in dB.
- AmScope configured fixed FPS, discrete speed level and device-measured FPS are separate quantities.
- AmScope pixel-format codes are not PFNC codes unless an explicit mapping is verified.

Never normalize unlike quantities because both happen to be numeric.

### Dynamic descriptors

Descriptors reflect current capability and state. They are not permanent static metadata. Exposure time may be readable while auto exposure is enabled but not writable; resolution and pixel format may be writable only while stopped.

The UI refreshes descriptors after relevant writes and lifecycle transitions. The backend revalidates at execution time; a stale descriptor cannot authorize an operation.

Common application-value checks live in `Camera/Common/include/camera/CameraFeatureValidation.h`. This shared validator checks availability, writability, value kind, scalar range/increment, enum membership and vector cardinality. Backends then perform backend-specific state checks, execute the write on the owned session and verify native readback. Do not duplicate or weaken the shared validation rules in a backend adapter.

### Write protocol

A write resolves the current descriptor, checks availability/writability/type, validates range/increment/vector length/enum symbol, serializes the SDK call on the existing handle, performs the write, reads back and verifies the result, then returns a typed outcome.

Coupled controls preserve unchanged components. For example, changing white-balance temperature must preserve current tint when the vendor API accepts the pair together.

One-shot actions such as auto white balance are commands, not Boolean settings. Do not disguise commands as ordinary values.

## 6. Capability truth

A function existing in a header or SDK does not prove that a camera model supports it.

Usable capability must be backed by appropriate evidence: model flags, non-mutating runtime probes, model-specific allow/deny policy and implemented backend coverage. Profiles may narrow real capabilities, but cannot fabricate them. Unsupported features remain explicitly unavailable/unsupported.

## 7. Verification discipline

Report evidence in separate categories:
- **Source inspected**
- **Compiled**
- **Unit tested**
- **Integration tested**
- **Hardware tested**

A green compile is not proof of control correctness. A mock is not hardware evidence. The original SDK working on hardware does not establish that a new adapter works.

Every increment records its Swift authority, C++ files changed, preserved behavior, unsupported behavior, actual verification and unresolved differences.

## 8. Delivered library naming

Every application-owned delivered library uses the `camera_` prefix in its artifact name. Examples:

- `libcamera_amscope_driver.dylib`
- `libcamera_amscope_qt.a` when the Qt adapter is static
- `libcamera_core.dylib` if a separate shared core is introduced
- `libcamera_gentl_adapter.dylib` if a separate adapter library is introduced

CMake target names and C++ namespaces may remain descriptive; use `OUTPUT_NAME` for the artifact name.

Do not rename original vendor SDK libraries such as `libamcam.dylib`, original Producer `.cti` files, or third-party GenApi/GenTL libraries.

## 9. Incremental implementation policy

1. Common feature types and application feature-access contract.
2. AmScope feature access using the existing native session and handle.
3. Remaining AmScope features, capability evidence and model policies.
4. Adapter to the existing GenTL/GenApi implementation (blocked until the existing C++ dependency is linked into this build).
5. Descriptor-driven Qt controls (implemented for the AmScope preview; the CTI adapter should consume the same common contract).
6. AmScope callback-to-pull acquisition and Stop/Close lifecycle parity (implemented; hardware qualification remains).
7. Cross-backend verification once the existing CTI backend is integrated, followed by hardware acceptance.

The first code increment may implement a subset. It must expose only features actually implemented and probed; the contract itself must be capable of expressing the full Swift feature model.

## 10. Current AmScope feature-access increment

The common C++ feature contract is implemented by the native AmScope session and delegated by the Qt adapter. The current descriptor set contains the Swift AmScope feature names:

- `ExposureAuto`: Off, Once, Continuous.
- `ExposureTime`: integer microseconds; writable only in manual exposure mode.
- `AmcamAnalogGainPercent`: percentage, not dB.
- `AmcamColorMatrixEnabled`: Boolean.
- `AmcamPixelFormat`: enumerated Amcam SDK pixel-format codes.
- `AmcamWhiteBalanceTemperature`: integer Kelvin.
- `AmcamWhiteBalanceTint`: integer tint.
- `AmcamWhiteBalanceRGBGain`: three-element integer vector.
- `AmcamFixedFrameRate`: configured fixed FPS in 0.1 FPS increments.
- `AmcamFrameSpeedLevel`: discrete model speed level, not FPS.
- `AmcamResolutionIndex`: `index:widthxheight` enumeration, writable only while stopped.

Descriptors are built from serialized, non-mutating SDK queries and model metadata. Precise fixed FPS requires the model's precise-frame-rate flag plus valid min/max/current option readbacks. Discrete speed requires a model-reported maximum speed and valid current speed readback. Resolution requires model-provided preview modes and a valid current index. Pixel format requires both a current-format query and a successful supported-format enumeration.

Feature writes validate descriptor state, type, range, increment, enumeration or vector cardinality; they execute under the owning session's SDK-call lock and verify readback. Temperature/tint writes preserve the untouched component. UI-facing Qt feature methods delegate to the same native session and do not create another handle.

The Qt Widgets preview builds the settings panel from these descriptors rather than defining a second AmScope settings API. The preview's displayed FPS is a host-monotonic receive/presentation estimate using the same 30-interval moving-average semantics as the Swift app; it is deliberately labeled preview FPS and is not confused with configured fixed FPS or SDK telemetry. It reads current values asynchronously, uses each descriptor's kind/range/vector length/enumeration symbols, disables controls until readable values are known, and refreshes dynamic descriptor state after writes. This UI is currently an AmScope application; it does not imply that the CTI adapter is already integrated.

The C++ AmScope backend accepts an injectable `AmcamCameraProfileRegistry`. Profiles use exact SDK model codes, whitespace trimming, a feature allow-list and a deny-list; explicit deny entries take precedence, and the last duplicate profile wins. A missing profile imposes no additional restriction. The profile is applied after runtime/model evidence, so it can only narrow availability. The C++ enum currently covers the capability families mapped to the eleven implemented control descriptors; the wider Swift sensor, transport, trigger and accessory taxonomy remains outside this increment.

### Native auto-exposure mode semantics

The Swift enum and Amcam SDK define the same numeric mapping: Off = 0,
Continuous = 1, Once = 2. The C++ feature layer writes these native values
directly and checks the immediate `Amcam_get_AutoExpoEnable` readback.
`Once` is therefore the SDK's one-shot mode, not an alias for Continuous and
not a timer-driven application emulation.

The SDK also defines `AMCAM_EVENT_AUTOEXPO_CONV` and
`AMCAM_EVENT_AUTOEXPO_CONVFAIL`. The current Swift session handler does not
act on either event; the C++ callback filter likewise excludes them. A
successful write of `ExposureAuto=Once` means the SDK accepted and returned
mode 2. It does not mean the application has observed convergence. Adding
convergence reporting later would be a separate parity change requiring an
explicit behavior change in the Swift authority as well.

### Post-Stop cache invariant

The C++ driver now tracks the Swift session's post-Stop cache-clean obligation. Every successful `Amcam_Stop` sets `frameCacheFlushPending`; the implementation calls `Amcam_put_Option(handle, AMCAM_OPTION_FLUSH, 3)` while holding the same SDK-call mutex. The pending flag is cleared only after the SDK reports success. A failed flush is reported and retained, and the next `start()` retries it before entering the Starting state. A successful Stop followed by a failed flush therefore leaves the session stopped and retryable; it does not pretend streaming is active.

The same obligation is recorded when the acquisition worker stops the SDK after a fatal event or when the application-owned worker cannot be created after pull mode has started. A failed user-requested `Amcam_Stop` while Streaming is recoverable: the C++ worker remains alive, the callback queue continues accepting events, frame buffers and callbacks remain owned, and the previous Streaming state is restored. While the Stop call is in progress, dequeued image events are suppressed (and a transient queue overflow is cleared) just as Swift ignores events while its streaming flag is false. If the SDK Stop succeeds, the event queue is stopped, the worker is joined, in-flight callbacks are drained, and only then are the frame pool and handlers released. If Stop fails from an already Faulted session, the state remains Faulted and resources remain retained for a later retry or terminal Close.

A terminal Close first stops the event queue and joins the callback-driven pull worker, then calls `Amcam_Close` directly under the SDK-call mutex. It does not call `Amcam_Stop` or require cache-flush success. After `Amcam_Close` returns, it waits for callbacks that entered before handle closure, clears queued events and the pending flush flag, and releases session resources. This matches the Swift terminal-close rule: Close ends handle ownership even if a prior Stop was unsuccessful.

### Callback-to-pull acquisition model

The previous hybrid implementation (non-null event callback plus `Amcam_WaitImageV4`) has been removed. The vendor header defines `AMCAM_EVENT_IMAGE` as a notification for which the application must call `Amcam_PullImageXXXX`; the bundled samples separately demonstrate the null-callback/`WaitImage` polling mode and callback/`PullImage` mode. This implementation now uses the latter, matching the Swift session:

1. The vendor callback performs no SDK calls, image pulls, UI work, or observer invocation. It copies only relevant event codes into a fixed-capacity, allocation-free FIFO.
2. One application-owned acquisition worker consumes events and calls `Amcam_PullImageV4` under the same SDK-call mutex used by feature access and lifecycle operations.
3. `E_PENDING` is treated as a transient callback/frame-readiness race; it does not fault the stream. Disconnect, generic SDK error, no-frame timeout, no-packet timeout, and queue overflow terminate acquisition with an error.
4. The bounded event queue has capacity 256. Overflow is explicit and faults acquisition instead of silently dropping events or allocating without a bound.
5. Stop/Close stop the queue before joining the worker. Successful Stop ends SDK callback production, after which in-flight callbacks are drained before queue/session resources are released. Close remains terminal and does not depend on Stop/flush success.

The C++ implementation now matches the Swift callback-to-deferred-pull execution model and the vendor's documented API pairing. This is source-level and CI-verified parity, not yet hardware acceptance: real-camera tests must still validate callback/Stop/Close timing, event rates against the queue bound, disconnect/no-frame/no-packet behavior, row layout, and stable frame delivery under retained frame leases.

Auto white balance is a one-shot command requiring acquisition to be active. It remains a separate native operation; it is not incorrectly represented as a Boolean feature because the Swift feature-value contract has no command value kind.
