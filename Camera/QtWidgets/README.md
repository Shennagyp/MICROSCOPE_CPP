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
and session lifetime. A faulted session remains closeable but cannot be started
or stopped through this panel.

The `camera_qt_widgets_camera_control_panel_tests` offscreen test covers
device-label/opaque-ID preservation, callback dispatch, empty discovery,
pending-state gating, and Closed/Open/Streaming/Faulted action availability.

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
without coupling the reusable widget to a particular camera backend.

In the AmScope preview host, the bar is placed in a movable, floatable, and
closable `QDockWidget`, initially docked at the bottom. The View menu can
show it again after closing it. Transient application messages are represented
as items in that same bar; the host does not create a separate `QStatusBar`.
Docking and application-status presentation belong to the host, not to the
backend-neutral widget contract.

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
