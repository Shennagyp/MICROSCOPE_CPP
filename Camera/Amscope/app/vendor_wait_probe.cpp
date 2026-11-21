#define AMCAM_HRESULT_ERRORCODE_NEEDED
#include "amcam.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string boundedString(const char* value, std::size_t capacity) {
    if (!value) return {};
    return std::string(value, std::find(value, value + capacity, '\0'));
}

void printUsage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " [--device-id ID] [--seconds N]\n"
              << "When exactly one camera is connected, --device-id may be omitted.\n";
}

bool allocateRgb24Buffer(int width, int height, std::vector<std::uint8_t>& buffer) {
    if (width <= 0 || height <= 0) return false;

    const std::uint64_t bitsPerRow = static_cast<std::uint64_t>(width) * 24U;
    const std::uint64_t rowBytes = ((bitsPerRow + 31U) & ~std::uint64_t(31U)) / 8U;
    const std::uint64_t totalBytes = rowBytes * static_cast<std::uint64_t>(height);

    if (totalBytes == 0 ||
        totalBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    try {
        buffer.resize(static_cast<std::size_t>(totalBytes));
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string requestedDeviceId;
    unsigned durationSeconds = 60;

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

    AmcamDeviceV2 devices[AMCAM_MAX]{};
    const unsigned deviceCount = std::min<unsigned>(Amcam_EnumV2(devices), AMCAM_MAX);
    std::cout << "Vendor SDK enumeration: " << deviceCount << " camera(s).\n";

    if (requestedDeviceId.empty()) {
        if (deviceCount != 1) {
            for (unsigned i = 0; i < deviceCount; ++i) {
                std::cout << "  [" << i << "] "
                          << boundedString(devices[i].displayname,
                                           sizeof(devices[i].displayname))
                          << " | id="
                          << boundedString(devices[i].id, sizeof(devices[i].id))
                          << "\n";
            }
            std::cerr << "Specify --device-id when zero or multiple cameras are detected.\n";
            return 64;
        }
        requestedDeviceId = boundedString(devices[0].id, sizeof(devices[0].id));
    } else {
        bool found = false;
        for (unsigned i = 0; i < deviceCount; ++i) {
            if (boundedString(devices[i].id, sizeof(devices[i].id)) == requestedDeviceId) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "The requested device id is not in the current enumeration.\n";
            return 64;
        }
    }

    HAmcam camera = Amcam_Open(requestedDeviceId.c_str());
    if (!camera) {
        std::cerr << "Amcam_Open failed for device id: " << requestedDeviceId << "\n";
        return 2;
    }

    int width = 0;
    int height = 0;
    HRESULT hr = Amcam_get_Size(camera, &width, &height);
    if (FAILED(hr)) {
        std::cerr << "Amcam_get_Size failed: 0x"
                  << std::hex << std::uppercase << static_cast<std::uint32_t>(hr)
                  << std::dec << "\n";
        Amcam_Close(camera);
        return 3;
    }

    std::vector<std::uint8_t> buffer;
    if (!allocateRgb24Buffer(width, height, buffer)) {
        std::cerr << "Could not allocate the vendor sample's RGB24 frame buffer.\n";
        Amcam_Close(camera);
        return 3;
    }

    std::cout << "Opened camera; final size=" << width << "x" << height
              << ", buffer_bytes=" << buffer.size() << "\n";

    // This is the vendor's demowait acquisition pairing:
    // NULL event callback + WaitImage polling + default (zero) row pitch.
    hr = Amcam_StartPullModeWithCallback(camera, nullptr, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Amcam_StartPullModeWithCallback(NULL) failed: 0x"
                  << std::hex << std::uppercase << static_cast<std::uint32_t>(hr)
                  << std::dec << "\n";
        Amcam_Close(camera);
        return 4;
    }

    std::cout << "Vendor-only polling started (no AmcamCamera, no Qt). "
              << "Sampling for " << durationSeconds << " seconds.\n"
              << std::flush;

    const auto startedAt = std::chrono::steady_clock::now();
    auto nextReport = startedAt + std::chrono::seconds(1);
    std::uint64_t totalFrames = 0;
    std::uint64_t totalTimeouts = 0;
    std::uint64_t intervalFrames = 0;
    std::uint64_t intervalTimeouts = 0;
    std::uint32_t firstSequence = 0;
    std::uint32_t lastSequence = 0;
    bool failed = false;
    HRESULT failureCode = S_OK;

    while (std::chrono::steady_clock::now() - startedAt <
           std::chrono::seconds(durationSeconds)) {
        AmcamFrameInfoV3 info{};
        hr = Amcam_WaitImageV3(camera, 1000, buffer.data(), 0, 24, 0, &info);

        if (hr == E_TIMEOUT) {
            ++totalTimeouts;
            ++intervalTimeouts;
        } else if (FAILED(hr)) {
            failed = true;
            failureCode = hr;
            std::cerr << "Amcam_WaitImageV3 failed: 0x"
                      << std::hex << std::uppercase << static_cast<std::uint32_t>(hr)
                      << std::dec << "\n";
            break;
        } else {
            if (totalFrames == 0) firstSequence = info.seq;
            lastSequence = info.seq;
            ++totalFrames;
            ++intervalFrames;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextReport) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - startedAt).count();
            std::cout << "t=" << elapsed
                      << "s frames=" << totalFrames
                      << " delta=" << intervalFrames
                      << " timeouts=" << totalTimeouts
                      << " timeout_delta=" << intervalTimeouts
                      << " seq=" << firstSequence << "->" << lastSequence
                      << "\n" << std::flush;
            intervalFrames = 0;
            intervalTimeouts = 0;
            nextReport += std::chrono::seconds(1);
        }
    }

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();

    const HRESULT stopResult = Amcam_Stop(camera);
    if (FAILED(stopResult)) {
        std::cerr << "Amcam_Stop after sample: 0x"
                  << std::hex << std::uppercase << static_cast<std::uint32_t>(stopResult)
                  << std::dec << "\n";
    }
    Amcam_Close(camera);

    std::cout << "RESULT duration_ms=" << elapsedMs
              << " frames=" << totalFrames
              << " timeouts=" << totalTimeouts
              << " failed=" << (failed ? "true" : "false");
    if (failed) {
        std::cout << " failure_code=0x" << std::hex << std::uppercase
                  << static_cast<std::uint32_t>(failureCode) << std::dec;
    }
    std::cout << "\n";

    return failed || totalFrames == 0 ? 5 : 0;
}
