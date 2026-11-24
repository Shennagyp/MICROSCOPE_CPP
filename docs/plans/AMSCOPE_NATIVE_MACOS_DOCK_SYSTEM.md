# Native macOS Dock System

**Status:** Implementation plan; baseline audit complete  
**Target branch:** `amscope-native-app-mac.v.0.8-docksystem`  
**Base:** `amscope-native-app-mac.v.0.8` at `68546dbb1bd3b4c21c90278aa1fdd5d6ce16db43`

## 1. Objective

Replace the current one-off workspace dock with a reusable native macOS docking system for tool panels, while preserving the microscope preview and camera runtime as independent, stable systems.

The intended interaction model is comparable to an IDE tool-window system:

- Multiple tool panels can share a tab group.
- Dock regions can be split horizontally or vertically.
- Tabs can be reordered and moved between groups.
- A panel or tab group can detach into a floating window and later return.
- Panel placement and window geometry survive application restarts.
- A permanent, narrow vertical tool rail remains overlaid on the main window's right edge. It is a launcher/selector, not a layout region and not a panel title bar.
- Auto-hidden panels may be revealed as temporary overlays without resizing the microscope preview.

Initial registered panels are **Camera Controls** and **Smart Gallery**. The model must accommodate additional tool panels without adding more panel-specific conditionals to the main window.

## 2. Scope and non-goals

### In scope

- Dock layout model, stable panel identities, layout validation and mutation commands.
- AppKit-backed split, tab-group, drag/drop and floating-window behavior.
- SwiftUI panel content hosted inside stable AppKit view controllers.
- Permanent overlaid tool rail and optional auto-hide/flyout behavior.
- Versioned layout persistence and migration from the current dock preferences.
- Lifecycle rules for hiding, reparenting, floating, restoring and closing tool windows.
- Accessibility, minimum sizes, display changes, and recovery from invalid saved layouts.

### Explicitly out of scope

- Changes to the Amcam SDK, C++ acquisition worker, event queue, frame pool, frame mailbox, or pixel format handling.
- Any additional camera frame consumer, per-frame SwiftUI state, polling loop, or frame-copy path.
- Reworking the dual-slide preview renderer.
- Replacing the app's camera model or camera command executor with docking state.
- A node editor or visual graph editor.
- A general-purpose public docking framework distributed independently of this application.
- Changing the macOS deployment target without an explicit compatibility decision.

## 3. Repository baseline

The audit is against the new branch's initial commit, not an assumed local working tree.

### Current implementation

- `MainWindowView.swift` composes the dual preview and a single trailing dock in an `HStack`.
- The main dock is displayed when `isWorkspaceDockVisible && !isWorkspaceDockFloating`.
- A custom AppKit resize-divider view controls a single dock width.
- `NativeWorkspaceDock.swift` switches between Camera Controls and Smart Gallery using `selectedWorkspaceDockTab`.
- `NativeWorkspaceDockTabRail` draws the two compact, vertical, rotated-label tabs as an overlay on the trailing edge of the main window.
- `NativeWorkspaceDockPanelAnchorNSView` owns one floating `NSPanel`, attached as a child of the main window. It uses AppKit frame autosave under `AmscopeWorkspaceDock`.
- `NativeCameraModel` currently owns the selected dock tab, dock visibility, floating flag and dock width. Only the width has an explicit `UserDefaults` key (`AmscopeNative.WorkspaceDockWidth.v1`).
- There is no serialized dock tree, panel registry, split tree, multiple tab groups, drop-target model, auto-hide placement, or unified layout restore.
- The standalone `CameraControlsWindow` remains a separate application window and currently shares the same camera model.

### Camera and preview ownership constraints

The preview must remain mounted and owned by its current AppKit preview surface. It must not be treated as an ordinary detachable tool panel.

The existing camera path has one camera session owner, one acquisition worker, one latest-frame mailbox and one AppKit consumer. Dock operations must not change any of those owners or add rendering consumers.

`NativeCameraModel` and `NativeCaptureGalleryStore` must remain stable application objects. Moving a panel may reparent its view controller; it must not reconstruct the camera runtime, gallery store, camera session, or preview surface.

## 4. Technology decision

### SwiftUI

SwiftUI provides the content and interaction primitives: views, controls, tab presentation and hosting. It does not provide a complete IDE-style docking manager.

`HSplitView`, `VSplitView`, `TabView`, `NavigationSplitView` and `.inspector` are useful presentation components, but none owns the complete semantics of tab-group movement, drag docking, floating-window reparenting, layout persistence and restore.

### AppKit

AppKit is the windowing and containment layer:

- `NSViewController` owns panel view-controller containment.
- `NSHostingController` hosts SwiftUI panel content.
- `NSSplitView` / `NSSplitViewController` provide native split resizing where they fit the layout.
- `NSWindow` / `NSPanel` own detached tool windows.
- AppKit drag-and-drop and window events provide the interaction boundary for tab tear-off, drop targets, movement and resize.

The docking model and controller remain application-owned. AppKit views are renderers of that model, not its source of truth.

### Third-party candidate: DockKit

The public `Polyjuice/swift-dockkit` repository was inspected as a candidate and architectural reference. Its current source advertises tab groups, nested splits, floating windows, layout serialization and incremental reconciliation. Its `DockablePanel` lifecycle distinguishes reparenting from terminal destruction, which is an important ownership distinction.

It is not selected as a dependency at this stage:

1. Its package manifest declares macOS 13 as the minimum platform, while this repository's Amscope CMake configuration declares a macOS 10.15 deployment target. Adopting it directly would silently change a stated compatibility boundary.
2. The current DockKit layout model does not represent the permanent edge tool rail or an auto-hide placement/flyout. Those still require application-specific behavior.
3. Its package product is dynamic, while this native application is currently assembled as a CMake/Ninja target from explicit Swift and Objective-C++ source files. Package resolution, dynamic-library embedding and signing would add integration work.
4. The current application needs a stable, non-detachable preview surface alongside dockable tool panels. That integration requires an explicit adapter and lifecycle proof.

**Decision:** implement a small first-party docking model and AppKit renderer, using DockKit as a reference for layout reconciliation and panel lifecycle ideas. Do not add a DockKit package dependency or raise the deployment target in this branch. Re-evaluate only if the supported macOS baseline is deliberately raised and a focused integration spike demonstrates a net reduction in complexity.

## 5. Conceptual architecture

Separate the system into five owners.

### 5.1 Panel catalog and registry

A registry maps stable IDs to immutable panel descriptors and runtime content factories.

A descriptor contains:

- Stable `DockPanelID` (not a title string or array index).
- Title and SF Symbol name.
- Minimum and preferred dimensions.
- Supported docking/floating capabilities.
- A content factory that creates the panel's hosting controller once.

Initial IDs:

- `cameraControls`
- `smartGallery`

The registry owns one runtime panel instance per ID. A panel can occupy only one placement at a time. Unknown panel IDs found in a saved layout must not crash restoration; they should be reported and safely omitted or retained as unresolved placeholders according to the final migration policy.

### 5.2 Versioned layout model

The serializable snapshot is independent of `NativeCameraModel` and contains:

- Schema version.
- Main-window dock regions (leading, trailing, top and bottom).
- Recursive region nodes: tab groups and horizontal/vertical splits.
- Auto-hidden tab groups associated with an edge.
- Floating-window groups, each with a stable window ID, root node, frame and optional screen identifier.

Selected tabs and split proportions are layout state. The currently expanded auto-hide flyout, hover state, drag state and window activation are transient runtime state and are not persisted.

The microscope preview remains the fixed central workspace and is not a movable member of the tool-panel tree.

### 5.3 Dock controller

A main-window-scoped controller owns the snapshot, validates commands, computes mutations and publishes state changes.

The public command surface should be semantic rather than view-specific:

- `selectPanel(panelID)`
- `selectTab(groupID, panelID)`
- `dock(panelID, edge, targetGroupID, zone)`
- `moveToTabGroup(panelID, targetGroupID)`
- `splitGroup(groupID, panelID, axis, side)`
- `moveToAutoHide(panelID, edge)`
- `revealAutoHide(groupID)`
- `float(panelID, frame)`
- `floatGroup(groupID, frame)`
- `restoreToPreviousDockLocation(panelID)`
- `resizeSplit(splitID, firstChildProportion)`
- `hideActivePanel(groupID)`

Command validation happens before mutation. UI callbacks request commands; they do not independently edit the serialized tree.

### 5.4 Renderer and host lifecycle

AppKit renders split containers, tab bars, drag targets and floating windows. Each registered panel's `NSViewController` is moved between containers; it is not destroyed and recreated during a docking operation.

SwiftUI is hosted inside the panel controller through `NSHostingController`. The renderer is responsible for containment constraints and activation; the registry remains responsible for panel identity and lifetime.

The controller should be owned by a main-window controller or another main-window-scoped owner, not by a transient `NSViewRepresentable` anchor and not by `NativeCameraModel`.

### 5.5 Persistence

Persist one versioned Codable snapshot under an application-specific key in `UserDefaults`. Persist after accepted layout mutations and floating-window move/resize events. Avoid writing during intermediate restore/reconciliation operations.

On restoration:

- Validate IDs, group membership, split ratios and frames.
- Clamp floating windows to a currently visible screen.
- Apply minimum sizes.
- Recover to a safe default layout if the snapshot is corrupt or incompatible.
- Do not mark an un-restored camera panel as closed or alter camera lifecycle.

Migration must preserve the current dock width and, where possible, the existing `AmscopeWorkspaceDock` floating frame autosave. The old key should remain readable until the new snapshot has been restored successfully.

## 6. Placement and interaction semantics

These states are distinct and must not be collapsed into two booleans:

| State | Meaning |
|---|---|
| Docked | Panel belongs to a main-window dock region and consumes layout space. |
| Auto-hidden | Panel is represented in the edge rail; it does not consume layout space. |
| Auto-hide revealed | Temporary flyout over the workspace; transient and not a second panel instance. |
| Floating | Panel or tab group belongs to a detached native window. |
| Unplaced | Registered panel has no current location; this is a recoverable state, not destruction. |

The permanent tool rail is independent of placement. It remains overlaid on the main window's right edge, keeps the current compact vertical labels and does not reserve workspace width.

Rail activation behavior is placement-aware:

- Docked panel: select its tab and ensure its dock region is shown.
- Auto-hidden panel: reveal its flyout over the workspace without resizing the preview.
- Floating panel: bring its owning window forward and select the panel.
- Unplaced panel: place it in the default trailing group.

A drag onto a tab center adds the panel to that tab group. A drag onto a region edge creates a split. A drag outside the main window may create a floating window. Drop targets must not interfere with slide measurement/annotation gestures inside the preview.

### Floating and restore

Floating preserves the panel ID, controller instance, tab-group membership and prior dock location. Docking back restores the previous group and tab position where it still exists; if it no longer exists, the controller uses a deterministic fallback region.

Closing a floating tool window must not destroy a registered core panel. The default close policy returns it to its previous dock location or auto-hidden state. A future plugin/temporary-panel API may support terminal close as a separate explicit command.

### Reparenting and lifecycle

A reparent is not a panel destruction event. The view controller must be detached from the old parent and attached to the new parent without discarding its associated model or services.

The same panel ID may not be simultaneously hosted in a dock region and a floating window. Duplicate presentation is an invariant violation.

## 7. Non-negotiable invariants

1. The preview surface remains a single stable AppKit owner and stays mounted during dock mutations.
2. No dock operation starts, stops, opens or closes the camera.
3. No change is made to the acquisition callback, event queue, acquisition worker, frame pool, mailbox, frame leases or pixel pipeline.
4. Dock state does not live in `NativeCameraModel`; camera state does not live in the dock snapshot.
5. A registered panel has one stable identity and at most one active host.
6. Moving a panel does not recreate its `ObservableObject` dependencies or release capture-gallery state.
7. The permanent right rail overlays the application and never reduces preview dimensions.
8. Floating window close, main window close and application termination have explicit, separate policies.
9. Layout restoration is validated and recoverable; malformed persistence cannot prevent application launch.
10. All AppKit window and view-controller mutations occur on the main thread.

## 8. Implementation phases

### Phase 0 — Branch and baseline audit

**Status: complete.**

- Created `amscope-native-app-mac.v.0.8-docksystem` from the exact current v0.8 tip.
- Audited the main window, current workspace dock, camera model, app delegate, capture gallery and CMake target.
- Recorded camera/preview ownership constraints and current persistence behavior.
- Compared native framework capabilities and inspected DockKit as a candidate.

**Exit condition:** this plan is committed on the new branch without changing the baseline branch.

### Phase 1 — Core layout model and validation

- Add stable panel IDs, edges, split axes, tab-group nodes, recursive split nodes, auto-hide groups and floating-window state.
- Add Codable snapshot/version representation.
- Add structural validation: unique panel placement, unique group/window IDs, non-empty groups, valid active tabs, finite split proportions and valid window frames.
- Add traversal helpers to resolve a panel's current placement.
- Keep this layer independent of AppKit, SwiftUI and camera types.

**Exit condition:** model can encode/decode, validate defaults and reject malformed layouts through focused automated tests.

### Phase 2 — Panel registry and controller

- Add a main-window-scoped panel registry with stable content-controller instances.
- Add a semantic command/reducer layer for selection, hide, move, float and restore.
- Add a versioned layout store and migration from the existing dock width/frame preferences.
- Register Camera Controls and Smart Gallery without changing their content views.
- Keep camera and capture-gallery objects application-owned.

**Exit condition:** both panels can be selected and moved in the model while panel identity and shared model state remain stable.

### Phase 3 — Replace the one-off dock renderer

- Replace the two-value switch in `NativeWorkspaceDock` with registry-driven rendering.
- Preserve the current right-side dock as the initial default region.
- Preserve the resize affordance and 280–520 point width bounds unless a region-specific size policy supersedes them.
- Keep the permanent rail rendered as a main-window overlay.
- Preserve the current visual treatment: compact vertical tabs, rotated text, visible selected state, no dedicated horizontal title bar.

**Exit condition:** the app looks and behaves like the current branch for the two initial panels, but all selection and visibility are driven by the new controller.

### Phase 4 — Tab groups, splitting and drag/drop

- Support multiple tab groups in a region.
- Implement tab reorder and tab-to-tab-group movement.
- Implement left/right/top/bottom split drop zones with visible previews.
- Persist split proportions and tab selection.
- Enforce panel capability and minimum-size constraints before accepting a drop.

**Exit condition:** two or more independent dock groups can be built, resized, tabbed and restored without duplicate panel hosts.

### Phase 5 — Floating tool windows

- Support floating a panel and an entire tab group.
- Create one native floating host per saved floating-window state.
- Reparent existing panel controllers instead of recreating them.
- Track frame and screen identity; clamp off-screen windows after display changes.
- Dock back to the saved prior location; use a deterministic fallback if the original group no longer exists.

**Exit condition:** dock → float → move/resize → dock round-trips the same panel identity and layout membership.

### Phase 6 — Auto-hide and permanent rail behavior

- Add auto-hide placement independently of the permanent launcher rail.
- Reveal auto-hidden content as an overlay that does not resize the preview.
- Support pinning a flyout into a dock region and auto-hiding it again.
- Define outside-click, Escape, focus, hover and activation behavior without making the panel inaccessible to keyboard users.
- Keep the rail permanently overlaid at the main window's right edge.

**Exit condition:** docked, auto-hidden, revealed and floating states are independently reachable and have deterministic transitions.

### Phase 7 — Persistence, migration and recovery hardening

- Restore the entire layout, selected tabs, split proportions and floating frames.
- Migrate the legacy dock width and floating-panel autosave data.
- Validate saved layouts before creating windows or changing view containment.
- Handle missing displays, invalid frames, unknown panel IDs and schema-version mismatch.
- Save only stable states and prevent restore-time notifications from overwriting the snapshot being restored.

**Exit condition:** repeated app restarts preserve the layout and recover safely from intentionally malformed snapshots.

### Phase 8 — UX, accessibility and integration verification

- Add accessible labels, keyboard activation, visible focus, tab navigation and drag alternatives.
- Check minimum dimensions, narrow windows, full-screen, multiple displays and floating-panel z-order.
- Verify that the preview remains one mounted surface and that camera acquisition is unaffected by every docking transition.
- Run the native app build and UI smoke procedure on the user's Mac; perform camera hardware comparison only if the user elects to validate streaming afterward.

**Exit condition:** the docking system meets its functional and lifecycle acceptance criteria without regressions to camera acquisition or rendering.

## 9. Acceptance matrix

| Area | Required result |
|---|---|
| Panel identity | No duplicate panel host; identity stable across every move. |
| Docking | Tab-center and edge drops produce the intended tab group or split. |
| Resizing | Split ratios persist and respect panel minimum sizes. |
| Floating | Detached windows are native, movable, resizable and restorable. |
| Rail | Always visible over the main window, compact, interactive and layout-neutral. |
| Auto-hide | Flyout overlays content and does not resize the preview. |
| Persistence | Full layout restores, including floating frames and selected tabs. |
| Recovery | Corrupt or obsolete state falls back safely. |
| Preview | One stable AppKit preview owner; no new frame consumers. |
| Camera | No docking-triggered camera lifecycle or acquisition changes. |
| Threading | Window/controller mutations remain on the main thread. |

## 10. Working rules for this branch

- Do not modify the C++ driver, Objective-C++ camera bridge, frame mailbox or preview consumer as part of docking work.
- Do not add DockKit or raise the deployment target without a separate decision.
- Do not implement panel placement as more booleans on `NativeCameraModel`.
- Do not recreate panel controllers during reparenting.
- Keep each phase reviewable and commit it independently.
- Do not claim build, test or visual verification unless it has actually been performed. The implementation work here will not run local builds or hardware tests; the user will run the provided local build and report diagnostics.

## 11. Implementation progress

### Phase 0 — complete

- New branch created from the recorded v0.8 base commit.
- This plan committed without changes to the base branch.

### Phase 1 — model implementation in progress

Committed source increments:

- `NativeDockSystemModel.swift`: stable panel IDs; edge, axis, tab-group, recursive split, auto-hide and floating-window models; versioned Codable layout snapshot; default layout matching the existing trailing two-tab dock; placement traversal; structural validator.
- `NativeDockSystemModelTests.swift`: executable checks for default placement, Codable round-trip, duplicate panel placement, active-tab membership, invalid split proportions and invalid floating frames.
- `Camera/Amscope/CMakeLists.txt`: model source registered with the native app and a dedicated `amscope_native_dock_model_tests` target registered with CTest when the native app and testing are enabled.

**Verification status:** source and build-target registration were reviewed statically. No CMake configure, build, test, app launch or hardware test was run. Phase 1 remains open until the user runs the native model test target and reports any compiler/runtime diagnostics.

### Immediate next work

1. Confirm the model test target compiles on the user's macOS/Swift toolchain.
2. Add the panel registry and main-window-scoped controller.
3. Implement model mutations and persistence before replacing the current renderer.
4. Adapt the existing two panels through the registry while preserving the current UI behavior.

### Follow-up — rail reopens a closed dock

- Added `NativeCameraModel.showWorkspaceDock(_:)`: selecting a permanent rail item selects that panel, clears stale floating placement and makes the in-window dock visible.
- Added `closeWorkspaceDock()` and routed the dock close button and floating-window close delegate through it.
- Routed the main dock close button through the same state transition.
- Made the complete rectangular rail button bounds explicit and kept the rail overlay above the workspace with hit testing enabled.

**Expected behavior:** after closing either dock presentation, clicking Camera Controls or Smart Gallery on the permanent right rail selects that tool and reopens the docked panel in the main window. The camera session and capture gallery are not affected.

**Verification:** source paths and state transitions statically inspected. Build and runtime verification remain pending on the user's Mac.

## 12. Follow-up implementation — dock caption and generic measurement scale

### Dock chrome

- The composite workspace dock remains docked in the main window. The whole-dock floating `NSPanel` host and the "Float this dock" action have been removed.
- The close control is part of a fixed 36-point dock caption header, alongside the active tool title. It is not an overlay floating over panel content and does not scroll with the content.
- Individual tool panels may still become floating windows as a future docking-system operation. That is distinct from floating the entire right-side dock.

### Application-level optical measurement

The application now computes specimen-plane scale from generic configured component specifications:

```text
µm per output pixel =
    sensor pixel pitch (µm) × output pixel factor
    ───────────────────────────────────────────────
    objective magnification × reduction-lens magnification
```

- The calculation is in `NativeMicroscopeMeasurementCalibration`, independent of camera brand, model, image dimensions, preview zoom and display scale.
- The user configures sensor pixel pitch and the current linear output-pixel factor. A factor of 1.0 means native readout; 2.0 represents 2× linear binning/resampling. ROI cropping alone remains 1.0.
- The app deliberately does not infer binning from frame dimensions. Identical dimensions may result from cropping, binning or resampling and do not uniquely determine specimen scale.
- The active objective magnification and the effective combined reduction/relay magnification are taken from microscope configuration.
- Each measurement captures the scale at the start of the gesture. Changing objective or optics configuration does not retroactively rewrite already recorded measurements.
- The overlay shows both calculated `µm` and source `px`. Missing or invalid specifications produce a pixel-only label marked "scale unavailable".
- The microscope configuration displays the calculated `µm/px` before applying changes.
- Legacy sensor/binning configuration keys are decoded into the generic output factor. A former MU503-named 0.50× relay preset is migrated to a generic 0.50× optical preset.

This remains a specification-derived estimate. For metrologically traceable measurements, calibrate the complete installed optical path against a certified stage micrometer and retain that empirical calibration as a later override.

### Added automated test coverage

`NativeMicroscopeCalibrationTests.swift` now checks the generic formula with multiple sensor pitches, output factors, objectives and reducer magnifications, including invalid inputs. No camera model or output-frame dimensions appear in the calibration API or tests.

**Verification status:** source and CMake registrations statically inspected. No CMake configure, compile, CTest, app launch or physical microscope calibration was run here.
