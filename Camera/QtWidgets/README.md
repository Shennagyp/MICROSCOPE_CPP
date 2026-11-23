# Camera Qt Widgets

This target contains reusable Qt Widgets that depend only on the backend-neutral
`Camera::Common` contracts and Qt Widgets. It is separate from
`Camera::Qt`, which is the Qt Core frame-mailbox layer.

## Current component: FrameView

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

## Build

The target is `Camera::QtWidgets`. It can be added as a CMake subdirectory
when `Camera::Common` and Qt 6 Widgets are available. The AmScope preview
build includes it when `AMSCOPE_BUILD_PREVIEW_APP=ON`.

The `camera_qt_widgets_frame_view_tests` test uses the Qt offscreen
platform and covers lease lifetime, RGB24/RAW8/RAW16 presentation, invalid
buffer rejection, and post-paint notification.
