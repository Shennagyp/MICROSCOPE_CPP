#include "camera/qt/FrameStream.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace {

::camera::FramePtr makeFrame(std::uint32_t value) {
    auto frame = std::make_shared<::camera::Frame>();
    frame->width = value;
    frame->height = 1;
    frame->strideBytes = 1;
    frame->pixelFormat = ::camera::PixelFormat::Raw8;
    frame->significantBitsPerPixel = 8;
    frame->storageBitsPerPixel = 8;
    frame->pixels.push_back(static_cast<std::uint8_t>(value & 0xffU));
    return frame;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    camera::qt::FrameStream stream;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    int notifications = 0;
    bool affinityValid = true;
    bool latestFrameValid = false;
    bool rearmedFrameValid = false;
    bool clearedFrameAbsent = false;

    QObject::connect(
        &stream,
        &camera::qt::FrameStream::frameAvailable,
        &application,
        [&]() {
            affinityValid = affinityValid &&
                QThread::currentThread() == stream.thread();
            const auto frame = stream.takeLatestFrame();
            ++notifications;

            if (notifications == 1) {
                latestFrameValid = frame && frame->width == 500U &&
                    frame->pixels.size() == 1U;
                auto nextFrame = makeFrame(501);
                stream.publishFrame(
                    std::move(nextFrame),
                    []() noexcept { return true; });
            } else if (notifications == 2) {
                rearmedFrameValid = frame && frame->width == 501U &&
                    frame->pixels.size() == 1U;
                loop.quit();
            } else if (notifications == 3) {
                clearedFrameAbsent = !frame;
                loop.quit();
            }
        });

    std::thread producer([&]() {
        for (std::uint32_t value = 1; value <= 500; ++value) {
            auto frame = makeFrame(value);
            stream.publishFrame(
                std::move(frame),
                []() noexcept { return true; });
        }
    });
    producer.join();

    timeout.start(3000);
    loop.exec();

    if (notifications != 2 || !affinityValid ||
        !latestFrameValid || !rearmedFrameValid) {
        std::cerr << "Qt frame stream lost the latest lease, failed to re-arm, "
                     "or violated QObject affinity.\n";
        return EXIT_FAILURE;
    }

    auto frameToClear = makeFrame(600);
    stream.publishFrame(
        std::move(frameToClear),
        []() noexcept { return true; });
    stream.clear();

    timeout.start(3000);
    loop.exec();

    if (notifications != 3 || !clearedFrameAbsent || !affinityValid) {
        std::cerr << "Qt frame stream clear did not remove the retained lease.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Qt frame stream handoff tests passed.\n";
    return EXIT_SUCCESS;
}
