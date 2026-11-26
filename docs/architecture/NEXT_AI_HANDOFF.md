# Camera v2.0 — Next AI Handoff

> Read this file and `CAMERA_V2_DEVELOPMENT_STATUS.md` before changing source.
> The status ledger is the phase-history authority; this file is the operational continuation guide.

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Branch:** `camera-v.2.0`  
**Code baseline reviewed:** `808779f8f67d81ed99e66a2aff0b5b9f1cde1321`  
**Documentation correction:** `3349a176e8ed6feb52d8167ed89b47379448eb02`  
**Latest CI for code baseline:** [workflow #45733875474](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45733875474) — Intel x86_64 and Apple Silicon arm64 jobs passed, including GenTL tests and official GenApi 3.5.1 build/tests.  
**Next implementation milestone:** **Phase 6 — capability, feature, command and model coverage.**

---

## 1. Continuation authority

The project owner explicitly corrected the previous continuation attempt:
- Phase 5 GenTL/GenApi integration is considered complete.
- Phase 6 is the next implementation milestone.
- Continue the existing roadmap; do not invent a replacement roadmap.
- AmScope preview/UI work is not the current task.
- The retired `AMSCOPE_DRIVER` project/repository is not a current dependency or source of truth. Do not inspect, revive, clone, build against, or modify it.

If older documents conflict with this owner direction or the current status ledger, do not silently follow the older instruction. Record the conflict.

## 2. Phase status

| Phase | Status | Instruction |
|---|---|---|
| 0 — Baseline | Historically accepted | Do not reopen without a concrete regression. |
| 1 — Native AmScope driver | Historically accepted; hardening evidence remains | Do not rewrite. Reopen only for a reproduced defect or a specific Phase 6 gap. |
| 2 — Common camera contracts | Implemented substantially; residual hardware evidence remains | Reuse existing contracts. Change only for a demonstrated semantic defect. |
| 3 — Qt streaming separation | Excluded from active scope by owner decision | No Qt work. |
| 4 — Reusable Qt UI | Excluded from active scope by owner decision | No UI or preview work. |
| 5 — GenTL/GenApi integration | **Complete by owner direction** | Do not restart, duplicate, or replace the integration. External device qualification is separate Phase 8 evidence. |
| 6 — Capability, feature, command and model coverage | **NEXT IMPLEMENTATION MILESTONE** | Start with a read-only coverage audit; implement only evidenced gaps. |
| 7 — Packaging and external consumers | Open | Do not jump ahead of Phase 6. |
| 8 — Hardware qualification and release | Open | Preserve as a separate evidence/release milestone. |

The task is not “add more GenTL.” Determine whether the public API accurately describes what each provider and supported camera model can actually do.

## 3. Repository boundaries

- `Camera/Common`: backend-neutral identity, capabilities, frame/layout types, feature/command contracts, validation and common frame-delivery primitives.
- `Camera/Amscope`: native AmScope C++ provider. `AmcamCamera` owns the AmScope SDK session and acquisition lifecycle.
- `Camera/GenTL`: GenTL 1.6 Consumer-side implementation, Producer/device/data-stream lifecycle, frame projection, remote-port/XML access and optional GenApi 3.5.1 adapters.
- Root CMake and CI: configure/build/test/install boundaries and cross-architecture validation.
- `docs/architecture/CAMERA_V2_DEVELOPMENT_STATUS.md`: current phase ledger and evidence history.
- `docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md`: historical v0.7 inventory, not current phase authority. Do not trust its old UI/test counts without re-verification.
- `Camera/GenTL/README.md`: API usage and implementation limitations; compare with current source before relying on individual details.

The separate Optika application owns presentation, UI, workspace, microscope stage/controller integration, slide mapping/scanning and image-processing workflows. These are not Camera Module v2.0 Phase 6 responsibilities.

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
12. Do not add Qt/UI dependencies to the active native library.

## 5. Phase 6 work package

### 6.1 Create a source-grounded coverage matrix

Inspect current public APIs and implementations for AmScope and GenTL. For every exposed item record:
- provider and API item;
- feature, command, telemetry or discovery category;
- applicable model/provider restrictions;
- value type, unit, range, increment and enum-symbol rules;
- availability/readability/writability semantics;
- preconditions and lifecycle state;
- synchronous/asynchronous behavior;
- readback/completion semantics and error mapping;
- implementation location and automated tests;
- hardware-evidence status.

Do not guess missing cells. Use explicit `unknown`, `not implemented`, `not applicable` or `hardware unverified`.

### 6.2 Audit capability resolution

Trace provider discovery through model policy to the public API. Check that provider support and per-model restrictions combine consistently; model profiles only narrow support; runtime availability is not confused with static support; unknown SDK flags are not treated as positive; and duplicate layers do not expose conflicting meanings.

### 6.3 Audit feature semantics

Check descriptor kind/value alternative consistency; units; exact numeric bounds/increments; non-finite floating-point handling; current enum symbols; availability/access modes; validation before provider calls; write/readback behavior; and correct preservation of provider-specific errors.

### 6.4 Audit command semantics

Check that commands use the command contract, not writable feature values; issue and completion polling are separate; availability/preconditions are enforced; timeout/cancellation/failure/completion remain distinct; and command issue is not misreported as physical effect.

### 6.5 Audit model coverage

Reconcile model profiles and provider discovery with actual model identifiers and SDK/GenApi evidence. Do not broaden support from family resemblance or marketing descriptions. Unknown models remain conservative.

### 6.6 Add only targeted tests

After the audit finds a concrete gap, add the smallest deterministic test at the correct layer. Test semantic boundaries and failure behavior. Mocks/XML fixtures prove deterministic software behavior, not physical-device behavior. Avoid broad refactors and unrelated cleanup.

### 6.7 Keep the evidence ledger current

For each completed slice record exact commit, source files, public behavior, tests, exact CI run/architectures, hardware evidence separately, limitations and follow-up. Never label a feature hardware-verified from mock CI.

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

Phase 8 remains responsible for exact-commit hardware records, supported provider/model matrix, installed-consumer deployment and release acceptance. Keep Phase 8 open until its criteria are met; do not use it to reopen Phase 5 implementation.

## 8. Safe working protocol

1. Confirm current branch and HEAD before every session; the branch may have advanced.
2. Read this handoff, the status ledger, relevant source and tests before proposing a change.
3. Start Phase 6 read-only. Produce a gap list tied to source/test evidence.
4. Do not modify unrelated projects, branches, UI, hardware-control code or historical snapshots.
5. Do not run local builds/tests/debugging in this environment. Use existing CI evidence; do not claim tests ran if they did not. Use/trigger CI only when authorized and appropriate.
6. Never force-push, rewrite history, delete branches or replace whole files without inspecting current contents and preserving unrelated work.
7. Make small isolated commits with descriptive messages. Update this handoff/status only when verified state changes.
8. After each change report exact files, behavior, test/CI evidence, remaining risk and next unfinished Phase 6 item.
9. If source contradicts this handoff, stop before destructive changes; inspect history and document the discrepancy.
10. Do not ask the owner to repeat information already recorded here. Continue the next concrete item in the existing plan.

## 9. Immediate next action

Perform a read-only Phase 6 audit of public capability, feature and command surfaces in `Camera/Common`, `Camera/Amscope` and `Camera/GenTL`. Trace every public item to implementation, model policy, tests and evidence. Return a prioritized gap matrix. Do not start by adding features or changing GenTL architecture. Implement the first confirmed gap only after its semantics and downstream impact are established.
