#include "amcam.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::string boundedString(const char* value, std::size_t capacity) {
    if (!value) return {};
    const char* end = value;
    while (end != value + capacity && *end != '\0') ++end;
    return std::string(value, end);
}

void printHresult(const char* operation, HRESULT result) {
    const auto signedResult = static_cast<std::int32_t>(result);
    const auto unsignedResult = static_cast<std::uint32_t>(signedResult);
    std::cout << operation
              << ": HRESULT decimal=" << signedResult
              << ", hex=0x" << std::hex << std::uppercase
              << std::setw(8) << std::setfill('0') << unsignedResult
              << std::dec << std::nouppercase << std::setfill(' ')
              << ", " << (SUCCEEDED(result) ? "success" : "failure")
              << '\n';
}

std::string fourccString(unsigned value) {
    std::string result;
    result.reserve(4);
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const auto character =
            static_cast<unsigned char>((value >> shift) & 0xffU);
        result.push_back(std::isprint(character)
            ? static_cast<char>(character)
            : '.');
    }
    return result;
}

void printRawModelFlags(std::uint64_t flags) {
    struct FlagName {
        const char* name;
        std::uint64_t mask;
    };

    const FlagName rawFlags[] = {
        {"RAW8", static_cast<std::uint64_t>(AMCAM_FLAG_RAW8)},
        {"RAW10", static_cast<std::uint64_t>(AMCAM_FLAG_RAW10)},
        {"RAW11", static_cast<std::uint64_t>(AMCAM_FLAG_RAW11)},
        {"RAW12", static_cast<std::uint64_t>(AMCAM_FLAG_RAW12)},
        {"RAW12PACK", static_cast<std::uint64_t>(AMCAM_FLAG_RAW12PACK)},
        {"RAW14", static_cast<std::uint64_t>(AMCAM_FLAG_RAW14)},
        {"RAW16", static_cast<std::uint64_t>(AMCAM_FLAG_RAW16)},
        {"GMCY8", static_cast<std::uint64_t>(AMCAM_FLAG_GMCY8)},
        {"GMCY12", static_cast<std::uint64_t>(AMCAM_FLAG_GMCY12)},
    };

    std::cout << "RAW-related model flags:";
    bool any = false;
    for (const auto& flag : rawFlags) {
        if ((flags & flag.mask) != 0) {
            std::cout << (any ? ", " : " ") << flag.name;
            any = true;
        }
    }
    if (!any) std::cout << " none";
    std::cout << '\n';

    std::cout << "SDK model transport classification (not a live link measurement): ";
    if ((flags & AMCAM_FLAG_USB30_OVER_USB20) != 0) {
        std::cout << "AMCAM_FLAG_USB30_OVER_USB20 is set; the SDK model "
                     "descriptor classifies this USB 3.0 camera as attached "
                     "through USB 2.0\n";
    } else if ((flags & AMCAM_FLAG_USB30) != 0) {
        std::cout << "AMCAM_FLAG_USB30 is set\n";
    } else {
        std::cout << "no USB 3.0 model flag is reported\n";
    }
}

bool parseIndex(const char* text, unsigned& index) {
    if (!text || *text == '\0') return false;
    const char* end = text;
    while (*end != '\0') ++end;
    const auto parsed = std::from_chars(text, end, index);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

void printDeviceList(const AmcamDeviceV2* devices, unsigned count) {
    std::cout << "Enumerated cameras: " << count << '\n';
    for (unsigned i = 0; i < count; ++i) {
        const auto& device = devices[i];
        std::cout << "\n[" << i << "] SDK display name: "
                  << boundedString(device.displayname, sizeof(device.displayname))
                  << "\n    camera id: "
                  << boundedString(device.id, sizeof(device.id));

        if (!device.model) {
            std::cout << "\n    model descriptor: unavailable\n";
            continue;
        }

        const auto modelName = device.model->name
            ? std::string(device.model->name)
            : std::string("<unnamed>");
        std::cout << "\n    model: " << modelName
                  << "\n    model flags decimal: " << device.model->flag
                  << "\n    model flags hex: 0x"
                  << std::hex << std::uppercase << device.model->flag
                  << std::dec << std::nouppercase << '\n';
        printRawModelFlags(device.model->flag);
    }
}

void printOptionInt(HAmcam camera, const char* label, unsigned option) {
    int value = std::numeric_limits<int>::min();
    const HRESULT result = Amcam_get_Option(camera, option, &value);
    printHresult(label, result);
    if (SUCCEEDED(result)) std::cout << "    value: " << value << '\n';
}

void printRgbOutputMode(HAmcam camera) {
    int value = std::numeric_limits<int>::min();
    const HRESULT result = Amcam_get_Option(
        camera, AMCAM_OPTION_RGB, &value);
    printHresult("Amcam_get_Option(AMCAM_OPTION_RGB)", result);
    if (FAILED(result)) return;

    const char* name = nullptr;
    switch (value) {
    case 0: name = "RGB24"; break;
    case 1: name = "RGB48 (requires camera bit depth > 8)"; break;
    case 2: name = "RGB32"; break;
    case 3: name = "Grey8 (monochrome cameras only)"; break;
    case 4: name = "Grey16 (monochrome cameras with bit depth > 8 only)"; break;
    case 5: name = "RGB64 (requires camera bit depth > 8)"; break;
    default: name = "unknown SDK value"; break;
    }
    std::cout << "    processed-output mode: " << name
              << " (value " << value << ')' << '\n';
}


struct RawFrameRecord {
    AmcamFrameInfoV4 info{};
    std::uint64_t byteCount = 0;
    unsigned minimum = 0;
    unsigned maximum = 0;
    std::uint64_t distinctValues = 0;
    std::uint64_t sentinelBytes = 0;
    std::uint64_t checksum = 0;
};

struct RawFrameCapture {
    static constexpr std::uint8_t sentinel = 0xA5;
    HAmcam camera = nullptr;
    std::vector<std::uint8_t> buffer;
    std::vector<std::uint8_t> firstFrame;
    std::vector<RawFrameRecord> records;
    std::mutex mutex;
    std::condition_variable completedCondition;
    unsigned targetFrameCount = 1;
    unsigned bitsPerPixel = 0;
    unsigned imageEvents = 0;
    unsigned errorEvents = 0;
    unsigned disconnectedEvents = 0;
    unsigned timeoutEvents = 0;
    unsigned terminalEvent = 0;
    std::map<unsigned, unsigned> eventCounts;
    HRESULT lastPullResult = 0;
    bool completed = false;
    bool invalidFrameLayout = false;
};

void __stdcall rawFrameEventCallback(unsigned event, void* context) {
    if (!context) return;
    auto& capture = *static_cast<RawFrameCapture*>(context);

    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        ++capture.eventCounts[event];
    }

    if (event != AMCAM_EVENT_IMAGE) {
        if (event != AMCAM_EVENT_ERROR &&
            event != AMCAM_EVENT_DISCONNECTED &&
            event != AMCAM_EVENT_NOFRAMETIMEOUT &&
            event != AMCAM_EVENT_NOPACKETTIMEOUT) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(capture.mutex);
            if (event == AMCAM_EVENT_ERROR) ++capture.errorEvents;
            if (event == AMCAM_EVENT_DISCONNECTED) ++capture.disconnectedEvents;
            if (event == AMCAM_EVENT_NOFRAMETIMEOUT ||
                event == AMCAM_EVENT_NOPACKETTIMEOUT) ++capture.timeoutEvents;
            if (!capture.completed) {
                capture.terminalEvent = event;
                capture.completed = true;
            }
        }
        capture.completedCondition.notify_one();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        ++capture.imageEvents;
        if (capture.completed ||
            capture.records.size() >= capture.targetFrameCount) {
            return;
        }

        std::fill(capture.buffer.begin(), capture.buffer.end(),
                  RawFrameCapture::sentinel);

        AmcamFrameInfoV4 info{};
        const HRESULT result = Amcam_PullImageV4(
            capture.camera, capture.buffer.data(), 0, 0, -1, &info);
        capture.lastPullResult = result;
        if (FAILED(result)) {
            capture.completed = true;
        } else {
            const unsigned frameWidth = info.v3.width;
            const unsigned frameHeight = info.v3.height;
            const std::uint64_t bytesPerPixel =
                capture.bitsPerPixel > 8 ? 2U : 1U;
            const std::uint64_t byteCount =
                static_cast<std::uint64_t>(frameWidth) *
                static_cast<std::uint64_t>(frameHeight) * bytesPerPixel;

            if (frameWidth == 0 || frameHeight == 0 ||
                byteCount == 0 || byteCount > capture.buffer.size()) {
                capture.invalidFrameLayout = true;
                capture.completed = true;
            } else {
                RawFrameRecord record;
                record.info = info;
                record.byteCount = byteCount;
                record.minimum = capture.bitsPerPixel > 8 ? 65535U : 255U;
                record.maximum = 0;
                std::uint64_t hash = 14695981039346656037ULL;

                if (capture.bitsPerPixel <= 8) {
                    std::array<bool, 256> seen{};
                    for (std::uint64_t i = 0; i < byteCount; ++i) {
                        const std::uint8_t byte =
                            capture.buffer[static_cast<std::size_t>(i)];
                        record.minimum = std::min(
                            record.minimum, static_cast<unsigned>(byte));
                        record.maximum = std::max(
                            record.maximum, static_cast<unsigned>(byte));
                        if (!seen[byte]) {
                            seen[byte] = true;
                            ++record.distinctValues;
                        }
                        if (byte == RawFrameCapture::sentinel) {
                            ++record.sentinelBytes;
                        }
                        hash ^= byte;
                        hash *= 1099511628211ULL;
                    }
                } else {
                    std::array<bool, 65536> seen{};
                    for (std::uint64_t i = 0; i < byteCount; i += 2) {
                        const unsigned low =
                            capture.buffer[static_cast<std::size_t>(i)];
                        const unsigned high =
                            capture.buffer[static_cast<std::size_t>(i + 1)];
                        const unsigned sample = low | (high << 8U);
                        record.minimum = std::min(record.minimum, sample);
                        record.maximum = std::max(record.maximum, sample);
                        if (!seen[sample]) {
                            seen[sample] = true;
                            ++record.distinctValues;
                        }
                        if (low == RawFrameCapture::sentinel) {
                            ++record.sentinelBytes;
                        }
                        if (high == RawFrameCapture::sentinel) {
                            ++record.sentinelBytes;
                        }
                        hash ^= low;
                        hash *= 1099511628211ULL;
                        hash ^= high;
                        hash *= 1099511628211ULL;
                    }
                }

                record.checksum = hash;
                if (capture.records.empty()) {
                    capture.firstFrame.assign(
                        capture.buffer.begin(),
                        capture.buffer.begin() +
                            static_cast<std::ptrdiff_t>(byteCount));
                }
                capture.records.push_back(record);
                if (capture.records.size() >= capture.targetFrameCount) {
                    capture.completed = true;
                }
            }
        }
    }
    capture.completedCondition.notify_one();
}

bool captureRawFrames(HAmcam camera, unsigned targetFrameCount) {
    constexpr const char* outputPath = "amscope-raw-frame.raw";
    if (std::remove(outputPath) == 0) {
        std::cout << "Removed previous capture artifact before acquisition: "
                  << outputPath << '\n';
    }

    std::cout << "\n========== RAW ACQUISITION VALIDITY TEST ==========\n"
              << "Requested consecutive frames: " << targetFrameCount << '\n';
    int originalRawMode = -1;
    HRESULT hr = Amcam_get_Option(camera, AMCAM_OPTION_RAW, &originalRawMode);
    printHresult("Read original RAW mode", hr);
    if (FAILED(hr) || (originalRawMode != 0 && originalRawMode != 1)) {
        std::cerr << "Cannot safely capture: original RAW mode is unavailable or invalid.\n";
        return false;
    }

    bool rawModeChanged = false;
    bool success = false;
    bool acquisitionStarted = false;
    const auto restoreRawMode = [&]() -> bool {
        if (!rawModeChanged) return true;
        const HRESULT restoreResult =
            Amcam_put_Option(camera, AMCAM_OPTION_RAW, originalRawMode);
        printHresult("Restore original RAW mode after capture", restoreResult);
        if (FAILED(restoreResult)) {
            std::cerr << "CRITICAL: RAW-mode restoration failed. Reconnect or power-cycle the camera before further use.\n";
            return false;
        }
        int restored = -1;
        const HRESULT verifyResult =
            Amcam_get_Option(camera, AMCAM_OPTION_RAW, &restored);
        printHresult("Verify restored RAW mode", verifyResult);
        if (FAILED(verifyResult) || restored != originalRawMode) {
            std::cerr << "CRITICAL: RAW-mode restoration readback mismatch.\n";
            return false;
        }
        return true;
    };

    if (originalRawMode == 0) {
        hr = Amcam_put_Option(camera, AMCAM_OPTION_RAW, 1);
        printHresult("Enable RAW mode for acquisition", hr);
        if (FAILED(hr)) return false;
        rawModeChanged = true;
    }

    int activeRawMode = -1;
    hr = Amcam_get_Option(camera, AMCAM_OPTION_RAW, &activeRawMode);
    printHresult("Verify RAW mode before acquisition", hr);
    if (FAILED(hr) || activeRawMode != 1) {
        std::cerr << "RAW mode was not confirmed; acquisition aborted.\n";
        restoreRawMode();
        return false;
    }

    unsigned fourCC = 0;
    unsigned bitsPerPixel = 0;
    hr = Amcam_get_RawFormat(camera, &fourCC, &bitsPerPixel);
    printHresult("Read active RAW format", hr);
    if (FAILED(hr) || bitsPerPixel == 0 || bitsPerPixel > 16) {
        std::cerr << "The SDK did not return a valid RAW sample depth; acquisition aborted.\n";
        restoreRawMode();
        return false;
    }
    std::cout << "    format: " << fourccString(fourCC)
              << ", significant bits per pixel: " << bitsPerPixel << '\n';

    int width = 0, height = 0;
    hr = Amcam_get_FinalSize(camera, &width, &height);
    printHresult("Amcam_get_FinalSize before RAW acquisition", hr);
    if (FAILED(hr) || width <= 0 || height <= 0) {
        std::cerr << "Invalid acquisition dimensions; RAW capture aborted.\n";
        restoreRawMode();
        return false;
    }

    const std::uint64_t capacity64 =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) * 2U;
    if (capacity64 == 0 ||
        capacity64 > 512ULL * 1024ULL * 1024ULL ||
        capacity64 > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "The RAW capture buffer size is invalid or exceeds the 512 MiB safety limit.\n";
        restoreRawMode();
        return false;
    }

    RawFrameCapture capture;
    capture.camera = camera;
    capture.targetFrameCount = targetFrameCount;
    capture.bitsPerPixel = bitsPerPixel;
    try {
        capture.buffer.resize(static_cast<std::size_t>(capacity64));
        capture.records.reserve(targetFrameCount);
    } catch (const std::exception& exception) {
        std::cerr << "RAW buffer allocation failed: " << exception.what() << '\n';
        restoreRawMode();
        return false;
    }

    int triggerMode = -1;
    const HRESULT triggerModeResult =
        Amcam_get_Option(camera, AMCAM_OPTION_TRIGGER, &triggerMode);
    printHresult("Read trigger mode before acquisition", triggerModeResult);
    if (SUCCEEDED(triggerModeResult)) {
        const char* triggerName = triggerMode == 0 ? "video" :
            triggerMode == 1 ? "software/simulated trigger" :
            triggerMode == 2 ? "external trigger" :
            triggerMode == 3 ? "external + software trigger" : "unknown";
        std::cout << "    trigger mode: " << triggerMode
                  << " (" << triggerName << ")\n";
    }

    unsigned configuredExposureUs = 0;
    const HRESULT configuredExposureResult =
        Amcam_get_ExpoTime(camera, &configuredExposureUs);
    printHresult("Read configured exposure time", configuredExposureResult);
    if (SUCCEEDED(configuredExposureResult)) {
        std::cout << "    configured exposure: " << configuredExposureUs
                  << " us\n";
    }
    unsigned realExposureUs = 0;
    const HRESULT realExposureResult =
        Amcam_get_RealExpoTime(camera, &realExposureUs);
    printHresult("Read actual exposure time", realExposureResult);
    if (SUCCEEDED(realExposureResult)) {
        std::cout << "    actual exposure: " << realExposureUs << " us\n";
    }

    int callbackThread = -1;
    const HRESULT callbackThreadResult = Amcam_get_Option(
        camera, AMCAM_OPTION_CALLBACK_THREAD, &callbackThread);
    printHresult("Read callback-thread option", callbackThreadResult);
    if (SUCCEEDED(callbackThreadResult)) {
        std::cout << "    dedicated callback thread: "
                  << callbackThread << '\n';
    }

    int droppedFramesBefore = -1;
    const HRESULT droppedBeforeResult = Amcam_get_Option(
        camera, AMCAM_OPTION_NUMBER_DROP_FRAME, &droppedFramesBefore);
    printHresult("Read software-dropped frame counter before acquisition",
                 droppedBeforeResult);
    if (SUCCEEDED(droppedBeforeResult)) {
        std::cout << "    counter before: " << droppedFramesBefore << '\n';
    }

    hr = Amcam_StartPullModeWithCallback(camera, rawFrameEventCallback, &capture);
    printHresult("Amcam_StartPullModeWithCallback (RAW)", hr);
    if (FAILED(hr)) {
        restoreRawMode();
        return false;
    }
    acquisitionStarted = true;

    bool completed = false;
    {
        std::unique_lock<std::mutex> lock(capture.mutex);
        completed = capture.completedCondition.wait_for(
            lock, std::chrono::seconds(30),
            [&capture] { return capture.completed; });
    }
    if (!completed) {
        std::cerr << "Timed out waiting 30 seconds for RAW frame sequence.\n";
    }

    if (acquisitionStarted) {
        const HRESULT stopResult = Amcam_Stop(camera);
        printHresult("Amcam_Stop after RAW acquisition", stopResult);
        if (FAILED(stopResult)) success = false;
        acquisitionStarted = false;
    }

    int droppedFramesAfter = -1;
    const HRESULT droppedAfterResult = Amcam_get_Option(
        camera, AMCAM_OPTION_NUMBER_DROP_FRAME, &droppedFramesAfter);
    printHresult("Read software-dropped frame counter after acquisition",
                 droppedAfterResult);
    if (SUCCEEDED(droppedBeforeResult) && SUCCEEDED(droppedAfterResult)) {
        std::cout << "    counter after: " << droppedFramesAfter
                  << "\n    counter delta: "
                  << (droppedFramesAfter - droppedFramesBefore) << '\n';
    }

    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        std::cout << "Image events received: " << capture.imageEvents
                  << "\nSuccessful RAW frames pulled: "
                  << capture.records.size()
                  << "\nSDK error events: " << capture.errorEvents
                  << "\nDisconnect events: " << capture.disconnectedEvents
                  << "\nFrame/packet timeout events: " << capture.timeoutEvents
                  << '\n';
        if (capture.terminalEvent != 0) {
            std::cout << "Terminal callback event: 0x"
                      << std::hex << std::uppercase << capture.terminalEvent
                      << std::dec << std::nouppercase << '\n';
        }
        std::cout << "Callback event histogram:";
        if (capture.eventCounts.empty()) {
            std::cout << " none";
        } else {
            for (const auto& [eventCode, count] : capture.eventCounts) {
                std::cout << " 0x" << std::hex << std::uppercase << eventCode
                          << std::dec << std::nouppercase << '=' << count;
            }
        }
        std::cout << '\n';
        if (capture.lastPullResult != 0 || !capture.records.empty()) {
            printHresult("Last Amcam_PullImageV4 result",
                         capture.lastPullResult);
        }
        if (capture.invalidFrameLayout) {
            std::cerr << "Returned RAW frame dimensions/layout exceed the allocated buffer.\n";
        }

        for (std::size_t i = 0; i < capture.records.size(); ++i) {
            const auto& record = capture.records[i];
            std::cout << "Frame[" << i << "]"
                      << " frameinfo_flags=0x" << std::hex
                      << record.info.v3.flag << std::dec
                      << " seq=" << record.info.v3.seq
                      << "(valid="
                      << ((record.info.v3.flag & AMCAM_FRAMEINFO_FLAG_SEQ) != 0)
                      << ") timestamp_us=" << record.info.v3.timestamp
                      << "(valid="
                      << ((record.info.v3.flag & AMCAM_FRAMEINFO_FLAG_TIMESTAMP) != 0)
                      << ") framecount=" << record.info.framecount
                      << " timecount=" << record.info.timecount
                      << " count_metadata_valid="
                      << ((record.info.v3.flag & AMCAM_FRAMEINFO_FLAG_COUNT) != 0)
                      << " uFV=" << record.info.uFV
                      << " autofocus_metadata_valid="
                      << ((record.info.v3.flag & AMCAM_FRAMEINFO_FLAG_AUTOFOCUS) != 0)
                      << " dimensions=" << record.info.v3.width << 'x'
                      << record.info.v3.height
                      << " bytes=" << record.byteCount
                      << " sample_range=" << record.minimum << ".."
                      << record.maximum
                      << " distinct_samples=" << record.distinctValues
                      << " sentinel_byte_matches=" << record.sentinelBytes
                      << " fnv1a64=0x" << std::hex << std::uppercase
                      << record.checksum << std::dec << std::nouppercase
                      << '\n';
        }

        if (!capture.records.empty()) {
            unsigned distinctSequences = 0;
            unsigned distinctTimestamps = 0;
            unsigned distinctChecksums = 0;
            for (std::size_t i = 0; i < capture.records.size(); ++i) {
                bool newSequence = true;
                bool newTimestamp = true;
                bool newChecksum = true;
                for (std::size_t j = 0; j < i; ++j) {
                    newSequence = newSequence &&
                        capture.records[i].info.v3.seq !=
                        capture.records[j].info.v3.seq;
                    newTimestamp = newTimestamp &&
                        capture.records[i].info.v3.timestamp !=
                        capture.records[j].info.v3.timestamp;
                    newChecksum = newChecksum &&
                        capture.records[i].checksum !=
                        capture.records[j].checksum;
                }
                if (newSequence) ++distinctSequences;
                if (newTimestamp) ++distinctTimestamps;
                if (newChecksum) ++distinctChecksums;
            }
            std::cout << "Distinct frame sequence values: " << distinctSequences
                      << " / " << capture.records.size()
                      << "\nDistinct frame timestamps: " << distinctTimestamps
                      << " / " << capture.records.size()
                      << "\nDistinct payload checksums: " << distinctChecksums
                      << " / " << capture.records.size() << '\n';

            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                std::cerr << "Could not create " << outputPath << '\n';
            } else {
                output.write(
                    reinterpret_cast<const char*>(capture.firstFrame.data()),
                    static_cast<std::streamsize>(capture.firstFrame.size()));
                output.close();
                if (!output) {
                    std::cerr << "Failed while writing " << outputPath << '\n';
                } else {
                    std::cout << "Saved first unmodified RAW payload: "
                              << outputPath << '\n';
                    success = true;
                }
            }
        }
    }

    if (capture.records.size() != targetFrameCount) success = false;
    if (!restoreRawMode()) success = false;
    std::cout << "===================================================\n";
    return success;
}

void printFirmwareIdentity(HAmcam camera) {
    std::cout << "\n--- Camera hardware/firmware identity ---\n";

    unsigned short revision = 0;
    HRESULT hr = Amcam_get_Revision(camera, &revision);
    printHresult("Amcam_get_Revision", hr);
    if (SUCCEEDED(hr)) {
        std::cout << "    revision: " << revision << " (0x"
                  << std::hex << std::uppercase << revision
                  << std::dec << std::nouppercase << ")\n";
    }

    char firmware[16]{};
    hr = Amcam_get_FwVersion(camera, firmware);
    printHresult("Amcam_get_FwVersion", hr);
    if (SUCCEEDED(hr)) {
        std::cout << "    firmware version: "
                  << boundedString(firmware, sizeof(firmware)) << '\n';
    }

    char hardware[16]{};
    hr = Amcam_get_HwVersion(camera, hardware);
    printHresult("Amcam_get_HwVersion", hr);
    if (SUCCEEDED(hr)) {
        std::cout << "    hardware version: "
                  << boundedString(hardware, sizeof(hardware)) << '\n';
    }

    char productionDate[10]{};
    hr = Amcam_get_ProductionDate(camera, productionDate);
    printHresult("Amcam_get_ProductionDate", hr);
    if (SUCCEEDED(hr)) {
        std::cout << "    production date: "
                  << boundedString(productionDate, sizeof(productionDate)) << '\n';
    }

    char fpga[16]{};
    hr = Amcam_get_FpgaVersion(camera, fpga);
    printHresult("Amcam_get_FpgaVersion", hr);
    if (SUCCEEDED(hr)) {
        std::cout << "    FPGA version: "
                  << boundedString(fpga, sizeof(fpga)) << '\n';
    }
}

void probeCamera(HAmcam camera, const AmcamDeviceV2& selectedDevice) {
    std::cout << "\n========== DIRECT AMCAM SDK PROBE ==========\n";
    const char* sdkVersion = Amcam_Version();
    std::cout << "SDK version: "
              << (sdkVersion ? sdkVersion : "<unavailable>") << '\n';
    printFirmwareIdentity(camera);
    std::cout << "SDK display name: "
              << boundedString(selectedDevice.displayname,
                               sizeof(selectedDevice.displayname))
              << '\n';

    if (selectedDevice.model && selectedDevice.model->name) {
        std::cout << "Selected model: " << selectedDevice.model->name << '\n';
        const std::uint64_t flags = selectedDevice.model->flag;
        std::cout << "Model flags decimal: " << flags
                  << "\nModel flags hex: 0x"
                  << std::hex << std::uppercase << flags
                  << std::dec << std::nouppercase << '\n';
        printRawModelFlags(flags);
    } else {
        std::cout << "Selected model descriptor: unavailable\n";
    }

    // The SDK returns the maximum depth as the successful HRESULT value,
    // for example 8 or 12, rather than through an output parameter.
    const HRESULT maximumDepthResult = Amcam_get_MaxBitDepth(camera);
    printHresult("Amcam_get_MaxBitDepth", maximumDepthResult);
    if (SUCCEEDED(maximumDepthResult) &&
        maximumDepthResult >= 8 && maximumDepthResult <= 16) {
        std::cout << "    interpreted maximum bit depth: "
                  << maximumDepthResult << " bits\n";
    } else {
        std::cout << "    interpreted maximum bit depth: unavailable\n";
    }

    printOptionInt(camera, "Amcam_get_Option(AMCAM_OPTION_RAW)",
                   AMCAM_OPTION_RAW);
    printOptionInt(camera, "Amcam_get_Option(AMCAM_OPTION_BITDEPTH)",
                   AMCAM_OPTION_BITDEPTH);
    printRgbOutputMode(camera);

    int currentPixelFormat = -1;
    const HRESULT currentPixelFormatResult = Amcam_get_Option(
        camera, AMCAM_OPTION_PIXEL_FORMAT, &currentPixelFormat);
    printHresult("Amcam_get_Option(AMCAM_OPTION_PIXEL_FORMAT)",
                 currentPixelFormatResult);
    if (SUCCEEDED(currentPixelFormatResult)) {
        const char* name = Amcam_get_PixelFormatName(currentPixelFormat);
        std::cout << "    current format code: " << currentPixelFormat
                  << ", name: " << (name ? name : "<unknown>") << '\n';
    }

    int pixelFormatCount = -1;
    const HRESULT countResult = Amcam_get_PixelFormatSupport(
        camera, static_cast<char>(-1), &pixelFormatCount);
    printHresult("Amcam_get_PixelFormatSupport(count)", countResult);
    std::cout << "    reported count: " << pixelFormatCount << '\n';

    constexpr int maximumFormatsToRead = 64;
    if (SUCCEEDED(countResult) &&
        pixelFormatCount >= 0 &&
        pixelFormatCount <= maximumFormatsToRead) {
        for (int i = 0; i < pixelFormatCount; ++i) {
            int code = -1;
            const HRESULT entryResult = Amcam_get_PixelFormatSupport(
                camera, static_cast<char>(i), &code);
            std::cout << "    entry[" << i << "]: ";
            printHresult("Amcam_get_PixelFormatSupport(entry)", entryResult);
            if (SUCCEEDED(entryResult)) {
                const char* name = Amcam_get_PixelFormatName(code);
                std::cout << "        code=" << code
                          << ", name=" << (name ? name : "<unknown>")
                          << '\n';
            }
        }
    } else if (SUCCEEDED(countResult) &&
               pixelFormatCount > maximumFormatsToRead) {
        std::cout << "    enumeration not expanded: count exceeds safety limit "
                  << maximumFormatsToRead << '\n';
    }

    unsigned rawFourCC = 0;
    unsigned rawBitsPerPixel = 0;
    const HRESULT rawFormatResult = Amcam_get_RawFormat(
        camera, &rawFourCC, &rawBitsPerPixel);
    printHresult("Amcam_get_RawFormat", rawFormatResult);
    if (SUCCEEDED(rawFormatResult)) {
        std::cout << "    RAW FourCC: '" << fourccString(rawFourCC)
                  << "' (0x" << std::hex << std::uppercase << rawFourCC
                  << std::dec << std::nouppercase << ')'
                  << "\n    RAW bits per pixel: " << rawBitsPerPixel
                  << '\n';
    }

    std::cout << "=============================================\n";
    std::cout << "BASELINE READ-ONLY: no Amcam_put_* calls were made during "
                 "this section; image acquisition was not started.\n";
}

 
bool probeRawModeTemporarily(HAmcam camera) {
    std::cout << "\n========== TEMPORARY RAW-MODE PROBE ==========\n";
    std::cout << "This mode changes AMCAM_OPTION_RAW to 1 while the "
                 "camera is stopped, reads capabilities, then restores "
                 "the original value.\n";

    int originalRawMode = -1;
    HRESULT hr = Amcam_get_Option(
        camera, AMCAM_OPTION_RAW, &originalRawMode);
    printHresult("Read original AMCAM_OPTION_RAW", hr);
    if (FAILED(hr) || (originalRawMode != 0 && originalRawMode != 1)) {
        std::cerr << "Cannot safely probe RAW mode: original mode is "
                     "unavailable or invalid.\n";
        return false;
    }
    std::cout << "    original RAW mode: " << originalRawMode << '\n';

    bool changedByProbe = false;
    const auto restoreOriginalMode = [&]() -> bool {
        if (!changedByProbe) return true;

        const HRESULT restoreHr = Amcam_put_Option(
            camera, AMCAM_OPTION_RAW, originalRawMode);
        printHresult("Restore original AMCAM_OPTION_RAW", restoreHr);
        if (FAILED(restoreHr)) {
            std::cerr << "CRITICAL: failed to restore the original RAW mode. "
                         "Power-cycle/reconnect the camera before further use.\n";
            return false;
        }

        int restoredMode = -1;
        const HRESULT verifyHr = Amcam_get_Option(
            camera, AMCAM_OPTION_RAW, &restoredMode);
        printHresult("Verify restored AMCAM_OPTION_RAW", verifyHr);
        if (FAILED(verifyHr) || restoredMode != originalRawMode) {
            std::cerr << "CRITICAL: RAW-mode restoration readback did not "
                         "match the original mode.\n";
            return false;
        }
        std::cout << "    restored RAW mode: " << restoredMode << '\n';
        return true;
    };

    if (originalRawMode == 0) {
        hr = Amcam_put_Option(camera, AMCAM_OPTION_RAW, 1);
        printHresult("Temporarily enable AMCAM_OPTION_RAW=1", hr);
        if (FAILED(hr)) {
            std::cout << "RAW mode could not be enabled; no mode change "
                         "was confirmed.\n";
            return false;
        }
        changedByProbe = true;
    } else {
        std::cout << "Camera was already in RAW mode; no mode switch needed.\n";
    }

    int activeRawMode = -1;
    hr = Amcam_get_Option(camera, AMCAM_OPTION_RAW, &activeRawMode);
    printHresult("Read active AMCAM_OPTION_RAW", hr);
    if (FAILED(hr) || activeRawMode != 1) {
        std::cerr << "RAW mode was not confirmed after the request.\n";
        restoreOriginalMode();
        return false;
    }

    std::cout << "\n--- Capabilities while RAW mode is active ---\n";

    const HRESULT maximumDepthResult = Amcam_get_MaxBitDepth(camera);
    printHresult("Amcam_get_MaxBitDepth [RAW mode]", maximumDepthResult);
    if (SUCCEEDED(maximumDepthResult) &&
        maximumDepthResult >= 8 && maximumDepthResult <= 16) {
        std::cout << "    interpreted RAW maximum: "
                  << maximumDepthResult << " bits\n";
    }

    printOptionInt(camera, "Amcam_get_Option(AMCAM_OPTION_BITDEPTH) [RAW]",
                   AMCAM_OPTION_BITDEPTH);

    int currentPixelFormat = -1;
    const HRESULT currentPixelFormatResult = Amcam_get_Option(
        camera, AMCAM_OPTION_PIXEL_FORMAT, &currentPixelFormat);
    printHresult("Amcam_get_Option(AMCAM_OPTION_PIXEL_FORMAT) [RAW]",
                 currentPixelFormatResult);
    if (SUCCEEDED(currentPixelFormatResult)) {
        const char* name = Amcam_get_PixelFormatName(currentPixelFormat);
        std::cout << "    current RAW pixel-format code: "
                  << currentPixelFormat << ", name: "
                  << (name ? name : "<unknown>") << '\n';
    }

    int pixelFormatCount = -1;
    const HRESULT countResult = Amcam_get_PixelFormatSupport(
        camera, static_cast<char>(-1), &pixelFormatCount);
    printHresult("Amcam_get_PixelFormatSupport(count) [RAW]", countResult);
    std::cout << "    RAW-mode reported count: " << pixelFormatCount << '\n';

    constexpr int maximumFormatsToRead = 64;
    if (SUCCEEDED(countResult) &&
        pixelFormatCount >= 0 &&
        pixelFormatCount <= maximumFormatsToRead) {
        for (int i = 0; i < pixelFormatCount; ++i) {
            int code = -1;
            const HRESULT entryResult = Amcam_get_PixelFormatSupport(
                camera, static_cast<char>(i), &code);
            std::cout << "    RAW entry[" << i << "]: ";
            printHresult("Amcam_get_PixelFormatSupport(entry)", entryResult);
            if (SUCCEEDED(entryResult)) {
                const char* name = Amcam_get_PixelFormatName(code);
                std::cout << "        code=" << code
                          << ", name=" << (name ? name : "<unknown>")
                          << '\n';
            }
        }
    } else if (SUCCEEDED(countResult) &&
               pixelFormatCount > maximumFormatsToRead) {
        std::cout << "    enumeration not expanded: count exceeds safety limit "
                  << maximumFormatsToRead << '\n';
    }

    unsigned rawFourCC = 0;
    unsigned rawBitsPerPixel = 0;
    const HRESULT rawFormatResult = Amcam_get_RawFormat(
        camera, &rawFourCC, &rawBitsPerPixel);
    printHresult("Amcam_get_RawFormat [RAW]", rawFormatResult);
    if (SUCCEEDED(rawFormatResult)) {
        std::cout << "    RAW FourCC: '" << fourccString(rawFourCC)
                  << "' (0x" << std::hex << std::uppercase << rawFourCC
                  << std::dec << std::nouppercase << ')'
                  << "\n    RAW bits per pixel: " << rawBitsPerPixel
                  << '\n';
    }

    const bool restored = restoreOriginalMode();
    std::cout << "==============================================\n";
    return restored;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: amscope_sdk_pixel_format_probe "
                     "[--index N] [--probe-raw-mode] [--capture-raw-frame] [--capture-raw-sequence]\n"
                     "Directly probes a connected camera using the vendor SDK.\n"
                     "--probe-raw-mode temporarily enables RAW mode while "
                     "the camera is stopped, reads RAW output capabilities, "
                     "and restores the original RAW mode.\n"
                     "--capture-raw-frame captures one RAW frame; --capture-raw-sequence "
                     "captures eight consecutive frames. Both write the first "
                     "payload to amscope-raw-frame.raw and report per-frame "
                     "sequence, timestamp, sample statistics, sentinel matches, "
                     "and checksum before restoring RAW mode.\n"
                     "A single connected camera is selected automatically. "
                     "With multiple cameras, choose an index interactively "
                     "or pass --index N.\n";
        return 0;
    }

    bool hasRequestedIndex = false;
    bool requestRawModeProbe = false;
    bool requestRawFrameCapture = false;
    bool requestRawSequenceCapture = false;
    unsigned requestedIndex = 0;
    for (int argument = 1; argument < argc; ++argument) {
        const std::string value(argv[argument]);
        if (value == "--index") {
            if (hasRequestedIndex || argument + 1 >= argc ||
                !parseIndex(argv[argument + 1], requestedIndex)) {
                std::cerr << "--index requires one non-negative integer.\n";
                return 64;
            }
            hasRequestedIndex = true;
            ++argument;
        } else if (value == "--probe-raw-mode") {
            if (requestRawModeProbe) {
                std::cerr << "--probe-raw-mode was specified more than once.\n";
                return 64;
            }
            requestRawModeProbe = true;
        } else if (value == "--capture-raw-frame") {
            if (requestRawFrameCapture) {
                std::cerr << "--capture-raw-frame was specified more than once.\n";
                return 64;
            }
            requestRawFrameCapture = true;
        } else if (value == "--capture-raw-sequence") {
            if (requestRawSequenceCapture) {
                std::cerr << "--capture-raw-sequence was specified more than once.\n";
                return 64;
            }
            requestRawSequenceCapture = true;
        } else {
            std::cerr << "Unknown argument: " << value << '\n'
                      << "Use --help for usage.\n";
            return 64;
        }
    }

    AmcamDeviceV2 devices[AMCAM_MAX]{};
    const unsigned enumeratedCount = Amcam_EnumV2(devices);
    if (enumeratedCount == 0) {
        std::cerr << "No Amcam camera was enumerated by the vendor SDK.\n";
        return 2;
    }
    if (enumeratedCount > AMCAM_MAX) {
        std::cerr << "Vendor SDK returned an invalid camera count: "
                  << enumeratedCount << '\n';
        return 3;
    }

    printDeviceList(devices, enumeratedCount);

    unsigned selectedIndex = 0;
    if (hasRequestedIndex) {
        selectedIndex = requestedIndex;
    } else if (enumeratedCount > 1) {
        std::cout << "\nSelect camera index: " << std::flush;
        if (!(std::cin >> selectedIndex)) {
            std::cerr << "No valid camera index was entered.\n";
            return 64;
        }
    }

    if (selectedIndex >= enumeratedCount) {
        std::cerr << "Camera index " << selectedIndex
                  << " is outside the enumerated range [0, "
                  << (enumeratedCount - 1) << "].\n";
        return 64;
    }

    const std::string cameraId = boundedString(
        devices[selectedIndex].id, sizeof(devices[selectedIndex].id));
    if (cameraId.empty()) {
        std::cerr << "The selected camera has an empty SDK camera ID.\n";
        return 4;
    }

    HAmcam camera = Amcam_Open(cameraId.c_str());
    if (!camera) {
        std::cerr << "Amcam_Open failed for selected camera ID: "
                  << cameraId << '\n';
        return 5;
    }

    probeCamera(camera, devices[selectedIndex]);
    bool rawModeProbeSucceeded = true;
    if (requestRawModeProbe) {
        rawModeProbeSucceeded = probeRawModeTemporarily(camera);
    }
    bool rawCaptureSucceeded = true;
    if (requestRawFrameCapture && requestRawSequenceCapture) {
        std::cerr << "Choose either --capture-raw-frame or --capture-raw-sequence, not both.\n";
        Amcam_Close(camera);
        return 64;
    }
    if (requestRawFrameCapture) {
        rawCaptureSucceeded = captureRawFrames(camera, 1);
    } else if (requestRawSequenceCapture) {
        rawCaptureSucceeded = captureRawFrames(camera, 8);
    }
    Amcam_Close(camera);
    return rawModeProbeSucceeded && rawCaptureSucceeded
        ? 0 : ((requestRawModeProbe || requestRawFrameCapture ||
          requestRawSequenceCapture) ? 6 : 0);
}
