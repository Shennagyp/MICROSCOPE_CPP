#include "amscope/AmcamCamera.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

const char* stateName(amscope::CameraState state) {
    switch (state) {
    case amscope::CameraState::Closed: return "Closed";
    case amscope::CameraState::Open: return "Open";
    case amscope::CameraState::Starting: return "Starting";
    case amscope::CameraState::Streaming: return "Streaming";
    case amscope::CameraState::Stopping: return "Stopping";
    case amscope::CameraState::Faulted: return "Faulted";
    }
    return "Unknown";
}

void printUsage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " [--device-id ID] [--seconds N]\n"
              << "When exactly one camera is connected, --device-id may be omitted.\n";
}

const char* pixelFormatName(amscope::PixelFormat format) {
    switch (format) {
    case amscope::PixelFormat::Unknown: return "Unknown";
    case amscope::PixelFormat::RGB24: return "RGB24";
    case amscope::PixelFormat::Raw8: return "Raw8";
    case amscope::PixelFormat::Raw16: return "Raw16";
    }
    return "Unknown";
}

struct FrameSnapshot {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool isStill = false;
    std::uint32_t strideBytes = 0;
    amscope::PixelFormat pixelFormat = amscope::PixelFormat::Unknown;
    std::optional<camera::PixelFormatIdentifier> pixelFormatIdentifier;
    std::uint8_t significantBitsPerPixel = 0;
    std::uint8_t storageBitsPerPixel = 0;
    std::size_t byteSize = 0;
    std::optional<std::uint64_t> sequenceNumber;
    std::optional<camera::FrameTimestamp> timestamp;
    std::optional<camera::FrameExposure> exposure;
};

void printFrameSnapshot(const FrameSnapshot& frame) {
    std::cout << "FRAME"
              << " width=" << frame.width
              << " height=" << frame.height
              << " is_still=" << (frame.isStill ? "true" : "false")
              << " stride_bytes=" << frame.strideBytes
              << " byte_size=" << frame.byteSize
              << " pixel_format=" << pixelFormatName(frame.pixelFormat)
              << " significant_bits=" << static_cast<unsigned>(frame.significantBitsPerPixel)
              << " storage_bits=" << static_cast<unsigned>(frame.storageBitsPerPixel);

    if (frame.pixelFormatIdentifier) {
        std::cout << " format_scheme=" << frame.pixelFormatIdentifier->scheme
                  << " format_value=0x" << std::hex
                  << frame.pixelFormatIdentifier->value << std::dec;
    } else {
        std::cout << " format_identifier=unavailable";
    }

    if (frame.sequenceNumber)
        std::cout << " sequence=" << *frame.sequenceNumber;
    else
        std::cout << " sequence=unavailable";

    if (frame.timestamp) {
        std::cout << " timestamp_ticks=" << frame.timestamp->ticks
                  << " timestamp_ticks_per_second="
                  << frame.timestamp->ticksPerSecond
                  << " clock_domain=";
        if (frame.timestamp->clockDomain)
            std::cout << *frame.timestamp->clockDomain;
        else
            std::cout << "unknown";
    } else {
        std::cout << " timestamp=unavailable";
    }

    if (frame.exposure) {
        std::cout << " exposure_ticks=" << frame.exposure->durationTicks
                  << " exposure_ticks_per_second="
                  << frame.exposure->ticksPerSecond;
    } else {
        std::cout << " exposure=unavailable";
    }
    std::cout << "\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string requestedDeviceId;
    unsigned durationSeconds = 30;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--device-id" && i + 1 < argc) {
            requestedDeviceId = argv[++i];
        } else if (argument == "--seconds" && i + 1 < argc) {
            try {
                const unsigned long parsed = std::stoul(argv[++i]);
                if (parsed < 5 || parsed > 3600) {
                    std::cerr << "--seconds must be between 5 and 3600.\n";
                    return 64;
                }
                durationSeconds = static_cast<unsigned>(parsed);
            } catch (...) {
                std::cerr << "Invalid value for --seconds.\n";
                return 64;
            }
        } else if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            printUsage(argv[0]);
            return 64;
        }
    }

    amscope::CameraError error;
    const std::vector<amscope::CameraInfo> devices =
        amscope::AmcamCamera::enumerate(&error);

    if (!error.message.empty()) {
        std::cerr << "ENUMERATION ERROR: " << error.message
                  << " [code=" << error.vendorCode << "]\n";
        return 1;
    }

    std::cout << "Detected " << devices.size() << " AmScope camera(s).\n";
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        std::cout << "  [" << i << "] "
                  << (device.displayName.empty() ? device.modelName : device.displayName)
                  << " | id=" << device.id << "\n";
    }

    if (requestedDeviceId.empty()) {
        if (devices.size() != 1) {
            std::cerr << "Specify --device-id when zero or multiple cameras are detected.\n";
            return 64;
        }
        requestedDeviceId = devices.front().id;
    }

    const auto selected = std::find_if(
        devices.begin(), devices.end(),
        [&requestedDeviceId](const amscope::CameraInfo& device) {
            return device.id == requestedDeviceId;
        });
    if (selected == devices.end()) {
        std::cerr << "The requested device id is not in the current enumeration.\n";
        return 64;
    }

    std::atomic<std::uint64_t> frameCount{0};
    std::mutex frameSnapshotMutex;
    std::optional<FrameSnapshot> firstFrame;
    std::atomic<bool> hasSequence{false};
    std::atomic<std::uint64_t> firstSequence{0};
    std::atomic<std::uint64_t> lastSequence{0};
    std::atomic<std::uint64_t> lastFrameTimeNs{0};
    std::mutex errorMutex;
    amscope::CameraError acquisitionError;
    std::atomic<bool> acquisitionFailed{false};

    amscope::AmcamCamera camera;
    if (!camera.open(requestedDeviceId, &error)) {
        std::cerr << "OPEN ERROR: " << error.message
                  << " [code=" << error.vendorCode << "]\n";
        return 2;
    }

    const auto runStarted = std::chrono::steady_clock::now();
    const bool started = camera.start(
        [&frameCount, &frameSnapshotMutex, &firstFrame,
         &hasSequence, &firstSequence, &lastSequence, &lastFrameTimeNs](
            amscope::FramePtr frame) {
            if (!frame) return;
            frameCount.fetch_add(1, std::memory_order_relaxed);

            {
                std::lock_guard<std::mutex> lock(frameSnapshotMutex);
                if (!firstFrame) {
                    firstFrame = FrameSnapshot{
                        frame->width,
                        frame->height,
                        frame->isStill,
                        frame->strideBytes,
                        frame->pixelFormat,
                        frame->pixelFormatIdentifier,
                        frame->significantBitsPerPixel,
                        frame->storageBitsPerPixel,
                        frame->byteSize(),
                        frame->sequenceNumber,
                        frame->timestamp,
                        frame->exposure
                    };
                }
            }
            if (frame->sequenceNumber) {
                const auto sequence = *frame->sequenceNumber;
                if (!hasSequence.load(std::memory_order_relaxed))
                    firstSequence.store(sequence, std::memory_order_relaxed);
                lastSequence.store(sequence, std::memory_order_relaxed);
                hasSequence.store(true, std::memory_order_release);
            }
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            lastFrameTimeNs.store(
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()),
                std::memory_order_relaxed);
        },
        [&errorMutex, &acquisitionError, &acquisitionFailed](
            const amscope::CameraError& callbackError) {
            {
                std::lock_guard<std::mutex> lock(errorMutex);
                acquisitionError = callbackError;
            }
            acquisitionFailed.store(true, std::memory_order_release);
        },
        &error);

    if (!started) {
        std::cerr << "START ERROR: " << error.message
                  << " [code=" << error.vendorCode << "]\n";
        (void)camera.close();
        return 3;
    }

    std::cout << "Native-only acquisition started; Qt is not linked into this probe.\n"
              << "Sampling for " << durationSeconds << " seconds.\n"
              << std::flush;

    std::uint64_t previousCount = 0;
    bool faulted = false;
    bool stalled = false;
    while (std::chrono::steady_clock::now() - runStarted <
           std::chrono::seconds(durationSeconds)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        const std::uint64_t currentCount =
            frameCount.load(std::memory_order_relaxed);
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - runStarted).count();

        std::cout << "t=" << elapsed
                  << "s frames=" << currentCount
                  << " delta=" << (currentCount - previousCount)
                  << " seq=";
        if (hasSequence.load(std::memory_order_acquire)) {
            std::cout << firstSequence.load(std::memory_order_relaxed)
                      << "->" << lastSequence.load(std::memory_order_relaxed);
        } else {
            std::cout << "unavailable";
        }
        std::cout << " dropped=" << camera.droppedFrames()
                  << " state=" << stateName(camera.state())
                  << "\n" << std::flush;

        previousCount = currentCount;
        if (camera.state() == amscope::CameraState::Faulted ||
            acquisitionFailed.load(std::memory_order_acquire)) {
            faulted = true;
            break;
        }

        const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const std::uint64_t lastNs =
            lastFrameTimeNs.load(std::memory_order_relaxed);
        const auto frameAgeNs = lastNs == 0
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - runStarted).count()
            : nowNs - static_cast<std::int64_t>(lastNs);
        if (frameAgeNs >= 5000000000LL) {
            stalled = true;
            std::cerr << "NATIVE FRAME STALL: no frame has arrived for 5 seconds.\n";
            break;
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - runStarted).count();
    const std::uint64_t totalFrames = frameCount.load(std::memory_order_relaxed);
    const std::uint64_t lastFrameNs =
        lastFrameTimeNs.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(frameSnapshotMutex);
        if (firstFrame)
            printFrameSnapshot(*firstFrame);
        else
            std::cout << "FRAME metadata=unavailable(no frame received)\\n";
    }

    std::cout << "RESULT duration_ms=" << elapsed
              << " frames=" << totalFrames
              << " dropped=" << camera.droppedFrames()
              << " state_before_stop=" << stateName(camera.state());

    if (lastFrameNs != 0) {
        const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        std::cout << " last_frame_age_ms=" << (nowNs - lastFrameNs) / 1000000;
    }
    std::cout << "\n";

    if (acquisitionFailed.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(errorMutex);
        std::cerr << "ACQUISITION ERROR: " << acquisitionError.operation
                  << ": " << acquisitionError.message
                  << " [code=" << acquisitionError.vendorCode << "]\n";
    }

    amscope::CameraError stopError;
    const bool stopSucceeded = camera.stop(&stopError);
    if (!stopSucceeded && !stopError.message.empty()) {
        std::cerr << "STOP ERROR: " << stopError.message
                  << " [code=" << stopError.vendorCode << "]\n";
    }

    amscope::CameraError closeError;
    const bool closeSucceeded = camera.close(&closeError);
    if (!closeSucceeded && !closeError.message.empty()) {
        std::cerr << "CLOSE ERROR: " << closeError.message
                  << " [code=" << closeError.vendorCode << "]\n";
    }

    if (faulted || stalled || totalFrames == 0 ||
        elapsed < durationSeconds * 1000LL ||
        !stopSucceeded || !closeSucceeded)
        return 4;

    return 0;
}
