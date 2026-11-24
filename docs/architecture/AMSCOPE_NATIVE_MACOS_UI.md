# AmScope Native macOS UI

**Status:** Native styling direction accepted; first source correction committed; visual acceptance pending.
**Branch:** `amscope-native-ui`
**Baseline:** `amscope-native-driver-0.7`
**Decision date:** 2026-11-24

## 1. Decision

Use Qt's own **macOS platform style** for the AmScope Qt Widgets application.

Do not impose a third-party design language over the application. Do not force Fusion. Do not use a global Qt Style Sheet to repaint standard controls.

The existing application is a Qt Widgets application and already has a complete native control vocabulary:

- `QPushButton` / `QToolButton`
- `QComboBox`
- `QLineEdit`, `QSpinBox`, `QDoubleSpinBox`
- `QSlider`
- `QCheckBox`, `QRadioButton`, `QButtonGroup`
- `QTabWidget`, `QGroupBox`
- `QMenu`, `QMenuBar`, `QToolBar`
- `QTreeView`, `QTableView`, `QListView`
- `QProgressBar`, `QStatusBar`
- Standard Qt dialogs and file pickers

On macOS, Qt supplies the native `QMacStyle` platform style. Standard widgets delegate their control painting and metrics to the active `QStyle`; the platform style provides the native look and feel. Let Qt select the platform style normally.

The relevant official Qt references are:
- [Qt Widgets styles and style-aware widgets](https://doc.qt.io/qt-6/qstyle.html)
- [Qt Widget Gallery](https://doc.qt.io/qt-6/gallery.html)
- [Qt Widgets styling](https://doc.qt.io/qt-6/qwidget-styling.html)
- [Writing Style-Aware Widgets](https://doc.qt.io/qt-6/style-aware-widgets.html)

## 2. Root cause found in the previous UI

The previous `applyProfessionalStyle()` function did three things that worked against native macOS controls:

1. It explicitly selected the cross-platform `Fusion` style using `QStyleFactory::create("Fusion")`.
2. It forced a 9 pt application-wide font instead of allowing the platform's native font defaults.
3. It installed a large application-wide stylesheet that overrode the appearance of buttons, fields, combo boxes, tabs, dock headers, menus, toolbars, scrollbars and other controls.

The styling problem was therefore not that Qt 6 lacked controls. The application was deliberately overriding Qt's platform control style.

## 3. Native styling rules

- Do not call `QApplication::setStyle()` in the production application unless a platform-specific, explicitly approved reason exists.
- Do not globally force `Fusion`.
- Do not install an application-wide `QApplication::setStyleSheet()`.
- Do not globally set the application font to a fixed point size; allow the macOS platform style and system text settings to determine standard control typography.
- Do not globally set custom foreground/background palettes for ordinary native controls.
- Prefer standard Qt Widgets with their ordinary properties, signals, layouts, and models.
- Do not recreate buttons, fields, menus, combo boxes, sliders, tab controls or scrollbars with custom painting.
- Preserve keyboard, accessibility, focus, validation, disabled and popup behavior provided by the standard widgets.
- Use custom painting only for domain-specific visualizations that are not ordinary controls, notably the camera image viewport and compact image-information strip.
- If a new composite camera control is needed, compose standard Qt widgets in a small reusable QWidget; do not create a second visual system for the inner controls.
- If a genuinely custom widget is unavoidable, make it style-aware: use `QStyleOption`, `QStyle::drawControl`, `QStyle::drawPrimitive`, `QStyle::pixelMetric` and the active palette rather than hard-coded platform-specific painting.

A narrowly scoped stylesheet may be considered only for a documented behavior or geometry that the platform style cannot express. It must not repaint standard control surfaces.

## 4. Native style versus a native Cocoa view

These concepts are related but not identical:

- **Qt native platform style:** Qt Widgets remain QWidget objects, while QMacStyle supplies native platform rendering/metrics and normal Qt event/model semantics. This is the selected path.
- **Direct AppKit control:** an actual `NSControl` / `NSView` is hosted through Objective-C++ and Qt's macOS native interfaces. This needs explicit ownership, focus, accessibility, resize, event and lifetime integration. Use it only if a specific control cannot be delivered correctly through the Qt platform style.
- **Qt Quick Controls macOS style:** native-framework-drawn controls in a declarative QML layer. This is not needed for the present QWidget application and would introduce a second UI technology and embedding constraints.

This work interprets “native” as the macOS-native appearance and behavior of Qt's platform widgets, not a wholesale AppKit rewrite.

If the product later requires actual AppKit view instances or SwiftUI's declarative composition model, treat that as a separate architecture change. Do not mix a handful of embedded AppKit views into the current QWidget hierarchy without an explicit requirement and lifecycle design.

## 5. Preserve the current application architecture

Keep the existing workspace and backend boundaries:

- `QMainWindow` and the document-centric Live Preview tab stay in place.
- Camera Explorer, Camera Properties and Output remain QWidget tool windows.
- The existing File/View entry points and QSettings workspace persistence remain.
- `FrameView` remains the central live image presentation widget.
- `ImageInfoBar` remains the editable image/camera information surface.
- Feature editors remain descriptor-driven and use the existing `QtAmcamCamera` asynchronous request API.
- Camera and imaging operations remain accessible from the existing application actions/panels.

No UI style decision may change the following camera/frame invariants:

1. One `AmcamCamera` owns the only native `HAmcam` session.
2. SDK callbacks only enqueue event codes into the bounded event queue.
3. One application acquisition worker handles image events and calls `Amcam_PullImageV4`.
4. The GUI receives the existing immutable `camera::FramePtr` lease through the mailbox.
5. `FrameView` wraps the existing memory in a `QImage`; it does not make a per-frame deep copy.
6. Feature, command, lifecycle and telemetry operations retain their current serialized asynchronous request behavior and generation checks.

Do not add QML, Qt Quick, QQuickWidget, a second renderer, another camera facade, another frame queue, or another worker as part of restoring native appearance.

## 6. First implementation

The new `amscope-native-ui` branch removes the previous `applyProfessionalStyle()` function and its invocation from `Camera/Amscope/app/main.cpp`.

This removes:
- explicit Fusion selection;
- forced application font size;
- the global application stylesheet and its standard-control overrides.

No camera logic, workspace structure, native driver, feature contract, acquisition code, frame mailbox, or reusable widget contract is changed by that source edit.

The result should use the platform style selected by the current Qt/macOS installation. This must be verified on the owner's actual Mac; source inspection alone does not prove what style plugin is installed or loaded at runtime.

## 7. Verification and acceptance

Evidence categories must remain separate:

### Source checks

- Confirm there is no production `setStyle(Fusion)`, global application stylesheet, or global forced font.
- Confirm standard controls remain standard Qt Widgets.
- Confirm domain-specific custom drawing is limited to the image viewport and information presentation.

### Build and software tests

- Build the normal `amscope_preview` target using the owner's existing Qt 6 kit.
- Run the full existing CTest suite.
- Verify the normal launcher still starts the same executable.
- Confirm the native-UI branch does not require a new third-party dependency or additional Qt module.

### Visual acceptance on macOS

- Capture the application with the native light appearance and dark appearance.
- Inspect buttons, menus, toolbar, combo boxes, spin boxes, check boxes, sliders, tabs, group boxes, scrollbars, status bar, dock headers and dialogs.
- Confirm the system font, sizing, control geometry, popup behavior and focus rings look consistent with current macOS applications.
- Confirm there are no unexpected Fusion controls or flat custom QSS surfaces.
- Review the central image viewport separately: it is a scientific image canvas, not a native form control.
- Verify the existing dock layout, document tab, File/Camera Workspace access, Output visibility, and persisted layout still work.

### Camera regression

- Launch the application with the MU503.
- Open, start, stop, restart, close and reopen using the existing workflow.
- Confirm live RGB and RAW presentation behavior is unchanged.
- Record event/pull/paint rates, dimensions, drops and stability using the existing diagnostic path. Compare against the same camera configuration and baseline.
- Verify feature readback and editing still flow through the descriptor-driven async request path.

No compile, CTest, visual, or hardware result is inferred from the source edit. These remain pending owner-side verification.

## 8. Why no component library

Qt Widgets itself is the component library for this application. It already supplies the standard control primitives and their platform style integration.

A library such as FluentQt or Qlementine would impose a separate visual language or custom control rendering over the macOS native style. That conflicts with the requested native appearance and is not required to obtain standard macOS buttons, fields, combo boxes, sliders, menus, tabs or dialogs.

The earlier experimental branch `amscope-fluent-ui-components` is therefore superseded and must not be merged into the native application. It may remain as a reference experiment, but it is no longer the active implementation direction.

## 9. Current status

- [x] Root cause identified: forced Fusion + application-wide QSS + fixed global font.
- [x] New isolated branch created from the current native-driver baseline.
- [x] Forced style and application-wide stylesheet removed.
- [ ] Owner Release build.
- [ ] Full CTest.
- [ ] macOS light/dark visual acceptance.
- [ ] Physical MU503 regression and performance comparison.

The current source correction is a minimal restoration of Qt's platform-native styling, not a completed UI acceptance claim.
