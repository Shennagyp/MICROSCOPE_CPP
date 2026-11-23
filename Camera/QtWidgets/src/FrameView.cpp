#include "camera/qtwidgets/FrameView.h"

#include <QColor>
#include <QPainter>
#include <QPaintEvent>

#include <cstdint>
#include <limits>
#include <utility>

namespace camera::qtwidgets {

namespace {

QString pixelFormatName(PixelFormat format) {
    switch (format) {
    case PixelFormat::Unknown: return QStringLiteral("Unknown");
    case PixelFormat::RGB24: return QStringLiteral("RGB24");
    case PixelFormat::Raw8: return QStringLiteral("Raw8");
    case PixelFormat::Raw16: return QStringLiteral("Raw16");
    }
    return QStringLiteral("Unknown");
}

} // namespace

FrameView::FrameView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(640, 480);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void FrameView::setMessage(QString message) {
    image_ = QImage{};
    frame_.reset();
    hasUnpaintedFrame_ = false;
    message_ = std::move(message);
    update();
}

void FrameView::setFramePresentedCallback(std::function<void()> callback) {
    framePresentedCallback_ = std::move(callback);
}

void FrameView::setFrame(FramePtr frame) {
    image_ = QImage{};
    frame_ = std::move(frame);
    message_.clear();

    const bool validDimensions =
        frame_ && frame_->data() && frame_->width > 0 && frame_->height > 0 &&
        frame_->width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
        frame_->height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
        frame_->strideBytes <= static_cast<std::uint32_t>(std::numeric_limits<int>::max());

    if (validDimensions) {
        std::uint64_t bytesPerPixel = 0;
        switch (frame_->pixelFormat) {
        case PixelFormat::RGB24: bytesPerPixel = 3; break;
        case PixelFormat::Raw8: bytesPerPixel = 1; break;
        case PixelFormat::Raw16: bytesPerPixel = 2; break;
        case PixelFormat::Unknown: break;
        }

        const std::uint64_t minimumRowBytes =
            static_cast<std::uint64_t>(frame_->width) * bytesPerPixel;
        const std::uint64_t requiredBytes =
            static_cast<std::uint64_t>(frame_->strideBytes) * frame_->height;
        const bool validBuffer =
            bytesPerPixel != 0 &&
            frame_->strideBytes >= minimumRowBytes &&
            requiredBytes <= frame_->byteSize();

        if (validBuffer) {
            const int width = static_cast<int>(frame_->width);
            const int height = static_cast<int>(frame_->height);
            const int stride = static_cast<int>(frame_->strideBytes);
            switch (frame_->pixelFormat) {
            case PixelFormat::RGB24:
                image_ = QImage(frame_->data(), width, height, stride,
                               QImage::Format_RGB888);
                break;
            case PixelFormat::Raw8:
                // Preview-only monochrome interpretation of sensor mosaic
                // samples. The retained RAW bytes remain unchanged.
                image_ = QImage(frame_->data(), width, height, stride,
                               QImage::Format_Grayscale8);
                break;
            case PixelFormat::Raw16:
                // The SDK uses native byte order on this platform. Do not
                // rewrite or down-convert the original 16-bit sample buffer.
                image_ = QImage(frame_->data(), width, height, stride,
                               QImage::Format_Grayscale16);
                break;
            case PixelFormat::Unknown:
                break;
            }
        }
    }

    if (image_.isNull()) {
        const QString frameDescription = frame_
            ? QStringLiteral("%1, %2x%3, stride: %4")
                  .arg(pixelFormatName(frame_->pixelFormat))
                  .arg(frame_->width)
                  .arg(frame_->height)
                  .arg(frame_->strideBytes)
            : QStringLiteral("invalid frame");
        frame_.reset();
        hasUnpaintedFrame_ = false;
        message_ = QStringLiteral("No renderable frame (%1)").arg(frameDescription);
    } else {
        // Count only a newly submitted frame that is actually painted. Device
        // sequence values are not uniqueness keys on all supported cameras.
        hasUnpaintedFrame_ = true;
    }
    update();
}

void FrameView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(23, 25, 29));

    if (image_.isNull()) {
        painter.setPen(QColor(214, 217, 223));
        painter.drawText(rect().adjusted(16, 16, -16, -16),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         message_);
        return;
    }

    QSize targetSize = image_.size();
    targetSize.scale(size(), Qt::KeepAspectRatio);
    const QRect targetRect(
        (width() - targetSize.width()) / 2,
        (height() - targetSize.height()) / 2,
        targetSize.width(),
        targetSize.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(targetRect, image_);

    if (hasUnpaintedFrame_) {
        hasUnpaintedFrame_ = false;
        if (framePresentedCallback_)
            framePresentedCallback_();
    }
}

} // namespace camera::qtwidgets
