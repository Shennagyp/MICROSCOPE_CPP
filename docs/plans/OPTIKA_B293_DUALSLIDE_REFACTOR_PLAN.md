# OPTIKA B293 Dual-Slide Application Refactoring Plan

**Branch:** `OPTIKA-B293-DUALSLIDE-APP`  
**Starting commit:** `f25e5d8e6fdefea7f742fb71fa5f4ead69291626`  
**Plan status:** Implementation in progress; source/build migration and initial UI decomposition committed; compile/runtime acceptance pending  
**Related architecture:** [OPTIKA B293 Dual-Slide Application Architecture](../architecture/OPTIKA_B293_DUALSLIDE_APPLICATION_ARCHITECTURE.md)


## Implementation status — 2026-11-25

This plan has moved from architecture-only planning into implementation on
`OPTIKA-B293-DUALSLIDE-APP`. The original v0.8 source remains the migration
reference on its original branch.

| Phase | Current status | Evidence / remaining boundary |
|---|---|---|
| 0 — Baseline and ownership map | Source inventory recorded | Camera/application split, dual-slide routing, frame ownership and coordinate behavior were inspected before extraction. Continue updating the inventory as responsibilities change. |
| 1 — Product/application build boundary | Source migration committed; build unverified | `Application/OptikaB293` owns the app target, UI, camera adapter and application tests. Root CMake composes the app with `Camera/Amscope`. Every path listed by the app CMake source list exists in the branch tree. |
| 2 — Functional core extraction | In progress | Microscope calibration, automatic-resolution policy, slide-image geometry and slide overlay value models are separate deterministic modules. Automatic-resolution readout semantics remain provisional and are not hardware-verified. |
| 3 — Application composition and state ownership | Source split committed; command/readback audit advanced; build unverified | The composition root constructs and injects the camera bridge, microscope configuration owner, resolution coordinator, workspace and gallery. Camera session lifecycle and generic feature snapshot/editor state have separate owners. The session model alone performs lifecycle and acquisition-affecting resolution mutations; generic editors cannot submit preview resolution. Feature-generation invalidation is reconciled after successful/failed writes and every lifecycle completion. Application termination also closes through the session owner, with a gate spanning previously queued operations. Microscope configuration/persistence and automatic-resolution coordination are separate optics feature owners. |
| 4 — UI and preview refactoring | In progress | Workspace, dock, preview router, pane, image view, image container and feature controls have separate source responsibilities. The preview consumes frames through a narrow `NativeCameraFrameFeed`; the concrete lifecycle bridge is no longer exposed to presentation. Each notification uses one feed call to obtain a `NativeCameraFrameSnapshot` containing counters and the newest lease. RGB/gray color spaces are cached, the mailbox moves its frame pointer into the slot without an extra shared-pointer copy, and frame-specific Core Graphics providers retain the source lease without copying pixel bytes. The Objective-C++ lease stores FramePtr inline, removing the separate per-frame heap holder. The source audit also records the four-slot live pool, discard-on-exhaustion policy and separate still-capture allocation. The single-slot latest-frame mailbox and coalesced notification semantics remain unchanged. Preserve the single frame consumer and verify every construction/update path statically. |
| 5 — Capability expansion seams | Not started | Motion and ImageProcessing remain separate future capabilities. Do not create placeholder implementations or speculative universal interfaces. |
| 6 — Migration acceptance and retirement | Not accepted | No build, test, application, camera or hardware verification has been run here. Do not claim acceptance or retire migration references based on source movement alone. |

### Current implementation constraints

- The camera driver, acquisition worker, bounded queue, frame pool, immutable
  frame leases, mailbox, generation gate and single preview consumer are not
  refactoring targets.
- Camera, Motion and ImageProcessing remain sibling capabilities coordinated
  by the application.
- Geometry and scientific calculations belong in deterministic modules;
  AppKit views own native input, rendering and resource lifecycles.
- The previous dock-width preference is migrated under the new OPTIKA key.
- The floating dock remains an overlay; its panel alpha remains 0.20 (80%
  transparency).
- No build, test, debugging session or hardware run has been performed during
  this refactoring. The user performs those checks in the development
  environment when explicitly requested.


## Objective

Turn the current camera-centric native macOS application into an OPTIKA B293 microscope workstation application. Move application ownership outside `Camera/`, refactor the UI and state model, and preserve the existing camera backend as an independent capability.

The goal is lower conceptual and maintenance complexity, not artificially low line count. Prefer fewer concepts, fewer duplicated rules, explicit state ownership and compact composition over compressed code or oversized generic frameworks.

## Constraints

- Existing `Camera/Amscope` driver behavior and ownership remain stable.
- No motorization or image-processing implementation is invented before its contracts and requirements are concrete.
- The new application is not nested under `Camera/`.
- Camera, Motion and ImageProcessing are sibling capabilities.
- UI does not call hardware APIs directly.
- Pure calculations remain independent of SwiftUI, AppKit and vendor SDKs.
- The existing single-stream, dual-slide behavior remains intact.
- Preview frame leases, acquisition worker, bounded queue, mailbox, generation gate and single consumer are preserved.
- The current v0.8 app remains a migration reference until replacement behavior is accepted.
- No source file is split or moved merely to make folders look clean.
- No builds, tests, debugging sessions or hardware runs are performed as part of static planning unless explicitly requested in a later task.

## Phase 0 — Baseline and ownership map

**Purpose:** establish exactly what exists before moving it.

Deliverables:
- Inventory all native application files currently under `Camera/Amscope/app/native_mac`.
- Classify each file as camera driver, application adapter, domain policy, presentation, persistence or test.
- Record public contracts and cross-file dependencies.
- Map state ownership and mutation paths in `NativeCameraModel`.
- Map the frame lifecycle from SDK callback through presentation and capture.
- Record the current dual-slide semantics, overlay coordinate system, image scaling semantics and calibration assumptions.
- Identify which dock-model types are used by the live UI and which are currently unused.

Exit criteria:
- Every file has a destination or an explicit retain-in-place decision.
- Every shared state value has one named owner.
- The frame pipeline invariants are captured before any UI change.
- Unverified optical assumptions are labeled as such.

## Phase 1 — Product/application build boundary

**Purpose:** make the application structurally independent of the camera directory.

**Implementation status:** source/build ownership migration committed; CMake configuration and compilation remain unverified.

Deliverables:
- Create `Application/OptikaB293` as the application-owned source/build boundary.
- Move application-owned entry point, UI and integration adapter files into the new boundary. The v0.8 source remains available in the pre-refactor Git baseline/history.
- Move `NativeCameraController` and the Objective-C++ bridge to an application camera adapter location; they coordinate the application with Camera but are not the vendor driver.
- Extract application-target configuration from `Camera/Amscope/CMakeLists.txt` into the application build definition.
- Keep the AmScope driver and common camera types buildable without the app.
- Add a top-level build entry point only when needed to compose the product targets.

Exit criteria:
- Camera library targets no longer own or compile the product application.
- Application target links to camera libraries from outside `Camera/`.
- The existing camera library's public contracts are unchanged.
- The current app CMake source list is statically verified against repository paths; the new target still requires user-side configure/build and behavior acceptance.

## Phase 2 — Functional core extraction

**Purpose:** move policy and deterministic calculations out of UI state holders.

**Implementation status:** calibration, automatic-resolution policy, image geometry and overlay value models are extracted. Source tests are registered but have not been executed.

Extract focused pure functions/value models for:
- Optical calibration and conversion to specimen-plane distance.
- Automatic resolution selection, including explicit readout-mode semantics and evidence/provenance; the pure policy is owned by optics and invoked through the application-level microscope resolution coordinator.
- Source-image, viewport and display coordinate transforms.
- Fit, lock and 1:1 image scaling policy.
- Measurement geometry and overlay coordinate calculations.
- Future stage coordinate transforms and motion-limit evaluation when stage requirements are specified.

Exit criteria:
- Pure operations accept explicit inputs and return explicit values/results.
- Invalid or unknown physical inputs produce an unknown/error result, not fabricated precision.
- UI does not duplicate the same calculations.
- Tests cover mathematical invariants and boundary cases as each pure module is introduced.

## Phase 3 — Application composition and state ownership

**Purpose:** remove the monolithic shared application model without introducing a global framework.

**Implementation status:** workspace, microscope configuration and automatic-resolution policy have distinct owners. Camera session lifecycle and generic feature snapshot/mutation state are split between `NativeCameraModel` and `NativeCameraFeatureModel`. The command/readback audit closed the routing and recovery defects found so far: generic editors exclude and reject acquisition-affecting preview resolution; generic writes refresh after either outcome because enqueueing invalidates earlier reads; and lifecycle completions refresh or clear the snapshot according to the resulting camera state because session-generation changes invalidate older reads. Acquisition-fault handling clears the snapshot, and late descriptor/read callbacks are gated by current session access.

Target state owners:
- Camera session state: application camera adapter/session projection.
- Camera feature state: feature snapshot and mutation coordinator.
- Microscope/optical configuration: `MicroscopeConfigurationModel` and its draft/persistence ownership.
- Automatic resolution: `MicroscopeResolutionCoordinator`, joining pure optics policy with camera readback and the session owner's acquisition-aware preview-resolution command.
- Workspace state: active live destination, dock visibility/layout and pane selection.
- Slide document state: per-pane retained image, freeze, view transform and overlays.
- Capture gallery state: capture store and persistence lifecycle.

Deliverables:
- Application composition root constructs the camera adapter, microscope configuration owner, resolution coordinator, workspace and gallery.
- Replace `NativeCameraModel.shared` as a hidden dependency with explicit composition/injection.
- Separate state transitions from effects.
- Make asynchronous completion/error paths update the correct state owner.
- Prevent stale feature reads or stale session callbacks from overwriting newer state.

Exit criteria:
- No single observable object owns camera, microscope, slide, dock and gallery state.
- UI dependencies are constructor/property injected.
- Side effects have explicit owners and cancellation/lifecycle rules.
- There is no duplicate source of truth for active live destination or optical configuration.

## Phase 4 — UI and preview refactoring

**Purpose:** reduce UI complexity without changing user-visible behavior.

Deliverables:
- Decompose the main workspace into small views with explicit inputs/actions.
- Separate dual-preview frame routing from slide-pane presentation.
- Separate image-document navigation/scaling from slide-pane controls.
- Separate overlay state/geometry from overlay drawing.
- Keep AppKit for high-rate preview rendering; use SwiftUI for composition and low-frequency controls.
- Same-size live frames update only the retained image reference and image-surface
  invalidation; do not recompute unchanged geometry or re-publish overlay state.
- Frozen destinations consume/release frame leases without allocating discarded
  `CGImage` objects. Empty overlay sets bypass overlay geometry/drawing setup.
- Pane diagnostics update only when their values change; the active pane combines
  acquisition counters and its displayed-frame increment in one label update.
- Preserve floating overlay dock, permanent rail, resize behavior and the current 80% panel transparency requirement unless the user changes it.
- Keep control visibility and minimum-pane-width behavior explicit.
- Keep keyboard/accessibility and narrow-pane behavior in scope rather than treating it as decoration.

Exit criteria:
- Presentation files have cohesive responsibilities.
- No view performs camera I/O, persistence or scientific policy.
- One live frame consumer remains.
- Every overlay coordinate maps to source-image coordinates and survives view transforms correctly.
- Dual-slide behavior is unchanged.

## Phase 5 — Capability expansion seams

**Purpose:** add future capabilities without placing them in Camera.

### Motion

Define contracts only after stage hardware and operations are specified. Expected concepts include connection, axes, position, velocity, move, stop, home, limits, status and fault. Coordinate transforms and safety limits should be deterministic functions. Hardware-specific command execution belongs to the Motion implementation.

### ImageProcessing

Define an input contract based on vendor-neutral image/frame data and explicit pixel layout, stride, color space/format, coordinate system and calibration metadata. Algorithms should return typed results tied to the source image identity and parameter set. Processing must not control camera acquisition or the UI.

### Workflow orchestration

Application workflows may coordinate camera acquisition, stage positioning and image processing. Such orchestration belongs to the application/use-case layer. Do not create direct Camera-to-Motion or Camera-to-ImageProcessing dependencies.

Exit criteria:
- Each capability can be substituted independently.
- Workflows coordinate them through declared contracts.
- No empty abstraction is added without a concrete implementation/consumer.

## Phase 6 — Migration acceptance and retirement of the transitional UI

**Purpose:** cut over only after required behavior is preserved.

Acceptance checklist:
- Camera discovery/open/close/start/stop and generic feature control.
- Single live stream routed to either slide.
- Per-slide freeze/hold semantics.
- Per-slide fit, manual scale lock, 1:1 and pan.
- Capture/gallery behavior and durable image files.
- Measurements and annotations in source-image coordinates.
- Explicit optical calibration and honest unavailable-state behavior.
- Floating dock/rail interactions and persisted preferences.
- Correct teardown and application termination behavior.
- No regression in frame ownership or backpressure.
- Future capability interfaces do not leak into Camera.

Only after acceptance:
- Remove the transitional native application target from the camera CMake definition.
- Remove obsolete UI files from `Camera/Amscope/app/native_mac`.
- Retain the AmScope driver and camera-level diagnostic tools in Camera.
- Update architecture and build documentation to the final layout.

## Verification checkpoints

**Run the first full configure/build/test checkpoint now, before beginning Phase 5.**
Phases 1–4 contain substantial C++, Objective-C++, Swift, CMake and ownership
changes, but source audits do not establish that the product compiles or runs.

With the Qt adapter and standalone preview app disabled, CTest should register
15 deterministic tests: five Camera/Common tests, six AmScope tests and four
application policy/geometry tests. For a machine using the standalone macOS
Command Line Tools, use CMake's Ninja generator (Swift support is experimental
but available). The Xcode generator is an alternative only when the full Xcode
app is installed and selected; Command Line Tools alone do not include
xcodebuild.

Check the compiler and build tool first:

```bash
cmake --version
swiftc --version
ninja --version
```

Then configure, build and run all deterministic tests:

```bash
cmake -S . -B build/optika-b293-ninja -G Ninja \
  -DOPTIKA_BUILD_NATIVE_APP=ON \
  -DAMSCOPE_BUILD_QT_ADAPTER=OFF \
  -DAMSCOPE_BUILD_PREVIEW_APP=OFF \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/optika-b293-ninja --parallel
ctest --test-dir build/optika-b293-ninja --output-on-failure
```

Use a new build directory when switching generators. CMake caches the selected
generator in each build tree and cannot convert an existing build directory
from Unix Makefiles or Xcode to Ninja.

Only after build and CTest pass should the native app be launched for manual
UI checks. Camera-connected acquisition, resolution changes, freeze behavior,
capture-file inspection and shutdown are a subsequent hardware acceptance
checkpoint; they are not replaced by the deterministic unit tests.

Repeat configure/build/CTest after changes to Camera, the frame feed, image
ownership, capture, CMake composition or other compiled application contracts.
Run Phase 6 end-to-end acceptance only after the earlier checkpoints pass and
the remaining product workflows are implemented. A build or test failure is a
stop condition: fix it before layering more refactoring on top.

## Implementation discipline

For every extraction or move, use this order:

1. Identify current owner and all callers.
2. Define the target contract and invariant.
3. Move/extract without changing behavior.
4. Update composition and build ownership.
5. Review the diff for duplicated logic and accidental coupling.
6. Add/adjust focused tests for pure behavior and state transitions.
7. Verify build/runtime/hardware only in the user's environment when the user explicitly chooses to run those checks.

A refactor is complete when the dependency graph and ownership are simpler, not merely when files are shorter or have new names.
