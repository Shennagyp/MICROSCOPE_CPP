# AmScope UI Component System Migration

**Status:** Architecture decision accepted; component-library integration is pending.
**Branch:** `amscope-fluent-ui-components`
**Baseline:** `amscope-native-driver-0.7` at `17067f3c5b10a4bb0e748297bbd34ada295bcc43`
**Decision date:** 2026-11-24

## 1. Problem statement

The current AmScope application has a substantially improved workspace shell, but its controls still look like conventional Qt Widgets controls. The application sets the Fusion style and then overrides a large set of standard widgets with a global stylesheet in `PreviewWindow::applyProfessionalStyle()`.

This creates a weak visual system:

- Controls are styled by broad selector rules rather than by a coherent component library.
- The visual language, sizes, states, typography, and layout are maintained manually in the application.
- The same style rules affect controls with different interaction semantics.
- The UI has no reusable component-level theme contract.
- Updating the application shell does not automatically improve the actual controls.

This is not a Qt 6 age problem. It is a component-system and presentation-architecture problem.

## 2. Existing application boundaries

The current executable is a Qt Widgets application:

- `QApplication` and `QMainWindow` own the desktop shell.
- `QDockWidget` provides Camera Explorer, Camera Properties, and Output.
- A central `QTabWidget` hosts the Live Preview document.
- `camera::qtwidgets::FrameView` presents the live frame lease.
- `camera::qtwidgets::CameraControlPanel` provides backend-neutral discovery and lifecycle intent controls.
- Camera feature editors are generated from the descriptors exposed by `amscope::QtAmcamCamera`.
- The AmScope camera facade owns the queued camera-operation executor and delegates to the single native driver session.

The present frame presentation contract is important. `FrameView` wraps the immutable `camera::FramePtr` storage in a `QImage` and paints it. It does not create a new owned image for every frame and does not introduce a GPU upload path.

## 3. Decision

Adopt **FluentQt v1.12.2** as the first component-library candidate for the existing QWidget application, subject to an isolated integration gate.

Upstream project:
- Repository: https://github.com/calvinhxx/Fluent-Qt
- Fixed release tag: `v1.12.2`
- Tag commit observed during assessment: `441270ec010aebbb149d996e398722f9da5a2deb`
- License: MIT
- Core integration target: `FluentQt::FluentQt`
- Core UI dependency: Qt Widgets
- Language baseline: C++17
- Documented Qt minimum: Qt 6.2

The library provides native C++ QWidget components and a theme/token system. Its controls derive from or integrate with existing QWidget controls (for example, the Fluent button retains QPushButton behavior). It offers reusable input, text-field, navigation, layout, dialog, status, collection, typography, icon, spacing, and theme facilities.

The release tag is pinned rather than consuming `main`. The assessment found an upstream full CI run succeeding for the v1.12.2 release line. This is upstream evidence only; it does not establish that the library builds in this repository or behaves correctly on the target Mac.

### Why this instead of immediately adopting Qt Quick

Qt Quick Controls is the closer conceptual equivalent to SwiftUI: declarative QML components, states, property bindings, transitions, and a component styling system.

However, the application already has a working QWidget shell and a QWidget live preview. Embedding the QML scene using `QQuickWidget` would add an offscreen render-to-texture/composition path and disables the threaded Qt Quick render loop. That is not a neutral choice for a live imaging application.

A full Qt Quick rewrite could be considered only as a separate architecture decision with its own native frame-item implementation and measured frame-path qualification. It is not the first step in this UI redesign.

### Alternatives considered

| Candidate | Role | Assessment |
|---|---|---|
| FluentQt | Ready-made QWidget component system and theme | Preferred first candidate; closest match to the existing app architecture |
| Qlementine | Modern QStyle for existing Qt Widgets | Useful alternative where the goal is broad restyling of stock controls; does not provide the same breadth of component APIs; current upstream requirement is Qt 6.8+ |
| Qt Quick Controls | Declarative component framework | Architecturally strong, but a QQuickWidget bridge is an undesirable default for this frame-centric window |
| Handwritten QSS | Application-local styling | Rejected as the primary component architecture; retained only for tightly scoped host-shell details when required |

The visual direction is a restrained scientific-instrument workbench, not a Windows imitation. FluentQt is a component implementation candidate, not permission to copy a generic Fluent app layout or add decorative chrome. The first visual acceptance check must be performed on macOS.

## 4. Dependency policy

The first integration must be opt-in and isolated.

- Add a dedicated CMake option for the FluentQt component integration; leave it OFF until the first Mac build and visual acceptance gate passes.
- Use `FetchContent` with `GIT_TAG v1.12.2`; do not track the upstream default branch.
- Explicitly disable unrelated upstream modules: Gallery, examples, tests (unless run as a separately selected validation), particles, Spatial, plugins, QML bridge, PySide bindings, and WebAssembly support.
- Link only `FluentQt::FluentQt` into the UI target that consumes its components.
- Do not make backend-neutral camera contracts or the native driver depend on FluentQt.
- Do not add Qt Quick, Qt Quick Widgets, OpenGL, a second rendering path, or another acquisition target as part of this integration.
- Preserve the current minimum Qt version unless a demonstrated component requirement forces a deliberate, separately documented change.

The component library must remain a presentation dependency. Its types must not appear in the driver, common camera contracts, SDK callback, event queue, frame lease, or acquisition worker.

## 5. Ownership and interaction semantics

The existing camera-facing contracts remain authoritative.

- `AmcamCamera` remains the sole owner of the native `HAmcam` session.
- The callback still only enqueues bounded event codes.
- One existing application acquisition worker still performs `Amcam_PullImageV4`.
- `QtAmcamCamera` retains the current serialized asynchronous request APIs, ordering, generations, and callback delivery semantics.
- UI widgets express user intent and render returned state. They do not own camera sessions, enumerate SDK devices, issue direct SDK calls, or perform blocking camera work.
- Existing feature descriptors remain the authority for editor type, range, increment, enum choices, readability, writability, and availability.
- A UI write must still pass through the existing descriptor validation and request methods. A widget's visual constraints are not a replacement for backend validation.
- Existing selected-device and feature-request generation guards remain in place.

FluentQt component instances may be held through existing base-widget pointers when the public type relationship permits it. This must not change signal ordering, callback routing, QObject ownership, or destruction order.

## 6. Frame-path invariants

The following are non-negotiable for the component migration:

1. Do not replace `FrameView` with a QML `Image`, `QQuickPaintedItem`, `QQuickWidget`, or per-frame QWidget.
2. Do not insert a QImage deep copy, pixel conversion, per-frame heap allocation, image decoder, or additional queue.
3. Do not add a second camera facade, native session, callback, acquisition worker, polling loop, or SDK call path.
4. Do not add a UI mutex to the frame handoff or alter the existing mailbox and lease lifetime.
5. The preview must continue to render the same `camera::FramePtr` contract and metadata.
6. Any future custom renderer or GPU path is a separate measured performance project, not part of component styling.

## 7. Theme and raw-widget policy

FluentQt controls receive the Fluent theme through their component-level token contract. Existing raw Qt widgets do not automatically inherit those semantic tokens.

Therefore:

- Centralize Light and Dark semantic theme setup in one AmScope application UI module.
- Install Fluent theme tokens before constructing the main window.
- Use FluentQt components for visible controls where a suitable component exists.
- For remaining raw widgets, provide a small theme adapter that applies semantic `QPalette` roles and updates them when the theme changes.
- Do not retain the present global stylesheet as a second competing component system.
- Keep narrowly scoped QSS only for host-shell cases that cannot be represented by the theme or a component API.
- Theme both modes together; do not implement Dark by inverting Light.
- Preserve visible focus, hover, pressed, checked, disabled, warning, error, and streaming states.

## 8. Migration sequence

### UI-0 — Decision and boundary record

- Record the candidate, constraints, rejected alternatives, and validation gates.
- Keep the current Widgets executable unchanged.
- Establish a separate migration branch.

**Exit:** this document committed and branch isolated.

### UI-1 — Dependency and visual proof

- Add the opt-in FluentQt CMake integration.
- Build a small application-owned component gallery using the exact Qt kit and compiler used by the AmScope app.
- Exercise the components needed by the current application: Button, ComboBox, CheckBox, Slider, numeric input, tabs, cards/group headers, menus/dialogs, and status feedback.
- Verify the library's Light and Dark theme behavior and fonts/icons on macOS.
- Confirm that no QML/Quick/Spatial/Particles modules are pulled into the core target.
- Review the upstream license and exact pinned source revision.

**Exit:** owner build succeeds; the component gallery opens on the target Mac; visual direction is accepted; the standard AmScope target remains unchanged when the option is OFF.

### UI-2 — Theme foundation and component adapter

- Add AmScope-owned theme initialization and a single theme-switch/state source.
- Add only the small adapters necessary to create and configure shared Fluent controls.
- Add the raw-QWidget semantic palette adapter for components that cannot yet be migrated.
- Ensure controls are not styled by overlapping app-wide QSS and Fluent component painting.
- Add deterministic tests for token initialization, theme changes, control state mapping, and widget lifecycle.

**Exit:** both theme modes update predictably; raw widgets and Fluent widgets remain visually coherent; no UI action behavior changes.

### UI-3 — Camera Explorer migration

- Migrate the discovery combo, refresh action, Open/Start/Stop/Close actions, still-mode selectors, resolution selector, and capture action.
- Preserve opaque device ID routing and the existing state/pending action gating.
- Keep callback signatures and request ownership unchanged.
- Keep the backend-neutral Camera::Common contracts independent of FluentQt.

**Exit:** existing CameraControlPanel tests and new visual/state tests pass; user can discover/open/start/stop/close and capture using the same operation flow as before.

### UI-4 — Camera Properties migration

- Replace stock controls in the descriptor-driven editors with suitable Fluent components.
- Keep descriptor-driven editor creation, current-value gating, range and enumeration semantics, generation checks, error display, refresh behavior, and write/readback flow.
- Retain raw domain-specific compound controls only where no suitable component exists; style them via the shared theme adapter.
- Migrate accessory and trigger controls without moving command logic into widgets.

**Exit:** all supported existing controls are reachable; read-only/unavailable/disabled states are accurate; settings retain the existing async request behavior.

### UI-5 — Workspace and diagnostics polish

- Align QMainWindow, dock headers, document tabs, Output, status area, menus and dialogs with the same visual tokens.
- Keep the current document-centric/dockable workspace behavior and QSettings layout persistence.
- Preserve the existing log retention bound.
- Remove superseded global QSS control rules; retain only local host-shell rules with an explicit reason.

**Exit:** workspace, panels, dialogs, menus, and controls look like one product in both modes.

### UI-6 — Regression and acceptance

Separate evidence into compile, unit, integration, visual, and hardware categories.

- Build the default configuration with Fluent integration OFF.
- Build the Fluent UI configuration with the exact selected Qt 6 kit.
- Run the entire existing CTest suite and newly added UI tests.
- Launch both the current default app and Fluent app on macOS.
- Compare frame presentation rate and stability before/after with the same camera, resolution, pixel format, exposure, and scene.
- Verify no new acquisition drop, stale-session update, UI freeze, or control-state regression.
- Perform the physical AmScope MU503 smoke test.
- Capture Light/Dark screenshots and record the exact commit, Qt version, macOS version, and test evidence.

**Exit:** all software gates pass; visual acceptance is recorded; physical camera smoke is recorded. A build alone is not UI acceptance.

## 9. Component mapping

| Existing UI responsibility | Initial FluentQt direction | Contract that must remain |
|---|---|---|
| Camera discovery | Fluent ComboBox | Device label and opaque ID remain separate |
| Open / Start / Stop / Close | Fluent Button | Existing allowed-action state machine and callbacks |
| Still capture mode / resolution | Fluent ComboBox | SDK-advertised mode values only |
| Capture still | Fluent Button | Same request queue and frame result |
| Boolean feature | Fluent CheckBox or ToggleSwitch, depending on semantics | Writable/readable/available descriptor state |
| Enumeration feature | Fluent ComboBox | Exact symbolic values, not enum ordinals |
| Integer / floating feature | Suitable numeric editor | Exact type, bounds, increment, and units |
| Numeric range feature | Fluent Slider paired with numeric readout/editor | No loss of precision or range semantics |
| Grouping of feature categories | Fluent card/group-header patterns | Feature grouping remains descriptor-driven |
| Information strip | Existing ImageInfoBar initially | Stable IDs and double-click edit routing |
| Live image | Existing FrameView | Unchanged frame lease and paint path |
| Output | Existing bounded Output view initially | Existing 2,000-block retention bound |

The exact chosen component must be checked against its actual public API in the pinned release. A similarly named control must not be assumed to have matching keyboard, accessibility, precision, or model semantics.

## 10. Risks and explicit non-goals

### Risks

- FluentQt is a young upstream dependency compared with Qt itself. Pinning the release, retaining an opt-out, and testing its exact macOS integration reduce but do not eliminate this risk.
- Custom Fluent controls and raw Qt controls can look inconsistent until the raw-widget theme adapter is complete.
- Some controls may not be exact replacements for descriptor-driven numeric/vector editors. A custom adapter may be needed.
- A theme library does not automatically redesign the dock/workspace shell.
- Component updates can affect focus, keyboard behavior, popup positioning, and high-DPI sizing; these require direct verification on the target Mac.

### Non-goals

- No rewrite to Qt Quick/QML in this increment.
- No QQuickWidget embedding.
- No change to the camera backend, vendor SDK usage, feature contract, session lifecycle, request worker, acquisition worker, frame queue, mailbox, frame ownership, pixel formats, or image processing.
- No new node editor, graph editor, backend, or parallel application service.
- No general redesign of the product's scientific workflow; this work is a presentation/component migration.

## 11. Current status

- [x] Existing Widgets shell and component usage inspected.
- [x] Current global Fusion/QSS styling identified as an important source of the weak component system.
- [x] FluentQt v1.12.2 assessed as a QWidget-native component candidate.
- [x] Qt Quick/QQuickWidget rejected as the first migration route because of the existing live preview architecture.
- [ ] UI-1 dependency build and visual proof on the target Mac.
- [ ] UI-2 through UI-6.

No FluentQt code has been integrated into the production AmScope target yet. This branch begins with the decision and the isolated migration plan; the existing `amscope-native-driver-0.7` branch remains the known working baseline.
