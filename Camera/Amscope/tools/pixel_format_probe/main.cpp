#include "amcam.h"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

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
        std::cout << "\n[" << i << "] "
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

void probeCamera(HAmcam camera, const AmcamDeviceV2& selectedDevice) {
    std::cout << "\n========== DIRECT AMCAM SDK PROBE ==========\n";
    const char* sdkVersion = Amcam_Version();
    std::cout << "SDK version: "
              << (sdkVersion ? sdkVersion : "<unavailable>") << '\n';
    std::cout << "Selected device: "
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
    std::cout << "READ-ONLY: no Amcam_put_* calls were made and "
                 "image acquisition was not started.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: amscope_sdk_pixel_format_probe [--index N]\n"
                     "Enumerates connected Amcam cameras and directly probes "
                     "the selected device using the vendor SDK.\n"
                     "A single connected camera is selected automatically. "
                     "With multiple cameras, choose an index interactively "
                     "or pass --index N.\n";
        return 0;
    }

    bool hasRequestedIndex = false;
    unsigned requestedIndex = 0;
    if (argc == 3 && std::string(argv[1]) == "--index") {
        if (!parseIndex(argv[2], requestedIndex)) {
            std::cerr << "Invalid camera index: " << argv[2] << '\n';
            return 64;
        }
        hasRequestedIndex = true;
    } else if (argc != 1) {
        std::cerr << "Usage: amscope_sdk_pixel_format_probe [--index N]\n";
        return 64;
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
    Amcam_Close(camera);
    return 0;
}
