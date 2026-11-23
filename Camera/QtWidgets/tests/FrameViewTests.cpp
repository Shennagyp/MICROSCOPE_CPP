#include "camera/qtwidgets/FrameView.h"

#include <QApplication>
#include <QColor>
#include <QImage>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <utility>

namespace {

camera::FramePtr makeRgbFrame() {
    auto frame = std::make_shared<camera::Frame>();
    frame->width = 2;
    frame->height = 1;
    frame->strideBytes = 6;
    frame->pixelFormat = camera::PixelFormat::RGB24;
    frame->significantBitsPerPixel = 24;
    frame->storageBitsPerPixel = 24;
    frame->pixels = {255, 0, 0, 0, 255, 0};
    return frame;
}

camera::FramePtr makeRaw8Frame() {
    auto frame = std::make_shared<camera::Frame>();
    frame->width = 2;
    frame->height = 1;
    frame->strideBytes = 2;
    frame->pixelFormat = camera::PixelFormat::Raw8;
    frame->significantBitsPerPixel = 8;
    frame->storageBitsPerPixel = 8;
    frame->pixels = {11, 240};
    return frame;
}

camera::FramePtr makeRaw16Frame() {
    auto frame = std::make_shared<camera::Frame>();
    frame->width = 2;
    frame->height = 1;
    frame->strideBytes = 4;
    frame->pixelFormat = camera::PixelFormat::Raw16;
    frame->significantBitsPerPixel = 12;
    frame->storageBitsPerPixel = 16;
    frame->pixels.resize(4);
    const std::uint16_t samples[] = {0x1100, 0xEE00};
    std::memcpy(frame->pixels.data(), samples, sizeof(samples));
    return frame;
}

void fail(const char* message) {
    std::cerr << message << '\n';
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    camera::qtwidgets::FrameView view;
    view.resize(640, 480);

    int presentedCount = 0;
    view.setFramePresentedCallback([&presentedCount]() { ++presentedCount; });

    auto rgbFrame = makeRgbFrame();
    std::weak_ptr<const camera::Frame> rgbLease = rgbFrame;
    view.setFrame(rgbFrame);
    rgbFrame.reset();

    if (rgbLease.expired()) {
        fail("FrameView did not retain the immutable frame lease.");
        return EXIT_FAILURE;
    }

    view.show();
    application.processEvents();

    QImage rendered(view.size(), QImage::Format_RGB32);
    rendered.fill(Qt::black);
    view.render(&rendered);

    if (presentedCount != 1) {
        fail("A valid RGB frame was not counted after paint.");
        return EXIT_FAILURE;
    }
    if (rendered.pixelColor(100, 160) != QColor(255, 0, 0) ||
        rendered.pixelColor(500, 160) != QColor(0, 255, 0)) {
        fail("RGB24 pixels were not rendered from the retained frame buffer.");
        return EXIT_FAILURE;
    }

    view.setMessage(QStringLiteral("No camera selected."));
    if (!rgbLease.expired()) {
        fail("FrameView retained the previous lease after clearing the image.");
        return EXIT_FAILURE;
    }

    presentedCount = 0;
    view.setFrame(makeRaw8Frame());
    rendered.fill(Qt::black);
    view.render(&rendered);

    if (presentedCount != 1) {
        fail("A valid RAW8 frame was not counted after paint.");
        return EXIT_FAILURE;
    }
    if (rendered.pixelColor(100, 160).red() != 11 ||
        rendered.pixelColor(500, 160).red() != 240) {
        fail("RAW8 samples were not presented as grayscale.");
        return EXIT_FAILURE;
    }

    presentedCount = 0;
    view.setFrame(makeRaw16Frame());
    rendered.fill(Qt::black);
    view.render(&rendered);
    if (presentedCount != 1) {
        fail("A valid RAW16 frame was not counted after paint.");
        return EXIT_FAILURE;
    }

    auto invalid = std::make_shared<camera::Frame>();
    invalid->width = 2;
    invalid->height = 1;
    invalid->strideBytes = 1;
    invalid->pixelFormat = camera::PixelFormat::RGB24;
    invalid->pixels = {1, 2, 3};
    view.setFrame(std::move(invalid));
    rendered.fill(Qt::black);
    view.render(&rendered);

    if (presentedCount != 1) {
        fail("An invalid/truncated frame was incorrectly counted as presented.");
        return EXIT_FAILURE;
    }

    std::cout << "All Qt Widgets frame view tests passed.\n";
    return EXIT_SUCCESS;
}
