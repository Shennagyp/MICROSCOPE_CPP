#include "camera/FrameStreamMailbox.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

camera::FramePtr makeFrame(std::uint32_t value) {
    auto frame = std::make_shared<camera::Frame>();
    frame->width = value;
    frame->height = 1;
    frame->strideBytes = 1;
    frame->pixelFormat = camera::PixelFormat::Raw8;
    frame->significantBitsPerPixel = 8;
    frame->storageBitsPerPixel = 8;
    frame->pixels.push_back(static_cast<std::uint8_t>(value));
    return frame;
}

} // namespace

int main() {
    camera::FrameStreamMailbox mailbox;
    int notifications = 0;

    // A slow consumer receives only the newest frame; notifications coalesce.
    mailbox.publishFrame(makeFrame(1), []() noexcept { return true; },
                         [&]() noexcept { ++notifications; });
    mailbox.publishFrame(makeFrame(2), []() noexcept { return true; },
                         [&]() noexcept { ++notifications; });
    mailbox.publishFrame(makeFrame(3), []() noexcept { return true; },
                         [&]() noexcept { ++notifications; });

    require(notifications == 1,
            "Multiple publications coalesce behind one pending notification");

    camera::FramePtr latest = mailbox.takeLatestFrame(
        [&]() noexcept { ++notifications; });
    require(latest && latest->width == 3 && latest->pixels.front() == 3,
            "Consumer receives the latest published frame");
    require(notifications == 1,
            "Taking an empty mailbox does not create another notification");

    // A publication after consumption creates a new notification cycle.
    mailbox.publishFrame(makeFrame(4), []() noexcept { return true; },
                         [&]() noexcept { ++notifications; });
    require(notifications == 2,
            "A new publication after consume rearms notification");

    latest = mailbox.takeLatestFrame([]() noexcept {});
    require(latest && latest->width == 4,
            "Rearmed notification exposes the next frame");

    // A generation change between the two checks removes only the stale frame
    // and must not signal the UI.
    int generationChecks = 0;
    mailbox.publishFrame(makeFrame(5), [&]() noexcept {
        ++generationChecks;
        return generationChecks == 1;
    }, [&]() noexcept { ++notifications; });
    require(generationChecks == 2,
            "Publication revalidates the session after storing the frame");
    require(!mailbox.takeLatestFrame([]() noexcept {}),
            "A superseded generation cannot leave a frame in the mailbox");
    require(notifications == 2,
            "A rejected stale publication does not signal the UI");

    // Clear releases a pending lease and resets the notification latch.
    std::weak_ptr<const camera::Frame> weakFrame;
    {
        auto frame = makeFrame(6);
        weakFrame = frame;
        mailbox.publishFrame(std::move(frame), []() noexcept { return true; },
                             [&]() noexcept { ++notifications; });
    }
    require(!weakFrame.expired(),
            "The mailbox retains the published immutable frame lease");
    mailbox.clear();
    require(weakFrame.expired(),
            "Clearing the mailbox releases the retained frame lease");
    require(!mailbox.takeLatestFrame([]() noexcept {}),
            "A cleared mailbox returns no frame");

    std::cout << "All common frame mailbox tests passed.\n";
    return EXIT_SUCCESS;
}
