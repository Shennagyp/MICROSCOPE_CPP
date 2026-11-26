# Camera Qt Widgets

This target contains reusable Qt Widgets that depend only on the backend-neutral
`Camera::Common` contracts and Qt Widgets. It is separate from
`Camera::Qt`, which is the Qt Core frame-mailbox layer.

## FrameView

`camera::qtwidgets::FrameView` presents common `camera::Frame`
layouts:

- `RGB24` is wrapped as `QImage::Format_RGB888`.
- `Raw8` is presented as grayscale sensor samples.
- `Raw16` is wrapped as `QImage::Format_Grayscale16`.

The `QImage` wraps the frame's existing pixel memory. `FrameView`
retains the immutable `camera::FramePtr` lease for as long as the image
references that memory. It does not create an owned per-frame image, demosaic
RAW data, or claim a GPU upload path. `QPainter` performs the widget
paint operation.

The optional frame-presented callback runs after a newly submitted valid frame
is actually painted. It can be used by a host application for presentation
telemetry; the widget does not own a camera, acquisition loop, frame queue, or
rate estimator.

## CameraControlPanel

`camera::qtwidgets::CameraControlPanel` presents the common camera discovery
list and the generic Open/Start/Stop/Close controls. It accepts
`camera::CameraDeviceInfo` values and reports user intent through host-supplied
callbacks. The host supplies a coarse presentation state, including discovery
and lifecycle-request pending flags.

The panel does not enumerate devices, interpret backend errors, own a camera
session, schedule operations, or know about a vendor API. In particular, the
host remains responsible for opaque-ID routing, request ordering, generations,
and session lifetime. The selection-changed callback reports the selected ID
unchanged within the current discovery result. A new setDevices snapshot resets
the selection to the new result set; IDs are enumeration handles, not stable
physical-camera identities across disconnect/replug. A faulted session remains
closeable but cannot be started or stopped through this panel. Its compact two-row layout puts device selection
and refresh on the first row and Open/Start/Stop/Close on the second. The
AmScope host places this panel in the Camera Explorer tool window; feature
settings and descriptor groups live in a separate Camera Properties tool
window. Both panes are dockable, tabifiable, floatable, and included in the
persisted workspace layout.

The `camera_qt_widgets_camera_control_panel_tests` offscreen test covers
device-label/opaque-ID pass-through, selection-change notification, selection
reset on a new discovery snapshot, empty discovery, pending-state gating, and
Closed/Open/Streaming/Faulted action availability.

## CameraCommandPanel

camera::qtwidgets::CameraCommandPanel presents commands described by the
common ICameraCommandAccess contract. It is intended for no-argument commands;
parameterized operations remain in host-specific UI and must not be flattened
into this panel.

The host supplies command descriptors and an execute-request callback. A
button click moves the row to Request pending only. The host reports Executing
after the backend accepts the command issue, Completed only after authoritative
completion polling, and Failed with an error message on failure. The panel
does not call the backend, poll completion, or infer that a click means the
device operation has completed. Request-pending/executing commands cannot be
submitted twice; completed and failed commands may be retried.

## CameraCapabilityPanel

camera::qtwidgets::CameraCapabilityPanel renders the common model capability
set as a read-only table. Each capability is shown as Unknown, Unsupported or
Supported. Unknown is preserved explicitly and is never inferred to mean
Unsupported. The host supplies CameraCapabilities from the selected device
discovery result; the panel owns no provider or model lookup.

The AmScope preview integrates this panel into Camera Properties and updates
it when the selected discovery entry changes. Enumeration IDs are treated as
opaque handles and are not persisted across a new discovery snapshot.

## CameraFeatureEditor

camera::qtwidgets::CameraFeatureEditor presents one
CameraFeatureDescriptor using the matching typed editor. It supports
Boolean, exact signed 64-bit integer, integer vector, floating-point, string
and enumeration values. It does not own a camera, schedule reads/writes, or
assume that an editor's initial control value is the device's current value.

The host supplies current readback, pending state, error messages and a write
request callback. Readable features remain non-editable until a valid current
readback is supplied. Write-only controls begin unset; the widget does not
fabricate a false, zero or first-enum value. Descriptor availability,
writability, ranges, increments, vector cardinality and enum symbols are
validated before a typed write request is emitted. The host remains responsible
for executing the request and presenting confirmed readback.

A host may provide an enumeration display-name callback for user-friendly
labels. The canonical enumeration symbol remains the submitted value; display
labels do not alter backend identity or semantics.

The AmScope preview uses this widget in its descriptor-driven feature tabs.
Amcam-specific labels are supplied by the application host; the widget itself
has no Amcam SDK dependency.

The camera_qt_widgets_feature_editor_tests offscreen test covers readback
gating, typed writes, integer increments, discrete integer choices, Boolean
state, enum symbols and display labels, vector cardinality, floating-point
steps, UTF-8 strings, read-only/unavailable behavior, write-only inputs and
pending-state gating.

## ImageInfoBar

`camera::qtwidgets::ImageInfoBar` renders a horizontally scrollable list of
host-supplied imaging facts and values. Each item carries a stable ID, label,
value, optional unit and tooltip. Read-only items remain informational;
editable items report a double-click (and keyboard activation) to a
host-supplied callback.

The widget never queries a camera, interprets a feature descriptor, writes a
setting, or owns feature state. The host decides which values are available,
which are editable, and how to validate and execute an edit. This permits frame
metadata and camera-feature readbacks to share one compact information strip
without coupling the reusable widget to a particular camera backend. The AmScope
host uses compact 8–9 pt typography, subtle separators, and a 42–48 px widget
height so the strip remains useful without consuming preview area.

In the AmScope host, the bar is a permanent widget in the bottom
`QStatusBar`. This keeps frame and camera values visible while a separate
Output tool window holds timestamped application and camera messages. The
status area also shows the current camera lifecycle state. Docking and
application-status presentation belong to the host, not to the
backend-neutral widget contract.

The AmScope preview uses a document-centric workspace: Live Preview is the
central document tab; Camera Explorer and Camera Properties are separate
right-side tool windows; Output is a bottom tool window. The View menu exposes
the tool-window toggles and a Reset Window Layout command. File → Camera
Workspace raises the two camera panes. The main window persists geometry and
dock arrangements, and invalidates the older workspace-state version when
the layout model changes.

The `camera_qt_widgets_image_info_bar_tests` offscreen test covers item
creation/removal, double-click routing for editable items, and no edit callback
for read-only items.

## Build

The target is `Camera::QtWidgets`. It can be added as a CMake subdirectory
when `Camera::Common` and Qt 6 Widgets are available. The AmScope preview
build includes it when `AMSCOPE_BUILD_PREVIEW_APP=ON`.

The `camera_qt_widgets_frame_view_tests` test uses the Qt offscreen
platform and covers lease lifetime, RGB24/RAW8/RAW16 presentation, invalid
buffer rejection, and post-paint notification.

The offscreen UI tests are also registered for `CameraControlPanel` and
`ImageInfoBar`; they exercise presentation and callback contracts only, not
physical camera operations or feature readback.
