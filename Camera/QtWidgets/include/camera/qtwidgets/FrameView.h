#pragma once

#include "camera/CameraFrames.h"

#include <QImage>
#include <QString>
#include <QWidget>

#include <functional>

namespace camera::qtwidgets {

// Displays common camera frame layouts without taking ownership of a second
// pixel buffer. The immutable FramePtr lease remains alive while QImage reads
// its externally owned pixel memory. The frame-presented callback fires after
// a newly submitted valid frame has actually been painted.
class FrameView final : public QWidget {
public:
    explicit FrameView(QWidget* parent = nullptr);

    void setMessage(QString message);
    void setFramePresentedCallback(std::function<void()> callback);
    void setFrame(FramePtr frame);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // frame_ owns the external pixel storage referenced by image_.
    FramePtr frame_;
    QImage image_;
    QString message_ = QStringLiteral("Select a camera and press Open, then Start.");
    std::function<void()> framePresentedCallback_;
    bool hasUnpaintedFrame_ = false;
};

} // namespace camera::qtwidgets
