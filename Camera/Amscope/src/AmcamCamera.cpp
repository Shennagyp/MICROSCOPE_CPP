#define AMCAM_HRESULT_ERRORCODE_NEEDED
#include "amcam.h"
#include "amscope/AmcamCamera.h"
#include "AmcamEventQueue.h"
#include "amscope/detail/FrameStallWatchdog.h"
#include "camera/CameraFeatureValidation.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <charconv>
#include <cstdint>
#include <exception>
#include <limits>
#include <iterator>
#include <system_error>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <type_traits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr std::size_t kPoolSize = 4;
constexpr int kRgb24Bits = 24;
constexpr int kPackedRows = -1;
thread_local const void* g_acquisitionOwner = nullptr;

bool fail(amscope::CameraError* out, const char* operation,
          std::int32_t code, std::string message) {
    if (out) {
        out->vendorCode = code;
        out->operation = operation ? operation : "";
        out->message = std::move(message);
    }
    return false;
}
std::string hrMessage(const char* operation, HRESULT hr) {
    std::ostringstream text;
    text << (operation ? operation : "Amcam SDK")
         << " failed (HRESULT 0x" << std::hex << std::uppercase
         << static_cast<std::uint32_t>(hr) << ')';
    return text.str();
}
bool failHr(amscope::CameraError* out, const char* operation, HRESULT hr) {
    return fail(out, operation, static_cast<std::int32_t>(hr), hrMessage(operation, hr));
}
std::string boundedString(const char* value, std::size_t capacity) {
    if (!value) return {};
    return std::string(value, std::find(value, value + capacity, '\0'));
}

camera::CameraFeatureAccessError featureError(
    camera::CameraFeatureAccessError::Code code,
    std::string feature,
    std::string message,
    std::int32_t nativeCode = 0,
    std::optional<camera::CameraFeatureKind> expected = std::nullopt,
    std::optional<camera::CameraFeatureKind> actual = std::nullopt) {
    return {code, std::move(feature), std::move(message), nativeCode, expected, actual};
}

std::string resolutionFeatureSymbol(
    std::size_t index,
    const amscope::Resolution& resolution) {
    return std::to_string(index) + ":" +
        std::to_string(resolution.width) + "x" +
        std::to_string(resolution.height);
}

std::string rawFormatFeatureSymbol(unsigned fourCC, unsigned bitsPerPixel) {
    const auto byte = [](unsigned value, unsigned shift) {
        const char c = static_cast<char>((value >> shift) & 0xffU);
        return c >= 32 && c <= 126 ? c : '.';
    };
    std::string text;
    text.push_back(byte(fourCC, 0));
    text.push_back(byte(fourCC, 8));
    text.push_back(byte(fourCC, 16));
    text.push_back(byte(fourCC, 24));
    return text + ":" + std::to_string(bitsPerPixel);
}

std::string binningFeatureSymbol(const char* value, const char* method) {
    if (!value || !method || *value == '\0' || *method == '\0')
        return {};
    const std::string v(value);
    const std::string m(method);
    if (v.find('|') != std::string::npos ||
        m.find('|') != std::string::npos)
        return {};
    return v + "|" + m;
}

bool parseBinningFeatureSymbol(
    const std::string& symbol,
    std::string& value,
    std::string& method) {
    const auto separator = symbol.find('|');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= symbol.size() ||
        symbol.find('|', separator + 1) != std::string::npos)
        return false;
    value.assign(symbol.data(), separator);
    method.assign(symbol.data() + separator + 1,
                  symbol.size() - separator - 1);
    return true;
}


struct ModelFlagCapability {
    const char* name;
    const char* displayName;
    std::uint64_t flag;
    amscope::AmcamCameraFeature profileFeature;
    camera::CameraFeatureGroup group;
};

constexpr ModelFlagCapability kModelFlagCapabilities[] = {
    {"AmcamSensorCMOS", "CMOS Sensor", AMCAM_FLAG_CMOS,
     amscope::AmcamCameraFeature::SensorCMOS, camera::CameraFeatureGroup::Sensor},

    {"AmcamHighFullWellCapability", "High Full-Well Sensor Capability", AMCAM_FLAG_HIGH_FULLWELL,
     amscope::AmcamCameraFeature::HighFullWell, camera::CameraFeatureGroup::Sensor},
    {"AmcamHDRConversionGainAvailable", "HDR Conversion Gain", AMCAM_FLAG_CGHDR,
     amscope::AmcamCameraFeature::ConversionGain, camera::CameraFeatureGroup::Sensor},
    {"AmcamISPProcessorAvailable", "ISP Processor", AMCAM_FLAG_ISP,
     amscope::AmcamCameraFeature::ImageProcessing, camera::CameraFeatureGroup::ImageProcessing},

    {"AmcamSensorCCDProgressive", "Progressive CCD Sensor", AMCAM_FLAG_CCD_PROGRESSIVE,
     amscope::AmcamCameraFeature::SensorCCDProgressive, camera::CameraFeatureGroup::Sensor},
    {"AmcamSensorCCDInterlaced", "Interlaced CCD Sensor", AMCAM_FLAG_CCD_INTERLACED,
     amscope::AmcamCameraFeature::SensorCCDInterlaced, camera::CameraFeatureGroup::Sensor},
    {"AmcamMonochromeSensor", "Monochrome Sensor", AMCAM_FLAG_MONO,
     amscope::AmcamCameraFeature::MonochromeSensor, camera::CameraFeatureGroup::Sensor},
    {"AmcamGlobalShutter", "Global Shutter", AMCAM_FLAG_GLOBALSHUTTER,
     amscope::AmcamCameraFeature::GlobalShutter, camera::CameraFeatureGroup::Sensor},
    {"AmcamGhoptoSensor", "GHOPTO Sensor", AMCAM_FLAG_GHOPTO,
     amscope::AmcamCameraFeature::SensorGhopto, camera::CameraFeatureGroup::Sensor},
    {"AmcamFrameBufferAvailable", "Camera Frame Buffer", AMCAM_FLAG_BUFFER,
     amscope::AmcamCameraFeature::FrameBuffer, camera::CameraFeatureGroup::ImageFormat},
    {"AmcamDDRFrameBufferAvailable", "DDR Frame Buffer", AMCAM_FLAG_DDR,
     amscope::AmcamCameraFeature::DDRFrameBuffer, camera::CameraFeatureGroup::ImageFormat},
    {"AmcamSoftwareTriggerAvailable", "Software Trigger Support", AMCAM_FLAG_TRIGGER_SOFTWARE,
     amscope::AmcamCameraFeature::TriggerSoftware, camera::CameraFeatureGroup::Trigger},
    {"AmcamExternalTriggerAvailable", "External Trigger Support", AMCAM_FLAG_TRIGGER_EXTERNAL,
     amscope::AmcamCameraFeature::TriggerExternal, camera::CameraFeatureGroup::Trigger},
    {"AmcamSingleTriggerAvailable", "Single Trigger Support", AMCAM_FLAG_TRIGGER_SINGLE,
     amscope::AmcamCameraFeature::TriggerSingle, camera::CameraFeatureGroup::Trigger},
    {"AmcamSelfTriggerAvailable", "Self Trigger Support", AMCAM_FLAG_SELFTRIGGER,
     amscope::AmcamCameraFeature::SelfTrigger, camera::CameraFeatureGroup::Trigger},
    {"AmcamHardwareEventsAvailable", "Hardware Event Support", AMCAM_FLAG_EVENT_HARDWARE,
     amscope::AmcamCameraFeature::HardwareEvents, camera::CameraFeatureGroup::Trigger},
    {"AmcamFocusMotorAvailable", "Focus Motor", AMCAM_FLAG_FOCUSMOTOR,
     amscope::AmcamCameraFeature::FocusMotor, camera::CameraFeatureGroup::Hardware},
    {"AmcamAutoFocusAvailable", "Auto Focus", AMCAM_FLAG_AUTO_FOCUS,
     amscope::AmcamCameraFeature::AutoFocus, camera::CameraFeatureGroup::Hardware},
    {"AmcamAstroAutoFocuserAvailable", "Astro Auto Focuser", AMCAM_FLAG_AUTOFOCUSER,
     amscope::AmcamCameraFeature::AstroAutoFocuser, camera::CameraFeatureGroup::Hardware},
    {"AmcamST4PortAvailable", "ST4 Guide Port", AMCAM_FLAG_ST4,
     amscope::AmcamCameraFeature::ST4Port, camera::CameraFeatureGroup::Hardware},
    {"AmcamFilterWheelAvailable", "Filter Wheel", AMCAM_FLAG_FILTERWHEEL,
     amscope::AmcamCameraFeature::FilterWheel, camera::CameraFeatureGroup::Hardware},
    {"AmcamEmbeddedLightSourceAvailable", "Embedded Light Source", AMCAM_FLAG_LIGHTSOURCE,
     amscope::AmcamCameraFeature::EmbeddedLightSource, camera::CameraFeatureGroup::Hardware},
    {"AmcamStandaloneLightSourceAvailable", "Standalone Light Source", AMCAM_FLAG_LIGHT_SOURCE,
     amscope::AmcamCameraFeature::StandaloneLightSource, camera::CameraFeatureGroup::Hardware},
    {"AmcamHardwareLevelRangeAvailable", "Hardware Level Range", AMCAM_FLAG_LEVELRANGE_HARDWARE,
     amscope::AmcamCameraFeature::HardwareLevelRange, camera::CameraFeatureGroup::ImageProcessing},
    {"AmcamTransportUSB3", "USB 3.0 Transport", AMCAM_FLAG_USB30,
     amscope::AmcamCameraFeature::TransportUSB3, camera::CameraFeatureGroup::Transport},
    {"AmcamTransportUSB3OverUSB2", "USB 3.0 Camera on USB 2.0", AMCAM_FLAG_USB30_OVER_USB20,
     amscope::AmcamCameraFeature::TransportUSB3OverUSB2, camera::CameraFeatureGroup::Transport},
    {"AmcamTransportGigE", "GigE Transport", AMCAM_FLAG_GIGE,
     amscope::AmcamCameraFeature::TransportGigE, camera::CameraFeatureGroup::Transport},
    {"AmcamTransport2Point5GigE", "2.5 GigE Transport", AMCAM_FLAG_25GIGE,
     amscope::AmcamCameraFeature::Transport2Point5GigE, camera::CameraFeatureGroup::Transport},
    {"AmcamTransport5GigE", "5 GigE Transport", AMCAM_FLAG_5GIGE,
     amscope::AmcamCameraFeature::Transport5GigE, camera::CameraFeatureGroup::Transport},
    {"AmcamTransport10GigE", "10 GigE Transport", AMCAM_FLAG_10GIGE,
     amscope::AmcamCameraFeature::Transport10GigE, camera::CameraFeatureGroup::Transport},
    {"AmcamTransportCameraLink", "Camera Link Transport", AMCAM_FLAG_CAMERALINK,
     amscope::AmcamCameraFeature::TransportCameraLink, camera::CameraFeatureGroup::Transport},
    {"AmcamTransportCoaXPress", "CoaXPress Transport", AMCAM_FLAG_CXP,
     amscope::AmcamCameraFeature::TransportCoaXPress, camera::CameraFeatureGroup::Transport}
};

bool parseSignedInteger(const std::string& text, std::int32_t& value) {
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto parsed = std::from_chars(first, last, value);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

bool parseResolutionFeatureSymbol(
    const std::string& symbol,
    std::uint32_t& index,
    std::uint32_t& width,
    std::uint32_t& height) {
    const auto colon = symbol.find(':');
    const auto x = symbol.find('x', colon == std::string::npos ? 0 : colon + 1);
    if (colon == std::string::npos || x == std::string::npos ||
        colon == 0 || x <= colon + 1 || x + 1 >= symbol.size()) return false;

    const auto parseUnsigned = [](const std::string& text, std::uint32_t& output) {
        if (text.empty()) return false;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), output);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    };
    return parseUnsigned(symbol.substr(0, colon), index) &&
           parseUnsigned(symbol.substr(colon + 1, x - colon - 1), width) &&
           parseUnsigned(symbol.substr(x + 1), height);
}

bool featureIsOperational(amscope::CameraState state) {
    return state == amscope::CameraState::Open ||
           state == amscope::CameraState::Streaming;
}

camera::CameraFeatureResult<std::vector<camera::CameraFeatureDescriptor>>
buildAmcamFeatureDescriptors(
    HAmcam handle,
    amscope::CameraState state,
    std::uint64_t modelFlags,
    bool hasModelDescriptor,
    const std::optional<std::uint32_t>& maximumSpeedLevel,
    const std::optional<std::uint32_t>& maximumFanSpeed,
    const std::vector<amscope::Resolution>& previewResolutions,
    const amscope::AmcamCameraProfileRegistry& profileRegistry,
    const std::string& modelCode) {
    using camera::CameraFeatureDescriptor;
    using camera::CameraFeatureKind;
    using camera::CameraFeatureNumericRange;
    using camera::CameraFeatureAccessError;

    if (!handle || !featureIsOperational(state)) {
        return featureError(CameraFeatureAccessError::Code::BackendFailure, {},
                            "The Amcam camera session is not operational.");
    }

    int autoMode = -1;
    const HRESULT autoHr = Amcam_get_AutoExpoEnable(handle, &autoMode);
    const bool autoValid = SUCCEEDED(autoHr) && autoMode >= 0 && autoMode <= 2;

    unsigned exposure = 0, minExposure = 0, maxExposure = 0, defaultExposure = 0;
    const HRESULT exposureHr = Amcam_get_ExpoTime(handle, &exposure);
    const HRESULT exposureRangeHr = Amcam_get_ExpTimeRange(
        handle, &minExposure, &maxExposure, &defaultExposure);
    const bool exposureReadable = SUCCEEDED(exposureHr);
    const bool exposureProbe = autoValid && exposureReadable &&
        SUCCEEDED(exposureRangeHr) && minExposure > 0 &&
        maxExposure >= minExposure && exposure >= minExposure && exposure <= maxExposure;

    unsigned short gain = 0, minGain = 0, maxGain = 0, defaultGain = 0;
    const HRESULT gainHr = Amcam_get_ExpoAGain(handle, &gain);
    const HRESULT gainRangeHr = Amcam_get_ExpoAGainRange(
        handle, &minGain, &maxGain, &defaultGain);
    const bool gainReadable = SUCCEEDED(gainHr);
    const bool gainProbe = gainReadable && SUCCEEDED(gainRangeHr) &&
        maxGain >= minGain && gain >= minGain && gain <= maxGain;

    int temperature = 0, tint = 0;
    const HRESULT tempTintHr = Amcam_get_TempTint(handle, &temperature, &tint);
    const bool tempTintProbe = SUCCEEDED(tempTintHr);
    int rgbGain[3]{};
    const HRESULT rgbGainHr = Amcam_get_WhiteBalanceGain(handle, rgbGain);
    const bool rgbGainProbe = SUCCEEDED(rgbGainHr);

    int colorMatrix = 0;
    const HRESULT colorMatrixHr = Amcam_get_Option(handle, AMCAM_OPTION_COLORMATIX, &colorMatrix);
    const bool colorMatrixProbe = SUCCEEDED(colorMatrixHr);

    int currentPixelFormat = -1;
    const HRESULT currentPixelFormatHr =
        Amcam_get_Option(handle, AMCAM_OPTION_PIXEL_FORMAT, &currentPixelFormat);
    std::vector<std::string> supportedPixelFormats;
    int pixelFormatCount = 0;
    const HRESULT pixelCountHr =
        Amcam_get_PixelFormatSupport(handle, static_cast<char>(-1), &pixelFormatCount);
    if (SUCCEEDED(pixelCountHr) && pixelFormatCount > 0 && pixelFormatCount <= 64) {
        supportedPixelFormats.reserve(static_cast<std::size_t>(pixelFormatCount));
        for (int i = 0; i < pixelFormatCount; ++i) {
            int code = -1;
            const HRESULT pixelHr =
                Amcam_get_PixelFormatSupport(handle, static_cast<char>(i), &code);
            if (FAILED(pixelHr)) {
                supportedPixelFormats.clear();
                break;
            }
            supportedPixelFormats.push_back(std::to_string(code));
        }
    }
    const bool pixelFormatProbe = SUCCEEDED(currentPixelFormatHr) &&
        !supportedPixelFormats.empty() &&
        std::find(supportedPixelFormats.begin(), supportedPixelFormats.end(),
                  std::to_string(currentPixelFormat)) != supportedPixelFormats.end();

    int minimumFPS = 0, maximumFPS = 0, currentFPS = 0;
    bool fixedFrameRateProbe = false;
    const bool preciseFrameRateAdvertised =
        (modelFlags & AMCAM_FLAG_PRECISE_FRAMERATE) != 0;
    if (hasModelDescriptor && preciseFrameRateAdvertised) {
        const HRESULT minHr = Amcam_get_Option(
            handle, AMCAM_OPTION_MIN_PRECISE_FRAMERATE, &minimumFPS);
        const HRESULT maxHr = Amcam_get_Option(
            handle, AMCAM_OPTION_MAX_PRECISE_FRAMERATE, &maximumFPS);
        const HRESULT currentHr = Amcam_get_Option(
            handle, AMCAM_OPTION_PRECISE_FRAMERATE, &currentFPS);
        fixedFrameRateProbe = SUCCEEDED(minHr) && SUCCEEDED(maxHr) &&
            SUCCEEDED(currentHr) && minimumFPS > 0 && maximumFPS >= minimumFPS &&
            currentFPS >= minimumFPS && currentFPS <= maximumFPS;
    }

    unsigned short currentSpeed = 0;
    const HRESULT speedHr = Amcam_get_Speed(handle, &currentSpeed);
    const bool speedProbe = hasModelDescriptor && maximumSpeedLevel.has_value() &&
        *maximumSpeedLevel <= std::numeric_limits<std::uint16_t>::max() &&
        SUCCEEDED(speedHr) && currentSpeed <= *maximumSpeedLevel;

    unsigned currentResolution = 0;
    const HRESULT resolutionHr = Amcam_get_eSize(handle, &currentResolution);
    const bool resolutionAvailable = hasModelDescriptor && !previewResolutions.empty();
    const bool resolutionReadable = SUCCEEDED(resolutionHr) &&
        currentResolution < previewResolutions.size();

    constexpr std::uint64_t rawCapabilityFlags =
        AMCAM_FLAG_RAW8 | AMCAM_FLAG_RAW10 | AMCAM_FLAG_RAW11 |
        AMCAM_FLAG_RAW12 | AMCAM_FLAG_RAW14 | AMCAM_FLAG_RAW16 |
        AMCAM_FLAG_RAW12PACK;
    const bool rawModelSupported = hasModelDescriptor &&
        (modelFlags & rawCapabilityFlags) != 0;
    int rawMode = 0;
    const HRESULT rawModeHr =
        Amcam_get_Option(handle, AMCAM_OPTION_RAW, &rawMode);
    int rawBitDepth = 0;
    const HRESULT rawBitDepthHr =
        Amcam_get_Option(handle, AMCAM_OPTION_BITDEPTH, &rawBitDepth);
    unsigned rawFourCC = 0;
    unsigned rawBitsPerPixel = 0;
    const HRESULT rawFormatHr =
        Amcam_get_RawFormat(handle, &rawFourCC, &rawBitsPerPixel);
    const bool rawModeReadable = SUCCEEDED(rawModeHr) &&
        (rawMode == 0 || rawMode == 1);
    const bool rawAvailable = rawModelSupported && rawModeReadable;
    const bool rawFormatReadable = rawModeReadable && rawMode == 1 &&
        SUCCEEDED(rawFormatHr) && rawBitsPerPixel > 0 && rawBitsPerPixel <= 16;
    const bool rawBitDepthReadable = rawModeReadable && rawMode == 1 &&
        SUCCEEDED(rawBitDepthHr) && (rawBitDepth == 0 || rawBitDepth == 1);


    const bool hasColorRawSensor =
        (modelFlags & (AMCAM_FLAG_RAW8 | AMCAM_FLAG_RAW10 | AMCAM_FLAG_RAW11 |
                       AMCAM_FLAG_RAW12 | AMCAM_FLAG_RAW14 | AMCAM_FLAG_RAW16 |
                       AMCAM_FLAG_RAW12PACK)) != 0 &&
        (modelFlags & AMCAM_FLAG_MONO) == 0;

    int linearMode = -1;
    const HRESULT linearHr =
        Amcam_get_Option(handle, AMCAM_OPTION_LINEAR, &linearMode);
    const bool linearProbe = SUCCEEDED(linearHr) &&
        (linearMode == 0 || linearMode == 1);

    int curveMode = -1;
    const HRESULT curveHr =
        Amcam_get_Option(handle, AMCAM_OPTION_CURVE, &curveMode);
    const bool curveProbe = SUCCEEDED(curveHr) &&
        curveMode >= 0 && curveMode <= 2;

    int videoDemosaic = -1;
    const HRESULT videoDemosaicHr =
        Amcam_get_Option(handle, AMCAM_OPTION_DEMOSAIC_VIDEO, &videoDemosaic);
    const bool videoDemosaicProbe = hasColorRawSensor &&
        SUCCEEDED(videoDemosaicHr) && videoDemosaic >= 0 && videoDemosaic <= 4;

    int stillDemosaic = -1;
    const HRESULT stillDemosaicHr =
        Amcam_get_Option(handle, AMCAM_OPTION_DEMOSAIC_STILL, &stillDemosaic);
    const bool stillDemosaicProbe = hasColorRawSensor &&
        SUCCEEDED(stillDemosaicHr) && stillDemosaic >= 0 && stillDemosaic <= 4;

    int denoiseStrength = -1;
    const HRESULT denoiseHr =
        Amcam_get_Option(handle, AMCAM_OPTION_DENOISE, &denoiseStrength);
    const bool denoiseProbe = (modelFlags & AMCAM_FLAG_ISP) != 0 &&
        SUCCEEDED(denoiseHr) && denoiseStrength >= 0 && denoiseStrength <= 100;

    int blackLevel = -1;
    const HRESULT blackLevelHr =
        Amcam_get_Option(handle, AMCAM_OPTION_BLACKLEVEL, &blackLevel);
    const bool blackLevelProbe = (modelFlags & AMCAM_FLAG_BLACKLEVEL) != 0 &&
        SUCCEEDED(blackLevelHr);

    int conversionGain = -1;
    const HRESULT conversionGainHr =
        Amcam_get_Option(handle, AMCAM_OPTION_CG, &conversionGain);
    const bool conversionGainAdvertised =
        (modelFlags & (AMCAM_FLAG_CG | AMCAM_FLAG_CGHDR | AMCAM_FLAG_GHOPTO)) != 0;
    const bool conversionGainProbe = conversionGainAdvertised &&
        SUCCEEDED(conversionGainHr) && conversionGain >= 0 &&
        conversionGain <= 2 &&
        (conversionGain != 2 ||
         (modelFlags & (AMCAM_FLAG_CGHDR | AMCAM_FLAG_GHOPTO)) != 0);

    int lowNoise = -1;
    const HRESULT lowNoiseHr =
        Amcam_get_Option(handle, AMCAM_OPTION_LOW_NOISE, &lowNoise);
    const bool lowNoiseProbe = (modelFlags & AMCAM_FLAG_LOW_NOISE) != 0 &&
        SUCCEEDED(lowNoiseHr) && (lowNoise == 0 || lowNoise == 1);

    int highFullWell = -1;
    const HRESULT highFullWellHr =
        Amcam_get_Option(handle, AMCAM_OPTION_HIGH_FULLWELL, &highFullWell);
    const bool highFullWellProbe =
        (modelFlags & AMCAM_FLAG_HIGH_FULLWELL) != 0 &&
        SUCCEEDED(highFullWellHr) &&
        (highFullWell == 0 || highFullWell == 1);

    short sensorTemperature = 0;
    const HRESULT sensorTemperatureHr =
        Amcam_get_Temperature(handle, &sensorTemperature);
    const bool sensorTemperatureProbe =
        (modelFlags & AMCAM_FLAG_GETTEMPERATURE) != 0 &&
        SUCCEEDED(sensorTemperatureHr);

    int tecEnabled = -1;
    const HRESULT tecEnabledHr =
        Amcam_get_Option(handle, AMCAM_OPTION_TEC, &tecEnabled);
    const bool tecToggleProbe =
        (modelFlags & AMCAM_FLAG_TEC_ONOFF) != 0 &&
        SUCCEEDED(tecEnabledHr) && (tecEnabled == 0 || tecEnabled == 1);

    int tecTarget = 0;
    const HRESULT tecTargetHr =
        Amcam_get_Option(handle, AMCAM_OPTION_TECTARGET, &tecTarget);
    int tecTargetRangePacked = 0;
    const HRESULT tecTargetRangeHr =
        Amcam_get_Option(handle, AMCAM_OPTION_TECTARGET_RANGE, &tecTargetRangePacked);
    const auto signed16 = [](std::uint16_t value) -> std::int32_t {
        return (value & 0x8000U) != 0
            ? static_cast<std::int32_t>(value) - 65536
            : static_cast<std::int32_t>(value);
    };
    const auto tecRangeBits = static_cast<std::uint32_t>(tecTargetRangePacked);
    const std::int32_t tecTargetMinimum = signed16(
        static_cast<std::uint16_t>(tecRangeBits & 0xffffU));
    const std::int32_t tecTargetMaximum = signed16(
        static_cast<std::uint16_t>((tecRangeBits >> 16U) & 0xffffU));
    const bool tecTargetProbe =
        (modelFlags & AMCAM_FLAG_TEC_ONOFF) != 0 &&
        SUCCEEDED(tecTargetHr) && SUCCEEDED(tecTargetRangeHr) &&
        tecTargetMaximum >= tecTargetMinimum &&
        tecTarget >= tecTargetMinimum && tecTarget <= tecTargetMaximum;

    int fanSpeed = -1;
    const HRESULT fanHr =
        Amcam_get_Option(handle, AMCAM_OPTION_FAN, &fanSpeed);
    const bool fanProbe = (modelFlags & AMCAM_FLAG_FAN) != 0 &&
        maximumFanSpeed.has_value() && SUCCEEDED(fanHr) &&
        fanSpeed >= -1 &&
        (fanSpeed == -1 ||
         static_cast<std::uint32_t>(fanSpeed) <= *maximumFanSpeed);

    int heatLevel = -1;
    int heatMaximum = -1;
    const HRESULT heatHr =
        Amcam_get_Option(handle, AMCAM_OPTION_HEAT, &heatLevel);
    const HRESULT heatMaximumHr =
        Amcam_get_Option(handle, AMCAM_OPTION_HEAT_MAX, &heatMaximum);
    const bool heatProbe = (modelFlags & AMCAM_FLAG_HEAT) != 0 &&
        SUCCEEDED(heatHr) && SUCCEEDED(heatMaximumHr) &&
        heatMaximum >= 0 && heatLevel >= 0 && heatLevel <= heatMaximum;

    int tecVoltage = 0;
    const HRESULT tecVoltageHr =
        Amcam_get_Option(handle, AMCAM_OPTION_TEC_VOLTAGE, &tecVoltage);
    const bool tecVoltageProbe = (modelFlags & AMCAM_FLAG_TEC) != 0 &&
        SUCCEEDED(tecVoltageHr);

    int flatFieldState = -1;
    const HRESULT flatFieldHr =
        Amcam_get_Option(handle, AMCAM_OPTION_FFC, &flatFieldState);
    const int flatFieldLow = static_cast<int>(
        static_cast<std::uint32_t>(flatFieldState) & 0xffU);
    const bool flatFieldProbe = SUCCEEDED(flatFieldHr) &&
        flatFieldLow <= 2;

    int darkFieldState = -1;
    const HRESULT darkFieldHr =
        Amcam_get_Option(handle, AMCAM_OPTION_DFC, &darkFieldState);
    const int darkFieldLow = static_cast<int>(
        static_cast<std::uint32_t>(darkFieldState) & 0xffU);
    const bool darkFieldProbe = SUCCEEDED(darkFieldHr) &&
        darkFieldLow <= 2;

    int defectPixelState = -1;
    const HRESULT defectPixelHr =
        Amcam_get_Option(handle, AMCAM_OPTION_DEFECT_PIXEL, &defectPixelState);
    const bool defectPixelProbe = SUCCEEDED(defectPixelHr) &&
        (defectPixelState == 0 || defectPixelState == 1);

    int sharpeningPacked = 0;
    const HRESULT sharpeningHr =
        Amcam_get_Option(handle, AMCAM_OPTION_SHARPENING, &sharpeningPacked);
    const std::uint32_t sharpeningBits =
        static_cast<std::uint32_t>(sharpeningPacked);
    const unsigned sharpeningStrength = sharpeningBits & 0xffffU;
    const unsigned sharpeningRadius = (sharpeningBits >> 16U) & 0xffU;
    const bool sharpeningProbe = SUCCEEDED(sharpeningHr) &&
        sharpeningStrength <= 500U &&
        ((sharpeningStrength == 0U && sharpeningRadius <= 10U) ||
         (sharpeningRadius >= 1U && sharpeningRadius <= 10U));

    int rotationDegrees = -1;
    const HRESULT rotationHr =
        Amcam_get_Option(handle, AMCAM_OPTION_ROTATE, &rotationDegrees);
    const bool rotationProbe = SUCCEEDED(rotationHr) &&
        (rotationDegrees == 0 || rotationDegrees == 90 ||
         rotationDegrees == 180 || rotationDegrees == 270);

    int upsideDown = -1;
    const HRESULT upsideDownHr =
        Amcam_get_Option(handle, AMCAM_OPTION_UPSIDE_DOWN, &upsideDown);
    const bool upsideDownProbe = SUCCEEDED(upsideDownHr) &&
        (upsideDown == 0 || upsideDown == 1);

    int testPattern = -1;
    const HRESULT testPatternHr =
        Amcam_get_Option(handle, AMCAM_OPTION_TESTPATTERN, &testPattern);
    const bool testPatternProbe = SUCCEEDED(testPatternHr) &&
        (testPattern == 0 || testPattern == 3 || testPattern == 5 ||
         testPattern == 7 || testPattern == 9);

    int autoExposureThreshold = -1;
    const HRESULT autoExposureThresholdHr =
        Amcam_get_Option(handle, AMCAM_OPTION_AUTOEXP_THRESHOLD,
                         &autoExposureThreshold);
    const bool autoExposureThresholdProbe =
        SUCCEEDED(autoExposureThresholdHr) &&
        autoExposureThreshold >= 2 && autoExposureThreshold <= 15;


    const bool roiModelSupported = hasModelDescriptor &&
        (modelFlags & AMCAM_FLAG_ROI_HARDWARE) != 0;
    unsigned roiX = 0, roiY = 0, roiWidth = 0, roiHeight = 0;
    const HRESULT roiHr = Amcam_get_Roi(
        handle, &roiX, &roiY, &roiWidth, &roiHeight);
    unsigned roiPreviewIndex = std::numeric_limits<unsigned>::max();
    const HRESULT roiPreviewIndexHr = Amcam_get_eSize(handle, &roiPreviewIndex);
    const bool roiPreviewIndexValid = SUCCEEDED(roiPreviewIndexHr) &&
        roiPreviewIndex < previewResolutions.size();
    bool roiGeometryValid = false;
    if (roiPreviewIndexValid && roiWidth != 0 && roiHeight != 0 &&
        ((roiX | roiY | roiWidth | roiHeight) & 1U) == 0) {
        const auto& base = previewResolutions[roiPreviewIndex];
        const std::uint64_t right =
            static_cast<std::uint64_t>(roiX) + roiWidth;
        const std::uint64_t bottom =
            static_cast<std::uint64_t>(roiY) + roiHeight;
        roiGeometryValid = base.width != 0 && base.height != 0 &&
            right <= base.width && bottom <= base.height;
    }
    const bool roiProbe = roiModelSupported && SUCCEEDED(roiHr) &&
        roiGeometryValid;

    const bool binningModelSupported = hasModelDescriptor &&
        (modelFlags & AMCAM_FLAG_BINSKIP_SUPPORTED) != 0;
    const HRESULT binningCountResult = Amcam_get_BinningNumber(handle);
    std::vector<std::string> supportedBinningModes;
    if (binningModelSupported && SUCCEEDED(binningCountResult) &&
        binningCountResult > 0 && binningCountResult <= 128) {
        const auto count = static_cast<unsigned>(binningCountResult);
        supportedBinningModes.reserve(count);
        for (unsigned index = 0; index < count; ++index) {
            const char* valueText = nullptr;
            const char* methodText = nullptr;
            const HRESULT valueHr =
                Amcam_get_BinningValue(handle, index, &valueText);
            const HRESULT methodHr =
                Amcam_get_BinningMethod(handle, index, &methodText);
            if (FAILED(valueHr) || FAILED(methodHr)) {
                supportedBinningModes.clear();
                break;
            }
            auto symbol = binningFeatureSymbol(valueText, methodText);
            if (symbol.empty()) {
                supportedBinningModes.clear();
                break;
            }
            supportedBinningModes.push_back(std::move(symbol));
        }
    }
    const char* currentBinningValue = nullptr;
    const char* currentBinningMethod = nullptr;
    const HRESULT currentBinningHr = Amcam_get_Binning(
        handle, &currentBinningValue, &currentBinningMethod);
    const std::string currentBinningSymbol = SUCCEEDED(currentBinningHr)
        ? binningFeatureSymbol(currentBinningValue, currentBinningMethod)
        : std::string{};
    const bool binningProbe = binningModelSupported &&
        !supportedBinningModes.empty() && !currentBinningSymbol.empty() &&
        std::find(supportedBinningModes.begin(), supportedBinningModes.end(),
                  currentBinningSymbol) != supportedBinningModes.end();

    const bool stopped = state == amscope::CameraState::Open;
    const auto allowed = [&profileRegistry, &modelCode](amscope::AmcamCameraFeature feature) {
        return profileRegistry.allows(modelCode, feature);
    };
    const bool exposureAutoAllowed = allowed(amscope::AmcamCameraFeature::ExposureAuto);
    const bool exposureManualAllowed = allowed(amscope::AmcamCameraFeature::ExposureManual);
    const bool gainAllowed = allowed(amscope::AmcamCameraFeature::AnalogGain);
    const bool whiteBalanceAllowed = allowed(amscope::AmcamCameraFeature::WhiteBalance);
    const bool colorCorrectionAllowed = allowed(amscope::AmcamCameraFeature::ColorCorrection);
    const bool pixelFormatAllowed = allowed(amscope::AmcamCameraFeature::PixelFormatSelection);
    const bool fixedFrameRateAllowed = allowed(amscope::AmcamCameraFeature::PreciseFrameRate);
    const bool speedLevelAllowed = allowed(amscope::AmcamCameraFeature::DiscreteFrameSpeed);
    const bool resolutionAllowed = allowed(amscope::AmcamCameraFeature::Resolution);
    const bool rawOutputAllowed = allowed(amscope::AmcamCameraFeature::RawOutput);
    const bool imageProcessingAllowed = allowed(amscope::AmcamCameraFeature::ImageProcessing);
    const bool blackLevelAllowed = allowed(amscope::AmcamCameraFeature::BlackLevel);
    const bool conversionGainAllowed = allowed(amscope::AmcamCameraFeature::ConversionGain);
    const bool lowNoiseAllowed = allowed(amscope::AmcamCameraFeature::LowNoiseMode);
    const bool highFullWellAllowed = allowed(amscope::AmcamCameraFeature::HighFullWell);
    const bool tecAllowed = allowed(amscope::AmcamCameraFeature::ThermoelectricCooling);
    const bool tecOnOffAllowed = allowed(amscope::AmcamCameraFeature::ThermoelectricCoolingOnOff);
    const bool sensorTemperatureAllowed = allowed(amscope::AmcamCameraFeature::SensorTemperature);
    const bool fanAllowed = allowed(amscope::AmcamCameraFeature::CoolingFan);
    const bool heatAllowed = allowed(amscope::AmcamCameraFeature::HeatAntiFog);
    const bool roiAllowed = allowed(amscope::AmcamCameraFeature::HardwareROI);
    const bool binningAllowed = allowed(amscope::AmcamCameraFeature::BinningAndSkipping);

    std::vector<CameraFeatureDescriptor> descriptors;
    descriptors.reserve(72);

    descriptors.push_back({
        "ExposureAuto", "Automatic Exposure", CameraFeatureKind::Enumeration,
        std::nullopt, autoValid && exposureAutoAllowed, autoValid,
        autoValid && exposureAutoAllowed, std::nullopt, std::nullopt,
        {"Off", "Once", "Continuous"}
    });
    descriptors.push_back({
        "ExposureTime", "Exposure Time", CameraFeatureKind::Integer,
        std::string("us"), exposureProbe && exposureManualAllowed, exposureProbe,
        exposureProbe && exposureManualAllowed &&
            autoMode == static_cast<int>(amscope::AmcamAutoExposureMode::Off),
        exposureProbe
            ? std::optional<CameraFeatureNumericRange>(CameraFeatureNumericRange{
                  static_cast<double>(minExposure), static_cast<double>(maxExposure), std::nullopt})
            : std::nullopt,
        std::nullopt, {}
    });
    descriptors.push_back({
        "AmcamAnalogGainPercent", "Analog Gain", CameraFeatureKind::Integer,
        std::string("%"), gainProbe && gainAllowed, gainProbe, gainProbe && gainAllowed,
        gainProbe
            ? std::optional<CameraFeatureNumericRange>(CameraFeatureNumericRange{
                  static_cast<double>(minGain), static_cast<double>(maxGain), std::nullopt})
            : std::nullopt,
        std::nullopt, {}
    });
    descriptors.push_back({
        "AmcamColorMatrixEnabled", "Color Matrix", CameraFeatureKind::Boolean,
        std::nullopt, colorMatrixProbe && colorCorrectionAllowed,
        colorMatrixProbe, colorMatrixProbe && colorCorrectionAllowed,
        std::nullopt, std::nullopt, {}
    });
    descriptors.push_back({
        "AmcamPixelFormat", "Pixel Format", CameraFeatureKind::Enumeration,
        std::nullopt, pixelFormatProbe && pixelFormatAllowed,
        SUCCEEDED(currentPixelFormatHr),
        pixelFormatProbe && pixelFormatAllowed && stopped, std::nullopt, std::nullopt,
        std::move(supportedPixelFormats)
    });
    descriptors.push_back({
        "AmcamWhiteBalanceTemperature", "White Balance Temperature", CameraFeatureKind::Integer,
        std::string("K"), tempTintProbe && whiteBalanceAllowed,
        tempTintProbe, tempTintProbe && whiteBalanceAllowed,
        CameraFeatureNumericRange{2000.0, 15000.0, std::nullopt},
        std::nullopt, {}
    });
    descriptors.push_back({
        "AmcamWhiteBalanceTint", "White Balance Tint", CameraFeatureKind::Integer,
        std::nullopt, tempTintProbe && whiteBalanceAllowed,
        tempTintProbe, tempTintProbe && whiteBalanceAllowed,
        CameraFeatureNumericRange{200.0, 2500.0, std::nullopt},
        std::nullopt, {}
    });
    descriptors.push_back({
        "AmcamWhiteBalanceRGBGain", "White Balance RGB Gain", CameraFeatureKind::IntegerVector,
        std::string("SDK units"), rgbGainProbe && whiteBalanceAllowed,
        rgbGainProbe && whiteBalanceAllowed, rgbGainProbe && whiteBalanceAllowed,
        std::nullopt, std::size_t{3}, {}
    });
    descriptors.push_back({
        "AmcamFixedFrameRate", "Fixed Frame Rate", CameraFeatureKind::FloatingPoint,
        std::string("fps"), fixedFrameRateProbe && fixedFrameRateAllowed,
        fixedFrameRateProbe, fixedFrameRateProbe && fixedFrameRateAllowed,
        fixedFrameRateProbe
            ? std::optional<CameraFeatureNumericRange>(CameraFeatureNumericRange{
                  static_cast<double>(minimumFPS) / 10.0,
                  static_cast<double>(maximumFPS) / 10.0, 0.1})
            : std::nullopt,
        std::nullopt, {}
    });
    descriptors.push_back({
        "AmcamFrameSpeedLevel", "Frame Speed Level", CameraFeatureKind::Integer,
        std::string("level"), speedProbe && speedLevelAllowed, SUCCEEDED(speedHr),
        speedProbe && speedLevelAllowed,
        speedProbe
            ? std::optional<CameraFeatureNumericRange>(CameraFeatureNumericRange{
                  0.0, static_cast<double>(*maximumSpeedLevel), std::nullopt})
            : std::nullopt,
        std::nullopt, {}
    });
    std::vector<std::string> resolutionSymbols;
    if (hasModelDescriptor && !previewResolutions.empty()) {
        resolutionSymbols.reserve(previewResolutions.size());
        for (std::size_t i = 0; i < previewResolutions.size(); ++i)
            resolutionSymbols.push_back(resolutionFeatureSymbol(i, previewResolutions[i]));
    }
    descriptors.push_back({
        "AmcamResolutionIndex", "Resolution", CameraFeatureKind::Enumeration,
        std::nullopt, resolutionAvailable && resolutionAllowed,
        resolutionReadable, resolutionAvailable && resolutionAllowed && stopped,
        std::nullopt, std::nullopt, std::move(resolutionSymbols),
        camera::CameraFeatureGroup::ImageFormat
    });
    descriptors.push_back({
        "AmcamRawOutputEnabled", "RAW Sensor Output", CameraFeatureKind::Boolean,
        std::nullopt, rawAvailable && rawOutputAllowed,
        rawModeReadable, rawAvailable && rawOutputAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::ImageFormat
    });
    descriptors.push_back({
        "AmcamRawBitDepth", "RAW Output Bit Depth", CameraFeatureKind::Enumeration,
        std::string("bits"), rawAvailable && rawOutputAllowed &&
            rawMode == 1 && rawBitDepthReadable,
        rawBitDepthReadable, rawAvailable && rawOutputAllowed &&
            rawMode == 1 && rawBitDepthReadable && stopped,
        std::nullopt, std::nullopt, {"8-bit", "16-bit"},
        camera::CameraFeatureGroup::ImageFormat
    });
    descriptors.push_back({
        "AmcamRawSensorFormat", "RAW Sensor Format", CameraFeatureKind::Enumeration,
        std::nullopt, rawAvailable && rawOutputAllowed && rawFormatReadable,
        rawFormatReadable, false,
        std::nullopt, std::nullopt,
        rawFormatReadable
            ? std::vector<std::string>{rawFormatFeatureSymbol(rawFourCC, rawBitsPerPixel)}
            : std::vector<std::string>{},
        camera::CameraFeatureGroup::ImageFormat
    });

    descriptors.push_back({
        "AmcamLinearToneMappingEnabled", "Linear Tone Mapping",
        CameraFeatureKind::Boolean, std::nullopt,
        linearProbe && imageProcessingAllowed, linearProbe,
        linearProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamCurveMode", "Tone Curve",
        CameraFeatureKind::Enumeration, std::nullopt,
        curveProbe && imageProcessingAllowed, curveProbe,
        curveProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt,
        {"Off", "Polynomial", "Logarithmic"},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamDemosaicVideo", "Video Demosaic",
        CameraFeatureKind::Enumeration, std::nullopt,
        videoDemosaicProbe && imageProcessingAllowed, videoDemosaicProbe,
        videoDemosaicProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt,
        {"Bilinear", "VNG", "PPG", "AHD", "EdgeAware"},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamDemosaicStill", "Still Demosaic",
        CameraFeatureKind::Enumeration, std::nullopt,
        stillDemosaicProbe && imageProcessingAllowed, stillDemosaicProbe,
        stillDemosaicProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt,
        {"Bilinear", "VNG", "PPG", "AHD", "EdgeAware"},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamDenoiseStrength", "Denoise Strength",
        CameraFeatureKind::Integer, std::string("0-100"),
        denoiseProbe && imageProcessingAllowed, denoiseProbe,
        denoiseProbe && imageProcessingAllowed && stopped,
        CameraFeatureNumericRange{0.0, 100.0, 1.0},
        std::nullopt, {},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamBlackLevel", "Sensor Black Level",
        CameraFeatureKind::Integer, std::string("SDK units"),
        blackLevelProbe && blackLevelAllowed, blackLevelProbe,
        blackLevelProbe && blackLevelAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::Sensor
    });
    std::vector<std::string> conversionGainModes;
    if (conversionGainProbe) {
        conversionGainModes = {"LCG", "HCG"};
        if ((modelFlags & AMCAM_FLAG_CGHDR) != 0)
            conversionGainModes.push_back("HDR");
        else if ((modelFlags & AMCAM_FLAG_GHOPTO) != 0)
            conversionGainModes.push_back("MCG");
    }
    descriptors.push_back({
        "AmcamConversionGainMode", "Conversion Gain",
        CameraFeatureKind::Enumeration, std::nullopt,
        conversionGainProbe && conversionGainAllowed, conversionGainProbe,
        conversionGainProbe && conversionGainAllowed && stopped,
        std::nullopt, std::nullopt, std::move(conversionGainModes),
        camera::CameraFeatureGroup::Sensor
    });
    descriptors.push_back({
        "AmcamLowNoiseModeEnabled", "Low Noise Mode",
        CameraFeatureKind::Boolean, std::nullopt,
        lowNoiseProbe && lowNoiseAllowed, lowNoiseProbe,
        lowNoiseProbe && lowNoiseAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::Sensor
    });
    descriptors.push_back({
        "AmcamHighFullWellEnabled", "High Full Well",
        CameraFeatureKind::Boolean, std::nullopt,
        highFullWellProbe && highFullWellAllowed, highFullWellProbe,
        highFullWellProbe && highFullWellAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::Sensor
    });
    descriptors.push_back({
        "AmcamSensorTemperature", "Sensor Temperature",
        CameraFeatureKind::Integer, std::string("0.1 °C"),
        sensorTemperatureProbe && sensorTemperatureAllowed,
        sensorTemperatureProbe, false,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::Hardware
    });
    descriptors.push_back({
        "AmcamTecEnabled", "Thermoelectric Cooler",
        CameraFeatureKind::Boolean, std::nullopt,
        tecToggleProbe && tecOnOffAllowed, tecToggleProbe,
        tecToggleProbe && tecOnOffAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::Hardware
    });
    descriptors.push_back({
        "AmcamTecTargetTemperature", "TEC Target Temperature",
        CameraFeatureKind::Integer, std::string("0.1 °C"),
        tecTargetProbe && tecOnOffAllowed, tecTargetProbe,
        tecTargetProbe && tecOnOffAllowed && stopped,
        tecTargetProbe
            ? std::optional<CameraFeatureNumericRange>(CameraFeatureNumericRange{
                  static_cast<double>(tecTargetMinimum),
                  static_cast<double>(tecTargetMaximum), 1.0})
            : std::nullopt,
        std::nullopt, {},
        camera::CameraFeatureGroup::Hardware
    });
    descriptors.push_back({
        "AmcamTecVoltage", "TEC Voltage",
        CameraFeatureKind::Integer, std::string("0.1 V"),
        tecVoltageProbe && tecAllowed, tecVoltageProbe, false,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::Hardware
    });
    descriptors.push_back({
        "AmcamFanSpeed", "Cooling Fan Speed",
        CameraFeatureKind::Integer, std::string("SDK level"),
        fanProbe && fanAllowed, fanProbe,
        fanProbe && fanAllowed && stopped,
        fanProbe
            ? std::optional<CameraFeatureNumericRange>(CameraFeatureNumericRange{
                  -1.0, static_cast<double>(*maximumFanSpeed), 1.0})
            : std::nullopt,
        std::nullopt, {},
        camera::CameraFeatureGroup::Hardware
    });
    descriptors.push_back({
        "AmcamHeatAntiFogLevel", "Anti-Fog Heater",
        CameraFeatureKind::Integer, std::string("SDK level"),
        heatProbe && heatAllowed, heatProbe,
        heatProbe && heatAllowed && stopped,
        heatProbe
            ? std::optional<CameraFeatureNumericRange>(CameraFeatureNumericRange{
                  0.0, static_cast<double>(heatMaximum), 1.0})
            : std::nullopt,
        std::nullopt, {},
        camera::CameraFeatureGroup::Hardware
    });

    descriptors.push_back({
        "AmcamBinning", "Binning / Skip Mode",
        CameraFeatureKind::Enumeration, std::nullopt,
        binningProbe && binningAllowed,
        binningProbe,
        binningProbe && binningAllowed && stopped,
        std::nullopt, std::nullopt, std::move(supportedBinningModes),
        camera::CameraFeatureGroup::ImageFormat
    });
    descriptors.push_back({
        "AmcamHardwareROI", "Hardware ROI",
        CameraFeatureKind::IntegerVector, std::string("pixels"),
        roiProbe && roiAllowed,
        roiProbe,
        roiProbe && roiAllowed && stopped,
        std::nullopt, std::size_t{4}, {},
        camera::CameraFeatureGroup::ImageFormat
    });


    descriptors.push_back({
        "AmcamFlatFieldCorrectionEnabled", "Flat-Field Correction",
        CameraFeatureKind::Boolean, std::nullopt,
        flatFieldProbe && imageProcessingAllowed, flatFieldProbe,
        flatFieldProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamDarkFieldCorrectionEnabled", "Dark-Field Correction",
        CameraFeatureKind::Boolean, std::nullopt,
        darkFieldProbe && imageProcessingAllowed, darkFieldProbe,
        darkFieldProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamDefectPixelCorrectionEnabled", "Defect-Pixel Correction",
        CameraFeatureKind::Boolean, std::nullopt,
        defectPixelProbe && imageProcessingAllowed, defectPixelProbe,
        defectPixelProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamSharpeningParameters", "Sharpening Parameters",
        CameraFeatureKind::IntegerVector, std::string("strength, radius, threshold"),
        sharpeningProbe && imageProcessingAllowed, sharpeningProbe,
        sharpeningProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::size_t{3}, {},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamRotationDegrees", "Clockwise Image Rotation",
        CameraFeatureKind::Enumeration, std::string("degrees"),
        rotationProbe && imageProcessingAllowed, rotationProbe,
        rotationProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt, {"0", "90", "180", "270"},
        camera::CameraFeatureGroup::ImageFormat
    });
    descriptors.push_back({
        "AmcamUpsideDownEnabled", "Upside-Down Image",
        CameraFeatureKind::Boolean, std::nullopt,
        upsideDownProbe && imageProcessingAllowed, upsideDownProbe,
        upsideDownProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt, {},
        camera::CameraFeatureGroup::ImageFormat
    });
    descriptors.push_back({
        "AmcamTestPattern", "Sensor Test Pattern",
        CameraFeatureKind::Enumeration, std::nullopt,
        testPatternProbe && imageProcessingAllowed, testPatternProbe,
        testPatternProbe && imageProcessingAllowed && stopped,
        std::nullopt, std::nullopt,
        {"Off", "MonochromeDiagonal", "MonochromeVertical",
         "MonochromeHorizontal", "ChromaticDiagonal"},
        camera::CameraFeatureGroup::ImageProcessing
    });
    descriptors.push_back({
        "AmcamAutoExposureThreshold", "Auto-Exposure Threshold",
        CameraFeatureKind::Integer, std::string("SDK units"),
        autoExposureThresholdProbe && exposureAutoAllowed,
        autoExposureThresholdProbe,
        autoExposureThresholdProbe && exposureAutoAllowed && stopped,
        CameraFeatureNumericRange{2.0, 15.0, 1.0},
        std::nullopt, {},
        camera::CameraFeatureGroup::Exposure
    });

    // Model flags are hardware facts, not mutable camera settings.    // Model flags are hardware facts, not mutable camera settings. Keep them
    // read-only and apply the same exact-model allow/deny policy as controls.
    for (const auto& capability : kModelFlagCapabilities) {
        const bool supported = hasModelDescriptor &&
            (modelFlags & capability.flag) != 0;
        const bool profileAllows =
            profileRegistry.allows(modelCode, capability.profileFeature);
        descriptors.push_back({
            capability.name, capability.displayName,
            CameraFeatureKind::Boolean, std::nullopt,
            supported && profileAllows,
            supported,
            false,
            std::nullopt, std::nullopt, {},
            capability.group
        });
    }
    return descriptors;
}

camera::CameraFeatureAccessError featureBackendError(
    const std::string& feature,
    const char* operation,
    HRESULT hr) {
    return featureError(camera::CameraFeatureAccessError::Code::BackendFailure,
                        feature, hrMessage(operation, hr),
                        static_cast<std::int32_t>(hr));
}

} // namespace

namespace amscope {
struct AmcamCamera::Impl {
    explicit Impl(AmcamCameraProfileRegistry profiles)
        : profileRegistry(std::move(profiles)) {}

    struct FramePool {
        FramePool(std::uint32_t w, std::uint32_t h,
                  std::uint32_t stride, std::size_t bytes,
                  PixelFormat format, std::uint32_t formatCode,
                  std::uint8_t bitsPerPixel, std::uint8_t containerBitsPerPixel,
                  std::size_t bytesPerPixel)
            : discard(bytes), pixelFormat(format), pixelFormatCode(formatCode),
              bitsPerPixel(bitsPerPixel), containerBitsPerPixel(containerBitsPerPixel),
              bytesPerPixel(bytesPerPixel) {
            slots.reserve(kPoolSize);
            for (std::size_t i = 0; i < kPoolSize; ++i) {
                auto frame = std::make_shared<Frame>();
                frame->width = w;
                frame->height = h;
                frame->strideBytes = stride;
                frame->pixelFormat = format;
                frame->pixelFormatCode = formatCode;
                frame->bitsPerPixel = bitsPerPixel;
                frame->containerBitsPerPixel = containerBitsPerPixel;
                frame->pixels.resize(bytes);
                slots.push_back(std::move(frame));
            }
        }
        // Only the acquisition worker calls this; consumer leases are shared_ptrs.
        std::shared_ptr<Frame> tryAcquire() noexcept {
            for (const auto& slot : slots)
                if (slot.use_count() == 1) return slot;
            return {};
        }
        std::vector<std::shared_ptr<Frame>> slots;
        std::vector<std::uint8_t> discard;
        const PixelFormat pixelFormat;
        const std::uint32_t pixelFormatCode;
        const std::uint8_t bitsPerPixel;
        const std::uint8_t containerBitsPerPixel;
        const std::size_t bytesPerPixel;
    };

    std::atomic<CameraState> state{CameraState::Closed};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> sdkStarted{false};
    std::atomic<std::uint32_t> expectedExposureMicroseconds{0};
    std::atomic<bool> frameCacheFlushPending{false};
    std::atomic<std::uint32_t> activeSdkCallbacks{0};
    std::atomic<std::uint64_t> dropped{0};

    detail::AmcamEventQueue eventQueue;
    std::mutex stillCaptureMutex;
    std::condition_variable stillCaptureCondition;
    std::uint32_t pendingStillImages = 0;
    unsigned stillCaptureFailureEvent = 0;
    bool stillCapturePending = false;
    bool stillCaptureDesynchronized = false;
    std::mutex callbackWaitMutex;
    std::condition_variable callbackWaitCondition;
    std::mutex lifecycleMutex;
    std::mutex apiMutex;
    HAmcam handle = nullptr;
    AmcamCameraProfileRegistry profileRegistry;
    std::string modelName;
    std::string modelCode = "Unknown";
    std::uint64_t modelFlags = 0;
    bool hasModelDescriptor = false;
    std::optional<std::uint32_t> maximumSpeedLevel;
    std::optional<std::uint32_t> maximumFanSpeed;
    std::vector<Resolution> previewResolutions;
    std::thread worker;
    std::shared_ptr<FramePool> pool;
    FrameCallback onFrame;
    ErrorCallback onError;

    bool onWorkerThread() const noexcept { return g_acquisitionOwner == this; }

    void updateExpectedExposure(unsigned exposureMicroseconds) noexcept {
        if (exposureMicroseconds == 0) return;
        const auto previous = expectedExposureMicroseconds.exchange(
            exposureMicroseconds, std::memory_order_acq_rel);
        if (previous != exposureMicroseconds)
            eventQueue.signal();
    }

    // A successful Amcam_Stop requires both the camera-DDR and SDK-DLL
    // frame caches to be flushed before pull mode is started again.
    // Keep the obligation pending if the SDK rejects the flush.
    bool flushStoppedFrameCaches(CameraError* error) {
        if (!frameCacheFlushPending.load(std::memory_order_acquire))
            return true;

        HRESULT hr = E_FAIL;
        {
            std::lock_guard<std::mutex> lock(apiMutex);
            if (!handle) {
                return fail(error, "Amcam_OPTION_FLUSH", -1,
                            "Cannot flush stopped-frame caches without an open camera handle.");
            }
            hr = Amcam_put_Option(handle, AMCAM_OPTION_FLUSH, 3);
            if (SUCCEEDED(hr))
                frameCacheFlushPending.store(false, std::memory_order_release);
        }
        return FAILED(hr)
            ? failHr(error, "Amcam_put_Option(AMCAM_OPTION_FLUSH, 3)", hr)
            : true;
    }

    void waitForSdkCallbacks() {
        std::unique_lock<std::mutex> lock(callbackWaitMutex);
        callbackWaitCondition.wait(lock, [this]() {
            return activeSdkCallbacks.load(std::memory_order_acquire) == 0;
        });
    }

    void resetStillCaptureStateAfterStop() noexcept {
        {
            std::lock_guard<std::mutex> lock(stillCaptureMutex);
            pendingStillImages = 0;
            stillCaptureFailureEvent = 0;
            stillCapturePending = false;
            stillCaptureDesynchronized = false;
        }
        stillCaptureCondition.notify_all();
    }

    static void __stdcall sdkEvent(unsigned event, void* context) {
        auto* self = static_cast<Impl*>(context);
        if (!self) return;

        self->activeSdkCallbacks.fetch_add(1, std::memory_order_acq_rel);
        struct CallbackExit final {
            Impl* owner;
            ~CallbackExit() {
                {
                    std::lock_guard<std::mutex> lock(owner->callbackWaitMutex);
                    owner->activeSdkCallbacks.fetch_sub(1, std::memory_order_acq_rel);
                }
                owner->callbackWaitCondition.notify_all();
            }
        } callbackExit{self};

        // Still-image events use an independent waiter, never the live FIFO.
        if (event == AMCAM_EVENT_STILLIMAGE) {
            try {
                {
                    std::lock_guard<std::mutex> lock(self->stillCaptureMutex);
                    if (self->stillCapturePending &&
                        self->pendingStillImages != std::numeric_limits<std::uint32_t>::max()) {
                        ++self->pendingStillImages;
                    }
                }
                self->stillCaptureCondition.notify_all();
            } catch (...) {
                // Never unwind through the vendor callback ABI.
            }
            return;
        }

        // Fatal SDK events also wake a still request, then follow their original
        // route into the live acquisition queue.
        switch (event) {
        case AMCAM_EVENT_DISCONNECTED:
        case AMCAM_EVENT_ERROR:
        case AMCAM_EVENT_NOFRAMETIMEOUT:
        case AMCAM_EVENT_NOPACKETTIMEOUT:
            try {
                {
                    std::lock_guard<std::mutex> lock(self->stillCaptureMutex);
                    if (self->stillCapturePending &&
                        self->stillCaptureFailureEvent == 0) {
                        self->stillCaptureFailureEvent = event;
                    }
                }
                self->stillCaptureCondition.notify_all();
            } catch (...) {
                // Never unwind through the vendor callback ABI.
            }
            [[fallthrough]];
        case AMCAM_EVENT_IMAGE:
            try {
                (void)self->eventQueue.push(event);
            } catch (...) {
                self->eventQueue.stop();
            }
            break;
        default:
            break;
        }
    }

    bool reportFatal(CameraError error) noexcept {
        // A fatal transition can originate from the frame watchdog, not only
        // from an SDK callback. Wake any still transaction before stopping the
        // SDK so it cannot wait out its entire capture timeout after fault.
        try {
            {
                std::lock_guard<std::mutex> captureLock(stillCaptureMutex);
                if (stillCapturePending && stillCaptureFailureEvent == 0)
                    stillCaptureFailureEvent = AMCAM_EVENT_ERROR;
            }
            stillCaptureCondition.notify_all();
        } catch (...) {
            // The fatal live transition must still proceed.
        }

        bool expected = false;
        if (!stopRequested.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel))
            return false;

        // The acquisition error now owns the transition. Publish Faulted
        // immediately so a concurrent user Stop cannot restore Streaming.
        state.store(CameraState::Faulted, std::memory_order_release);

        // Stop the worker's event wait before asking the SDK to terminate
        // callback production. SDK callbacks can only enqueue and return.
        eventQueue.stop();

        bool stoppedSuccessfully = false;
        {
            std::lock_guard<std::mutex> lock(apiMutex);
            if (handle && sdkStarted.load(std::memory_order_acquire)) {
                const HRESULT hr = Amcam_Stop(handle);
                if (SUCCEEDED(hr)) {
                    sdkStarted.store(false, std::memory_order_release);
                    frameCacheFlushPending.store(true, std::memory_order_release);
                    stoppedSuccessfully = true;
                } else {
                    error.message += " " + hrMessage("Amcam_Stop", hr);
                }
            }
        }

        if (stoppedSuccessfully) {
            // A successful Stop ends callback production. Wait for callbacks
            // already in flight before allowing another session transition.
            waitForSdkCallbacks();
            eventQueue.clearPending();

            CameraError flushError;
            if (!flushStoppedFrameCaches(&flushError))
                error.message += " " + flushError.message;
        }

        state.store(CameraState::Faulted, std::memory_order_release);
        try {
            if (onError) onError(error);
        } catch (...) {
            // Observer exceptions must not unwind through the acquisition thread.
        }
        return true;
    }

    void acquisitionLoop(const std::shared_ptr<FramePool>& activePool) noexcept {
        g_acquisitionOwner = this;
        auto lastFrameAt = std::chrono::steady_clock::now();

        // The outer loop is important: a fatal event can race a user Stop.
        // If Stop owns the transition, reportFatal declines ownership and the
        // worker waits for the Stop outcome instead of exiting prematurely.
        for (;;) {
            bool fatal = false;
            CameraError error;

            for (;;) {
                std::uint32_t event = 0;
                auto waitResult = eventQueue.waitNextUntil(
                    lastFrameAt + detail::frameStallTimeout(
                        expectedExposureMicroseconds.load(std::memory_order_acquire)),
                    event);
                if (waitResult == detail::AmcamEventQueue::WaitResult::Timeout) {
                    // During a user-owned Stop, keep consuming/suppressing SDK
                    // events until Stop either succeeds or restores Streaming.
                    if (stopRequested.load(std::memory_order_acquire) &&
                        state.load(std::memory_order_acquire) == CameraState::Stopping) {
                        waitResult = eventQueue.waitNext(event);
                    } else {
                        const auto now = std::chrono::steady_clock::now();
                        const auto exposure =
                            expectedExposureMicroseconds.load(std::memory_order_acquire);
                        if (detail::frameStalled(now, lastFrameAt, exposure)) {
                            const auto stalledMilliseconds =
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now - lastFrameAt).count();
                            fatal = true;
                            error = {static_cast<std::int32_t>(E_TIMEOUT),
                                     "Amcam frame watchdog",
                                     "No preview frame event received for " +
                                         std::to_string(stalledMilliseconds) +
                                         " ms (last exposure " +
                                         std::to_string(exposure) +
                                         " us; watchdog threshold " +
                                         std::to_string(
                                             detail::frameStallTimeout(exposure).count()) +
                                         " ms)."};
                            break;
                        }
                        continue;
                    }
                }
                if (waitResult == detail::AmcamEventQueue::WaitResult::Signalled)
                    continue;
                if (waitResult == detail::AmcamEventQueue::WaitResult::Stopped) {
                    if (!stopRequested.load(std::memory_order_acquire)) {
                        fatal = true;
                        error = {-1, "Amcam event callback queue",
                                 "The acquisition event queue stopped unexpectedly while the camera was streaming."};
                    }
                    break;
                }

                if (waitResult == detail::AmcamEventQueue::WaitResult::Overflow) {
                    // A user Stop transition temporarily suppresses event processing.
                    // Overflow during that interval is equivalent to callbacks handled
                    // while Swift has streaming=false: discard that transient backlog.
                    if (stopRequested.load(std::memory_order_acquire) &&
                        state.load(std::memory_order_acquire) == CameraState::Stopping) {
                        eventQueue.clearPending();
                        continue;
                    }
                    fatal = true;
                    error = {-1, "Amcam event callback queue",
                             "The camera generated more pending callback events than the bounded queue can retain."};
                    break;
                }

                // Stop temporarily suppresses frame pulls but leaves the callback
                // queue accepting events. If Stop fails, streaming resumes and later
                // callbacks continue to be handled without rebuilding the session.
                if (stopRequested.load(std::memory_order_acquire) &&
                    state.load(std::memory_order_acquire) == CameraState::Stopping)
                    continue;

                if (event == AMCAM_EVENT_DISCONNECTED) {
                    fatal = true;
                    error = {static_cast<std::int32_t>(E_GEN_FAILURE),
                             "AMCAM_EVENT_DISCONNECTED", "The camera reported a disconnect."};
                    break;
                }
                if (event == AMCAM_EVENT_ERROR) {
                    fatal = true;
                    error = {static_cast<std::int32_t>(E_FAIL),
                             "AMCAM_EVENT_ERROR", "The camera SDK reported a generic device error."};
                    break;
                }
                if (event == AMCAM_EVENT_NOFRAMETIMEOUT) {
                    fatal = true;
                    error = {static_cast<std::int32_t>(E_TIMEOUT),
                             "AMCAM_EVENT_NOFRAMETIMEOUT", "The camera reported a no-frame timeout."};
                    break;
                }
                if (event == AMCAM_EVENT_NOPACKETTIMEOUT) {
                    fatal = true;
                    error = {static_cast<std::int32_t>(E_TIMEOUT),
                             "AMCAM_EVENT_NOPACKETTIMEOUT", "The camera reported a no-packet timeout."};
                    break;
                }
                if (event != AMCAM_EVENT_IMAGE)
                    continue;

                auto writable = activePool->tryAcquire();
                void* destination = writable
                    ? static_cast<void*>(writable->pixels.data())
                    : static_cast<void*>(activePool->discard.data());

                AmcamFrameInfoV4 info{};
                HRESULT hr = E_FAIL;
                bool pullSuppressedByStop = false;
                {
                    std::lock_guard<std::mutex> lock(apiMutex);
                    // Recheck after acquiring the SDK lock. A user Stop may have
                    // started after the event was dequeued but before this pull.
                    if (stopRequested.load(std::memory_order_acquire) &&
                        state.load(std::memory_order_acquire) == CameraState::Stopping) {
                        pullSuppressedByStop = true;
                    } else if (!handle) {
                        fatal = true;
                        error = {-1, "Amcam_PullImageV4", "Camera handle is no longer open."};
                    } else {
                        const int pullBits =
                            activePool->pixelFormat == PixelFormat::RGB24
                                ? kRgb24Bits : 0;
                        hr = Amcam_PullImageV4(
                            handle, destination, 0, pullBits, kPackedRows, &info);
                    }
                }
                if (pullSuppressedByStop)
                    continue;
                if (fatal)
                    break;

                // The vendor documents E_PENDING as "no image data is available
                // yet". The callback can race the frame-ready transition; this
                // event is consumed and acquisition waits for the next image event.
                if (hr == E_PENDING)
                    continue;
                if (FAILED(hr)) {
                    fatal = true;
                    error = {static_cast<std::int32_t>(hr),
                             "Amcam_PullImageV4", hrMessage("Amcam_PullImageV4", hr)};
                    break;
                }

                // Only a successfully pulled frame proves acquisition progress.
                // An IMAGE callback or E_PENDING result alone does not reset health.
                lastFrameAt = std::chrono::steady_clock::now();
                if (info.v3.expotime != 0)
                    expectedExposureMicroseconds.store(
                        info.v3.expotime, std::memory_order_release);

                // Keep draining the SDK even when all frame leases are retained.
                // Pull into the private discard buffer and account for the drop.
                if (!writable) {
                    dropped.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                const auto w = info.v3.width;
                const auto h = info.v3.height;
                const std::uint64_t stride64 =
                    static_cast<std::uint64_t>(w) * activePool->bytesPerPixel;
                if (w == 0 || h == 0 ||
                    stride64 > std::numeric_limits<std::uint32_t>::max() ||
                    stride64 > std::numeric_limits<std::uint64_t>::max() / h) {
                    fatal = true;
                    error = {-1, "Amcam_PullImageV4",
                             "SDK frame dimensions overflow the selected pixel-format layout."};
                    break;
                }
                const std::uint64_t required64 = stride64 * h;
                if (required64 > std::numeric_limits<std::size_t>::max() ||
                    required64 > writable->pixels.capacity()) {
                    fatal = true;
                    error = {-1, "Amcam_PullImageV4",
                             "SDK frame dimensions exceed the allocated pixel-format buffer."};
                    break;
                }
                const auto required = static_cast<std::size_t>(required64);

                // Keep the logical byte count equal to this frame's dimensions,
                // while retaining vector capacity for later frames of this size.
                writable->pixels.resize(required);

                writable->width = w;
                writable->height = h;
                writable->strideBytes = static_cast<std::uint32_t>(stride64);
                writable->pixelFormat = activePool->pixelFormat;
                writable->pixelFormatCode = activePool->pixelFormatCode;
                writable->bitsPerPixel = activePool->bitsPerPixel;
                writable->containerBitsPerPixel = activePool->containerBitsPerPixel;
                writable->sequence = info.v3.seq;
                writable->timestampMicroseconds = info.v3.timestamp;
                writable->sdkFlags = info.v3.flag;
                writable->exposureMicroseconds = info.v3.expotime;
                writable->exposureGainPercent = info.v3.expogain;
                writable->blackLevel = info.v3.blacklevel;

                FramePtr lease = std::move(writable);
                try {
                    if (onFrame) onFrame(std::move(lease));
                } catch (...) {
                    dropped.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (fatal) {
                if (stopRequested.load(std::memory_order_acquire) &&
                    state.load(std::memory_order_acquire) == CameraState::Stopping) {
                    // The user Stop currently owns the transition. Keep the
                    // worker attached to the live event queue until it resolves.
                    continue;
                }

                if (!reportFatal(std::move(error))) {
                    const CameraState current = state.load(std::memory_order_acquire);
                    if ((stopRequested.load(std::memory_order_acquire) &&
                         current == CameraState::Stopping) ||
                        (current == CameraState::Streaming &&
                         sdkStarted.load(std::memory_order_acquire))) {
                        // The user Stop won the atomic transition just before
                        // reportFatal. Resume/wait rather than losing the worker.
                        continue;
                    }
                }
            }

            // The queue was stopped by successful Stop, Close, or a fatal
            // transition that this worker successfully claimed.
            break;
        }

        g_acquisitionOwner = nullptr;
    }

    bool stopLocked(CameraError* error) {
        const CameraState before = state.load(std::memory_order_acquire);
        if (before == CameraState::Closed) return true;
        if (before == CameraState::Open)
            return flushStoppedFrameCaches(error);

        const bool wasStreaming = before == CameraState::Streaming;
        const bool alreadyFaulted = before == CameraState::Faulted;

        // User Stop and an acquisition fault compete for one transition token.
        // Only the winner may change the transition state or clear stopRequested.
        bool expected = false;
        const bool ownsStopTransition =
            stopRequested.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel);
        if (ownsStopTransition) {
            if (!alreadyFaulted)
                state.store(CameraState::Stopping, std::memory_order_release);
        } else {
            // A fatal acquisition event already owns this transition. Preserve
            // its fault state rather than overwriting it with Stopping/Streaming.
            state.store(CameraState::Faulted, std::memory_order_release);
        }

        // Do not stop the callback queue or join its worker until the SDK has
        // confirmed Stop. During a user-owned transition the worker suppresses
        // PullImage while callbacks continue enqueueing events.

        HRESULT hr = S_OK;
        {
            std::lock_guard<std::mutex> lock(apiMutex);
            if (handle && sdkStarted.load(std::memory_order_acquire)) {
                hr = Amcam_Stop(handle);
                if (SUCCEEDED(hr)) {
                    sdkStarted.store(false, std::memory_order_release);
                    frameCacheFlushPending.store(true, std::memory_order_release);
                }
            }
        }

        if (FAILED(hr)) {
            if (ownsStopTransition && wasStreaming) {
                // Restore the state before releasing the transition token. A
                // fatal report cannot claim ownership until stopRequested clears.
                state.store(CameraState::Streaming, std::memory_order_release);
                stopRequested.store(false, std::memory_order_release);
                // The worker may have entered its Stop-transition wait after
                // the watchdog deadline expired. Wake it to resume monitoring.
                eventQueue.signal();
            } else {
                state.store(CameraState::Faulted, std::memory_order_release);
            }
            return failHr(error, "Amcam_Stop", hr);
        }

        // A successful Stop ends callback production. Now wake/join the worker,
        // drain any callbacks already in flight, and only then release its pool.
        eventQueue.stop();
        if (worker.joinable()) worker.join();
        waitForSdkCallbacks();
        eventQueue.clearPending();
        resetStillCaptureStateAfterStop();

        pool.reset();
        onFrame = {};
        onError = {};
        const bool endedFaulted =
            !ownsStopTransition ||
            alreadyFaulted ||
            state.load(std::memory_order_acquire) == CameraState::Faulted;
        state.store(endedFaulted ? CameraState::Faulted : CameraState::Open,
                    std::memory_order_release);
        return flushStoppedFrameCaches(error);
    }
};

AmcamCamera::AmcamCamera(AmcamCameraProfileRegistry profiles)
    : impl_(std::make_unique<Impl>(std::move(profiles))) {}
AmcamCamera::~AmcamCamera() {
    if (impl_ && !impl_->onWorkerThread()) {
        CameraError ignored;
        close(&ignored);
    }
}

std::vector<CameraInfo> AmcamCamera::enumerate(CameraError* error) {
    if (error) *error = {};
    AmcamDeviceV2 devices[AMCAM_MAX]{};
    const unsigned count = std::min<unsigned>(Amcam_EnumV2(devices), AMCAM_MAX);
    std::vector<CameraInfo> result;
    result.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const auto& device = devices[i];
        CameraInfo info;
        info.id = boundedString(device.id, sizeof(device.id));
        info.displayName = boundedString(device.displayname, sizeof(device.displayname));
        if (device.model) {
            info.hasModelDescriptor = true;
            if (device.model->name) {
                info.modelName = device.model->name;
                info.modelCode = info.modelName;
            }
            info.modelFlags = device.model->flag;
            info.maximumSpeedLevel = device.model->maxspeed;
            if ((device.model->flag & AMCAM_FLAG_FAN) != 0)
                info.maximumFanSpeed = device.model->maxfanspeed;
            const unsigned n = std::min<unsigned>(device.model->preview, 16U);
            for (unsigned r = 0; r < n; ++r)
                info.previewResolutions.push_back(
                    {device.model->res[r].width, device.model->res[r].height});
        }
        result.push_back(std::move(info));
    }
    return result;
}

bool AmcamCamera::open(const std::string& deviceId, CameraError* error) {
    if (error) *error = {};
    if (impl_->onWorkerThread())
        return fail(error, "open", -1, "open() cannot run on the acquisition callback thread.");
    if (deviceId.empty())
        return fail(error, "open", -1, "A camera id returned by enumerate() is required.");

    // Model metadata is refreshed by opaque ID. Never carry it across a replug
    // or assume the caller's earlier enumeration is still current.
    const auto discovered = AmcamCamera::enumerate();
    const auto selected = std::find_if(discovered.begin(), discovered.end(),
        [&deviceId](const CameraInfo& item) { return item.id == deviceId; });
    if (selected == discovered.end())
        return fail(error, "open", -1,
                    "The selected camera is no longer enumerated. Refresh the device list.");

    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    if (impl_->state.load(std::memory_order_acquire) != CameraState::Closed)
        return fail(error, "open", -1, "The camera must be closed before opening.");

    std::lock_guard<std::mutex> api(impl_->apiMutex);
    impl_->handle = Amcam_Open(deviceId.c_str());
    if (!impl_->handle)
        return fail(error, "Amcam_Open", -1,
                    "Amcam_Open failed. Re-enumerate devices and check that the camera is not in use.");

    impl_->modelName = selected->modelName;
    impl_->modelCode = selected->modelCode;
    impl_->modelFlags = selected->modelFlags;
    impl_->hasModelDescriptor = selected->hasModelDescriptor;
    impl_->maximumSpeedLevel = selected->maximumSpeedLevel;
    impl_->maximumFanSpeed = selected->maximumFanSpeed;
    impl_->previewResolutions = selected->previewResolutions;

    impl_->stopRequested.store(true, std::memory_order_release);
    impl_->eventQueue.stop();
    impl_->sdkStarted.store(false, std::memory_order_release);
    impl_->frameCacheFlushPending.store(false, std::memory_order_release);
    impl_->dropped.store(0, std::memory_order_relaxed);
    impl_->state.store(CameraState::Open, std::memory_order_release);
    return true;
}

bool AmcamCamera::close(CameraError* error) {
    if (error) *error = {};
    if (impl_->onWorkerThread())
        return fail(error, "close", -1,
                    "close() cannot run on the acquisition callback thread; queue it to the owner thread.");

    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    if (impl_->state.load(std::memory_order_acquire) == CameraState::Closed)
        return true;

    // Close is terminal. Do not make it depend on Stop or the recoverable
    // post-Stop cache flush: Amcam_Close ends SDK ownership of this handle.
    // The callback-driven PullImage worker must finish before handle teardown.
    impl_->state.store(CameraState::Stopping, std::memory_order_release);
    impl_->stopRequested.store(true, std::memory_order_release);
    impl_->eventQueue.stop();
    if (impl_->worker.joinable())
        impl_->worker.join();

    {
        std::lock_guard<std::mutex> api(impl_->apiMutex);
        if (impl_->handle) {
            Amcam_Close(impl_->handle);
            impl_->handle = nullptr;
        }

        impl_->sdkStarted.store(false, std::memory_order_release);
        impl_->frameCacheFlushPending.store(false, std::memory_order_release);

        // Model metadata is read by feature access under apiMutex.
        impl_->modelName.clear();
        impl_->modelCode = "Unknown";
        impl_->modelFlags = 0;
        impl_->hasModelDescriptor = false;
        impl_->maximumSpeedLevel.reset();
        impl_->maximumFanSpeed.reset();
        impl_->previewResolutions.clear();
    }

    // Amcam_Close ends callback production. Wait for callbacks that entered
    // before Close returned before releasing any callback/session state.
    impl_->waitForSdkCallbacks();
    impl_->eventQueue.clearPending();
    impl_->resetStillCaptureStateAfterStop();
    impl_->pool.reset();
    impl_->onFrame = {};
    impl_->onError = {};
    impl_->state.store(CameraState::Closed, std::memory_order_release);
    return true;
}

bool AmcamCamera::start(FrameCallback onFrame, ErrorCallback onError,
                        CameraError* error) {
    if (error) *error = {};
    if (impl_->onWorkerThread())
        return fail(error, "start", -1, "start() cannot run on the acquisition callback thread.");
    if (!onFrame) return fail(error, "start", -1, "A frame callback is required.");

    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
        return fail(error, "start", -1, "The camera must be open and stopped.");

    // Retry a previous post-Stop flush before starting pull mode again.
    if (!impl_->flushStoppedFrameCaches(error))
        return false;

    impl_->state.store(CameraState::Starting, std::memory_order_release);
    impl_->eventQueue.reset();
    impl_->resetStillCaptureStateAfterStop();
    impl_->stopRequested.store(false, std::memory_order_release);
    impl_->dropped.store(0, std::memory_order_relaxed);

    int width = 0, height = 0;
    bool rawOutput = false;
    bool rawPolicyDenied = false;
    unsigned rawFourCC = 0;
    unsigned rawBitsPerPixel = 0;
    int rawStorageMode = 0;
    HRESULT hr = S_OK;
    {
        std::lock_guard<std::mutex> api(impl_->apiMutex);
        if (!impl_->handle) {
            impl_->state.store(CameraState::Closed, std::memory_order_release);
            return fail(error, "start", -1, "Camera handle is not open.");
        }
        hr = Amcam_get_FinalSize(impl_->handle, &width, &height);
        if (SUCCEEDED(hr)) {
            unsigned exposureMicroseconds = 0;
            if (SUCCEEDED(Amcam_get_ExpoTime(
                    impl_->handle, &exposureMicroseconds))) {
                impl_->expectedExposureMicroseconds.store(
                    exposureMicroseconds, std::memory_order_release);
            } else {
                impl_->expectedExposureMicroseconds.store(
                    0, std::memory_order_release);
            }
            int rawOption = 0;
            const HRESULT rawHr =
                Amcam_get_Option(impl_->handle, AMCAM_OPTION_RAW, &rawOption);
            constexpr std::uint64_t rawCapabilityFlags =
                AMCAM_FLAG_RAW8 | AMCAM_FLAG_RAW10 | AMCAM_FLAG_RAW11 |
                AMCAM_FLAG_RAW12 | AMCAM_FLAG_RAW14 | AMCAM_FLAG_RAW16 |
                AMCAM_FLAG_RAW12PACK;
            const bool modelAdvertisesRaw =
                (impl_->modelFlags & rawCapabilityFlags) != 0;
            if (SUCCEEDED(rawHr)) {
                if (rawOption != 0 && rawOption != 1) {
                    hr = E_INVALIDARG;
                } else if (rawOption == 1) {
                    rawOutput = true;
                    hr = Amcam_get_RawFormat(
                        impl_->handle, &rawFourCC, &rawBitsPerPixel);
                    if (SUCCEEDED(hr) &&
                        (rawBitsPerPixel == 0 || rawBitsPerPixel > 16)) {
                        hr = E_INVALIDARG;
                    }
                    if (SUCCEEDED(hr) &&
                        !impl_->profileRegistry.allows(
                            impl_->modelCode, AmcamCameraFeature::RawOutput)) {
                        rawPolicyDenied = true;
                    }
                    if (SUCCEEDED(hr)) {
                        hr = Amcam_get_Option(
                            impl_->handle, AMCAM_OPTION_BITDEPTH, &rawStorageMode);
                        if (SUCCEEDED(hr) &&
                            rawStorageMode != 0 && rawStorageMode != 1) {
                            hr = E_INVALIDARG;
                        }
                    }
                }
            } else if (modelAdvertisesRaw) {
                hr = rawHr;
            }
        }
    }
    if (rawPolicyDenied) {
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return fail(error, "start", -1,
                    "RAW output is disabled by the selected model profile.");
    }
    if (FAILED(hr)) {
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return failHr(error, rawOutput ? "Amcam_get_RawFormat" : "Amcam_get_FinalSize", hr);
    }

    if (width <= 0 || height <= 0) {
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return fail(error, "start", -1, "The SDK returned invalid final image dimensions.");
    }

    const std::uint8_t containerBitsPerPixel =
        !rawOutput ? 24U : (rawStorageMode == 0 ? 8U : 16U);
    const std::size_t bytesPerPixel = containerBitsPerPixel / 8U;
    const std::uint64_t stride64 =
        static_cast<std::uint64_t>(width) * bytesPerPixel;
    if (stride64 > std::numeric_limits<std::uint32_t>::max() ||
        stride64 > std::numeric_limits<std::uint64_t>::max() /
                       static_cast<std::uint64_t>(height)) {
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return fail(error, "start", -1,
                    "The selected pixel format produces an overflowing frame layout.");
    }
    const std::uint64_t bytes64 = stride64 * static_cast<std::uint64_t>(height);
    if (bytes64 == 0 || bytes64 > std::numeric_limits<std::size_t>::max() ||
        bytes64 > 512ULL * 1024ULL * 1024ULL) {
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return fail(error, "start", -1,
                    "The selected pixel format produces an invalid or excessively large frame buffer.");
    }
    const auto strideBytes = static_cast<std::uint32_t>(stride64);
    const auto bytes = static_cast<std::size_t>(bytes64);
    const PixelFormat pixelFormat = !rawOutput ? PixelFormat::RGB24 :
        (containerBitsPerPixel == 8U ? PixelFormat::Raw8 : PixelFormat::Raw16);
    const auto frameBitsPerPixel = static_cast<std::uint8_t>(
        rawOutput ? rawBitsPerPixel : 24U);

    try {
        impl_->pool = std::make_shared<Impl::FramePool>(
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            strideBytes, bytes, pixelFormat,
            rawOutput ? rawFourCC : 0U,
            frameBitsPerPixel, containerBitsPerPixel, bytesPerPixel);
    } catch (const std::exception& ex) {
        impl_->pool.reset();
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return fail(error, "start", -1,
                    std::string("Frame pool allocation failed: ") + ex.what());
    }

    impl_->onFrame = std::move(onFrame);
    impl_->onError = std::move(onError);

    {
        std::lock_guard<std::mutex> api(impl_->apiMutex);
        hr = Amcam_put_Option(impl_->handle, AMCAM_OPTION_BYTEORDER, 0);
        if (FAILED(hr)) {
            impl_->eventQueue.stop();
            impl_->pool.reset();
            impl_->onFrame = {};
            impl_->onError = {};
            impl_->state.store(CameraState::Open, std::memory_order_release);
            return failHr(error, "Amcam_put_Option(AMCAM_OPTION_BYTEORDER)", hr);
        }
        hr = Amcam_StartPullModeWithCallback(
            impl_->handle, &Impl::sdkEvent, impl_.get());
    }
    if (FAILED(hr)) {
        impl_->eventQueue.stop();
        impl_->waitForSdkCallbacks();
        impl_->eventQueue.clearPending();
        impl_->pool.reset();
        impl_->onFrame = {};
        impl_->onError = {};
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return failHr(error, "Amcam_StartPullModeWithCallback", hr);
    }

    impl_->sdkStarted.store(true, std::memory_order_release);
    impl_->state.store(CameraState::Streaming, std::memory_order_release);
    try {
        auto activePool = impl_->pool;
        impl_->worker = std::thread([p = impl_.get(), activePool]() {
            p->acquisitionLoop(activePool);
        });
    } catch (const std::exception& ex) {
        impl_->stopRequested.store(true, std::memory_order_release);
        impl_->eventQueue.stop();
        HRESULT stopHr = S_OK;
        {
            std::lock_guard<std::mutex> api(impl_->apiMutex);
            if (impl_->handle && impl_->sdkStarted.load(std::memory_order_acquire)) {
                stopHr = Amcam_Stop(impl_->handle);
                if (SUCCEEDED(stopHr)) {
                    impl_->sdkStarted.store(false, std::memory_order_release);
                    impl_->frameCacheFlushPending.store(true, std::memory_order_release);
                }
            }
        }
        if (SUCCEEDED(stopHr)) {
            impl_->waitForSdkCallbacks();
            impl_->eventQueue.clearPending();
        }
        impl_->pool.reset();
        impl_->onFrame = {};
        impl_->onError = {};
        impl_->state.store(FAILED(stopHr) ? CameraState::Faulted : CameraState::Open,
                           std::memory_order_release);
        std::string message = std::string("Could not create acquisition worker: ") + ex.what();
        if (FAILED(stopHr)) {
            message += " " + hrMessage("Amcam_Stop", stopHr);
        } else {
            CameraError flushError;
            if (!impl_->flushStoppedFrameCaches(&flushError))
                message += " " + flushError.message;
        }
        return fail(error, "start", -1, std::move(message));
    }
    return true;
}

bool AmcamCamera::stop(CameraError* error) {
    if (error) *error = {};
    if (impl_->onWorkerThread())
        return fail(error, "stop", -1,
                    "stop() cannot run on the acquisition callback thread; queue it to the owner thread.");
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    return impl_->stopLocked(error);
}

CameraState AmcamCamera::state() const noexcept {
    return impl_->state.load(std::memory_order_acquire);
}
std::uint64_t AmcamCamera::droppedFrames() const noexcept {
    return impl_->dropped.load(std::memory_order_relaxed);
}

bool AmcamCamera::setResolution(std::uint32_t index, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    if (!impl_->handle || impl_->state.load(std::memory_order_acquire) != CameraState::Open)
        return fail(error, "setResolution", -1,
                    "Resolution can only change while the camera is open and stopped.");
    const HRESULT hr = Amcam_put_eSize(impl_->handle, index);
    return FAILED(hr) ? failHr(error, "Amcam_put_eSize", hr) : true;
}

bool AmcamCamera::setAutoExposure(bool enabled, CameraError* error) {
    return setAutoExposureMode(
        enabled ? AmcamAutoExposureMode::Continuous : AmcamAutoExposureMode::Off,
        error);
}

bool AmcamCamera::setAutoExposureMode(AmcamAutoExposureMode mode, CameraError* error) {
    if (error) *error = {};
    const auto rawMode = static_cast<std::int32_t>(mode);
    if (rawMode < 0 || rawMode > 2)
        return fail(error, "setAutoExposureMode", -1, "Invalid Amcam auto-exposure mode.");

    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "setAutoExposureMode", -1, "The camera must be open.");

    HRESULT hr = Amcam_put_AutoExpoEnable(impl_->handle, rawMode);
    if (FAILED(hr)) return failHr(error, "Amcam_put_AutoExpoEnable", hr);
    int readback = -1;
    hr = Amcam_get_AutoExpoEnable(impl_->handle, &readback);
    if (FAILED(hr)) return failHr(error, "Amcam_get_AutoExpoEnable", hr);
    if (readback != rawMode)
        return fail(error, "setAutoExposureMode", -1,
                    "Auto-exposure readback did not match the requested mode.");
    unsigned exposureMicroseconds = 0;
    if (SUCCEEDED(Amcam_get_ExpoTime(impl_->handle, &exposureMicroseconds)))
        impl_->updateExpectedExposure(exposureMicroseconds);
    return true;
}

bool AmcamCamera::setExposureTime(std::uint32_t time, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "setExposureTime", -1, "The camera must be open.");

    int autoMode = -1;
    HRESULT hr = Amcam_get_AutoExpoEnable(impl_->handle, &autoMode);
    if (FAILED(hr)) return failHr(error, "Amcam_get_AutoExpoEnable", hr);
    if (autoMode != static_cast<int>(AmcamAutoExposureMode::Off))
        return fail(error, "setExposureTime", -1,
                    "Exposure time is writable only when automatic exposure is Off.");

    unsigned minimum = 0, maximum = 0, defaultValue = 0;
    hr = Amcam_get_ExpTimeRange(impl_->handle, &minimum, &maximum, &defaultValue);
    if (FAILED(hr)) return failHr(error, "Amcam_get_ExpTimeRange", hr);
    if (minimum == 0 || maximum < minimum || time < minimum || time > maximum)
        return fail(error, "setExposureTime", -1,
                    "Requested exposure time is outside the current camera range.");

    hr = Amcam_put_ExpoTime(impl_->handle, time);
    if (FAILED(hr)) return failHr(error, "Amcam_put_ExpoTime", hr);
    unsigned readback = 0;
    hr = Amcam_get_ExpoTime(impl_->handle, &readback);
    if (FAILED(hr)) return failHr(error, "Amcam_get_ExpoTime", hr);
    if (readback != time)
        return fail(error, "setExposureTime", -1,
                    "Exposure-time readback did not match the requested value.");
    impl_->updateExpectedExposure(readback);
    return true;
}

bool AmcamCamera::setExposureGain(std::uint16_t gain, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "setExposureGain", -1, "The camera must be open.");

    unsigned short minimum = 0, maximum = 0, defaultValue = 0;
    HRESULT hr = Amcam_get_ExpoAGainRange(impl_->handle, &minimum, &maximum, &defaultValue);
    if (FAILED(hr)) return failHr(error, "Amcam_get_ExpoAGainRange", hr);
    if (maximum < minimum || gain < minimum || gain > maximum)
        return fail(error, "setExposureGain", -1,
                    "Requested analog gain is outside the current camera range.");

    hr = Amcam_put_ExpoAGain(impl_->handle, gain);
    if (FAILED(hr)) return failHr(error, "Amcam_put_ExpoAGain", hr);
    unsigned short readback = 0;
    hr = Amcam_get_ExpoAGain(impl_->handle, &readback);
    if (FAILED(hr)) return failHr(error, "Amcam_get_ExpoAGain", hr);
    if (readback != gain)
        return fail(error, "setExposureGain", -1,
                    "Analog-gain readback did not match the requested value.");
    return true;
}

bool AmcamCamera::readExposure(ExposureSettings& settings, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "readExposure", -1, "The camera must be open.");

    int automatic = 0;
    unsigned time = 0, minTime = 0, maxTime = 0, defaultTime = 0;
    unsigned short gain = 0, minGain = 0, maxGain = 0, defaultGain = 0;
    HRESULT hr = Amcam_get_AutoExpoEnable(impl_->handle, &automatic);
    if (FAILED(hr)) return failHr(error, "Amcam_get_AutoExpoEnable", hr);
    hr = Amcam_get_ExpoTime(impl_->handle, &time);
    if (FAILED(hr)) return failHr(error, "Amcam_get_ExpoTime", hr);
    hr = Amcam_get_ExpTimeRange(impl_->handle, &minTime, &maxTime, &defaultTime);
    if (FAILED(hr)) return failHr(error, "Amcam_get_ExpTimeRange", hr);
    hr = Amcam_get_ExpoAGain(impl_->handle, &gain);
    if (FAILED(hr)) return failHr(error, "Amcam_get_ExpoAGain", hr);
    hr = Amcam_get_ExpoAGainRange(impl_->handle, &minGain, &maxGain, &defaultGain);
    if (FAILED(hr)) return failHr(error, "Amcam_get_ExpoAGainRange", hr);

    if (automatic < 0 || automatic > 2)
        return fail(error, "Amcam_get_AutoExpoEnable", -1,
                    "The camera returned an unknown auto-exposure mode.");
    settings.autoMode = static_cast<AmcamAutoExposureMode>(automatic);
    settings.automatic = isAutomaticExposureMode(settings.autoMode);
    settings.timeMicroseconds = time;
    settings.minimumTimeMicroseconds = minTime;
    settings.maximumTimeMicroseconds = maxTime;
    settings.defaultTimeMicroseconds = defaultTime;
    settings.gainPercent = gain;
    settings.minimumGainPercent = minGain;
    settings.maximumGainPercent = maxGain;
    settings.defaultGainPercent = defaultGain;
    impl_->updateExpectedExposure(time);
    return true;
}

bool AmcamCamera::setWhiteBalance(int temperature, int tint, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "setWhiteBalance", -1, "The camera must be open.");
    const HRESULT hr = Amcam_put_TempTint(impl_->handle, temperature, tint);
    return FAILED(hr) ? failHr(error, "Amcam_put_TempTint", hr) : true;
}

bool AmcamCamera::readWhiteBalance(WhiteBalanceSettings& settings, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "readWhiteBalance", -1, "The camera must be open.");
    const HRESULT hr = Amcam_get_TempTint(
        impl_->handle, &settings.temperature, &settings.tint);
    return FAILED(hr) ? failHr(error, "Amcam_get_TempTint", hr) : true;
}

bool AmcamCamera::autoWhiteBalance(CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    if (!impl_->profileRegistry.allows(
            impl_->modelCode, AmcamCameraFeature::WhiteBalance))
        return fail(error, "autoWhiteBalance", -1,
                    "Auto white balance is disabled by the selected model profile.");
    if (!impl_->handle ||
        impl_->state.load(std::memory_order_acquire) != CameraState::Streaming)
        return fail(error, "autoWhiteBalance", -1,
                    "The SDK requires auto white balance after acquisition starts.");
    const HRESULT hr = Amcam_AwbOnce(impl_->handle, nullptr, nullptr);
    return FAILED(hr) ? failHr(error, "Amcam_AwbOnce", hr) : true;
}

bool AmcamCamera::readFrameRate(FrameRateStatistics& stats, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    if (!impl_->handle ||
        impl_->state.load(std::memory_order_acquire) != CameraState::Streaming)
        return fail(error, "readFrameRate", -1, "Acquisition must be running.");
    unsigned frames = 0, milliseconds = 0, total = 0;
    const HRESULT hr = Amcam_get_FrameRate(impl_->handle, &frames, &milliseconds, &total);
    if (FAILED(hr)) return failHr(error, "Amcam_get_FrameRate", hr);
    stats.framesPerSecond = milliseconds == 0 ? 0.0 :
        static_cast<double>(frames) * 1000.0 / static_cast<double>(milliseconds);
    stats.totalFrames = total;
    return true;
}

bool AmcamCamera::readStillResolutions(
    std::vector<StillResolution>& resolutions,
    CameraError* error) {
    if (error) *error = {};
    resolutions.clear();

    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const CameraState currentState = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle ||
        (currentState != CameraState::Open &&
         currentState != CameraState::Streaming)) {
        return fail(error, "readStillResolutions", -1,
                    "The camera must be open to query still-image resolutions.");
    }
    if (!impl_->profileRegistry.allows(
            impl_->modelCode, AmcamCameraFeature::StillCapture)) {
        return fail(error, "readStillResolutions", -1,
                    "Still capture is disabled by the selected model profile.");
    }

    const HRESULT countResult = Amcam_get_StillResolutionNumber(impl_->handle);
    if (FAILED(countResult))
        return failHr(error, "Amcam_get_StillResolutionNumber", countResult);

    const unsigned count = static_cast<unsigned>(countResult);
    if (count > 256U) {
        return fail(error, "Amcam_get_StillResolutionNumber", -1,
                    "The SDK reported an unreasonable still-resolution count.");
    }

    try {
        resolutions.reserve(count);
        for (unsigned index = 0; index < count; ++index) {
            int width = 0;
            int height = 0;
            const HRESULT hr = Amcam_get_StillResolution(
                impl_->handle, index, &width, &height);
            if (FAILED(hr))
                return failHr(error, "Amcam_get_StillResolution", hr);
            if (width <= 0 || height <= 0) {
                return fail(error, "Amcam_get_StillResolution", -1,
                            "The SDK returned an invalid still-resolution entry.");
            }
            resolutions.push_back(StillResolution{
                index,
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height)
            });
        }
    } catch (const std::exception& exception) {
        resolutions.clear();
        return fail(error, "readStillResolutions", -1,
                    std::string("Still-resolution list allocation failed: ") +
                        exception.what());
    }

    return true;
}
 

bool AmcamCamera::captureStill(
    const StillCaptureRequest& request,
    FramePtr& frame,
    CameraError* error) {
    if (error) *error = {};
    frame.reset();

    if (impl_->onWorkerThread()) {
        return fail(error, "captureStill", -1,
                    "captureStill() cannot run from the live acquisition callback; submit it through the camera owner.");
    }
    if (request.timeoutMilliseconds == 0) {
        return fail(error, "captureStill", -1,
                    "A positive still-capture timeout is required.");
    }
    if (request.mode != StillCaptureMode::Processed &&
        request.mode != StillCaptureMode::Raw) {
        return fail(error, "captureStill", -1,
                    "The requested still-capture mode is invalid.");
    }

    // This excludes Start/Stop/Close for the duration of the still request.
    // The live acquisition worker continues consuming its normal event queue.
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    if (!impl_->handle ||
        impl_->state.load(std::memory_order_acquire) != CameraState::Streaming ||
        !impl_->sdkStarted.load(std::memory_order_acquire)) {
        return fail(error, "captureStill", -1,
                    "Still capture requires live acquisition to be running.");
    }
    if (!impl_->profileRegistry.allows(
            impl_->modelCode, AmcamCameraFeature::StillCapture)) {
        return fail(error, "captureStill", -1,
                    "Still capture is disabled by the selected model profile.");
    }
    if (request.mode == StillCaptureMode::Raw &&
        !impl_->profileRegistry.allows(
            impl_->modelCode, AmcamCameraFeature::RawOutput)) {
        return fail(error, "captureStill", -1,
                    "RAW still capture is disabled by the selected model profile.");
    }

    constexpr std::uint32_t kCurrentPreviewResolution =
        std::numeric_limits<std::uint32_t>::max();
    unsigned stillResolutionCount = 0;
    int expectedWidth = 0;
    int expectedHeight = 0;
    unsigned rawFourCC = 0;
    unsigned rawBitsPerPixel = 0;
    std::uint32_t selectedStillResolutionIndex = request.resolutionIndex;
    int rawStorageMode = 0;
    {
        std::lock_guard<std::mutex> api(impl_->apiMutex);
        if (!impl_->handle) {
            return fail(error, "captureStill", -1,
                        "Camera handle is no longer open.");
        }

        const HRESULT countResult =
            Amcam_get_StillResolutionNumber(impl_->handle);
        if (FAILED(countResult)) {
            return failHr(error, "Amcam_get_StillResolutionNumber", countResult);
        }
        stillResolutionCount = static_cast<unsigned>(countResult);
        if (stillResolutionCount == 0) {
            return fail(error, "captureStill", -1,
                        "The camera does not report still-image capture resolutions.");
        }

        if (request.resolutionIndex == kCurrentPreviewResolution) {
            int previewWidth = 0;
            int previewHeight = 0;
            const HRESULT previewSizeResult =
                Amcam_get_FinalSize(impl_->handle, &previewWidth, &previewHeight);
            if (FAILED(previewSizeResult)) {
                return failHr(error, "Amcam_get_FinalSize", previewSizeResult);
            }
            if (previewWidth <= 0 || previewHeight <= 0) {
                return fail(error, "captureStill", -1,
                            "The SDK returned invalid live-preview dimensions.");
            }

            bool matched = false;
            for (unsigned index = 0; index < stillResolutionCount; ++index) {
                int candidateWidth = 0;
                int candidateHeight = 0;
                const HRESULT candidateResult = Amcam_get_StillResolution(
                    impl_->handle, index, &candidateWidth, &candidateHeight);
                if (FAILED(candidateResult)) {
                    return failHr(error, "Amcam_get_StillResolution", candidateResult);
                }
                if (candidateWidth <= 0 || candidateHeight <= 0) {
                    return fail(error, "Amcam_get_StillResolution", -1,
                                "The SDK returned an invalid still-resolution entry.");
                }
                if (candidateWidth == previewWidth &&
                    candidateHeight == previewHeight) {
                    selectedStillResolutionIndex = index;
                    expectedWidth = candidateWidth;
                    expectedHeight = candidateHeight;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return fail(error, "captureStill", -1,
                            "No still-resolution entry matches the current preview dimensions. Select an explicit still-resolution index.");
            }
        } else {
            if (request.resolutionIndex >= stillResolutionCount) {
                return fail(error, "captureStill", -1,
                            "The requested still-resolution index is outside the camera-reported range.");
            }
            const HRESULT resolutionResult = Amcam_get_StillResolution(
                impl_->handle, request.resolutionIndex,
                &expectedWidth, &expectedHeight);
            if (FAILED(resolutionResult)) {
                return failHr(error, "Amcam_get_StillResolution", resolutionResult);
            }
            if (expectedWidth <= 0 || expectedHeight <= 0) {
                return fail(error, "Amcam_get_StillResolution", -1,
                            "The SDK returned invalid still-image dimensions.");
            }
        }

        if (request.mode == StillCaptureMode::Raw) {
            constexpr std::uint64_t rawCapabilityFlags =
                AMCAM_FLAG_RAW8 | AMCAM_FLAG_RAW10 | AMCAM_FLAG_RAW11 |
                AMCAM_FLAG_RAW12 | AMCAM_FLAG_RAW14 | AMCAM_FLAG_RAW16 |
                AMCAM_FLAG_RAW12PACK;
            if (!impl_->hasModelDescriptor ||
                (impl_->modelFlags & rawCapabilityFlags) == 0) {
                return fail(error, "captureStill", -1,
                            "The camera model does not advertise RAW sensor output.");
            }
            const HRESULT rawFormatResult =
                Amcam_get_RawFormat(impl_->handle, &rawFourCC, &rawBitsPerPixel);
            if (FAILED(rawFormatResult)) {
                return failHr(error, "Amcam_get_RawFormat", rawFormatResult);
            }
            if (rawBitsPerPixel == 0 || rawBitsPerPixel > 16) {
                return fail(error, "Amcam_get_RawFormat", -1,
                            "The SDK returned an unsupported RAW sensor bit depth.");
            }
            const HRESULT bitDepthResult = Amcam_get_Option(
                impl_->handle, AMCAM_OPTION_BITDEPTH, &rawStorageMode);
            if (FAILED(bitDepthResult)) {
                return failHr(error, "Amcam_get_Option(BITDEPTH)", bitDepthResult);
            }
            if (rawStorageMode != 0 && rawStorageMode != 1) {
                return fail(error, "Amcam_get_Option(BITDEPTH)", -1,
                            "The SDK returned an unknown RAW storage bit-depth mode.");
            }
        }
    }

    {
        std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
        if (impl_->stillCaptureDesynchronized) {
            return fail(error, "captureStill", -1,
                        "A previous still capture timed out or failed. Stop and restart acquisition before capturing again.");
        }
        if (impl_->stillCapturePending) {
            return fail(error, "captureStill", -1,
                        "Another still-image request is already pending.");
        }
        impl_->pendingStillImages = 0;
        impl_->stillCaptureFailureEvent = 0;
        impl_->stillCapturePending = true;
    }

    HRESULT snapResult = E_FAIL;
    {
        std::lock_guard<std::mutex> api(impl_->apiMutex);
        if (!impl_->handle ||
            impl_->state.load(std::memory_order_acquire) != CameraState::Streaming) {
            std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
            impl_->stillCapturePending = false;
            impl_->stillCaptureDesynchronized = true;
            return fail(error, "captureStill", -1,
                        "The camera stopped before the still-image request could be submitted.");
        }
        snapResult = request.mode == StillCaptureMode::Raw
            ? Amcam_SnapR(impl_->handle, selectedStillResolutionIndex, 1)
            : Amcam_SnapN(impl_->handle, selectedStillResolutionIndex, 1);
    }
    if (FAILED(snapResult)) {
        {
            std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
            impl_->stillCapturePending = false;
            impl_->pendingStillImages = 0;
            impl_->stillCaptureDesynchronized = true;
        }
        impl_->stillCaptureCondition.notify_all();
        return failHr(error,
                      request.mode == StillCaptureMode::Raw
                          ? "Amcam_SnapR" : "Amcam_SnapN",
                      snapResult);
    }

    unsigned failureEvent = 0;
    {
        std::unique_lock<std::mutex> captureLock(impl_->stillCaptureMutex);
        const bool signalled = impl_->stillCaptureCondition.wait_for(
            captureLock,
            std::chrono::milliseconds(request.timeoutMilliseconds),
            [this]() {
                return impl_->pendingStillImages != 0 ||
                       impl_->stillCaptureFailureEvent != 0;
            });
        if (!signalled) {
            impl_->stillCapturePending = false;
            impl_->stillCaptureDesynchronized = true;
            return fail(error, "captureStill", static_cast<std::int32_t>(E_TIMEOUT),
                        "Timed out waiting for AMCAM_EVENT_STILLIMAGE. Stop and restart acquisition before retrying.");
        }
        failureEvent = impl_->stillCaptureFailureEvent;
        if (failureEvent == 0 && impl_->pendingStillImages != 0) {
            --impl_->pendingStillImages;
        }
        impl_->pendingStillImages = 0;
        impl_->stillCapturePending = false;
        if (failureEvent != 0) {
            impl_->stillCaptureDesynchronized = true;
        }
    }

    if (failureEvent != 0) {
        switch (failureEvent) {
        case AMCAM_EVENT_DISCONNECTED:
            return fail(error, "AMCAM_EVENT_DISCONNECTED", static_cast<std::int32_t>(E_GEN_FAILURE),
                        "The camera disconnected while a still image was being captured.");
        case AMCAM_EVENT_NOFRAMETIMEOUT:
            return fail(error, "AMCAM_EVENT_NOFRAMETIMEOUT", static_cast<std::int32_t>(E_TIMEOUT),
                        "The camera reported a no-frame timeout during still capture.");
        case AMCAM_EVENT_NOPACKETTIMEOUT:
            return fail(error, "AMCAM_EVENT_NOPACKETTIMEOUT", static_cast<std::int32_t>(E_TIMEOUT),
                        "The camera reported a no-packet timeout during still capture.");
        default:
            return fail(error, "AMCAM_EVENT_ERROR", static_cast<std::int32_t>(E_FAIL),
                        "The camera SDK reported an error during still capture.");
        }
    }

    const bool raw = request.mode == StillCaptureMode::Raw;
    const int pullBits = raw ? 0 : kRgb24Bits;
    AmcamFrameInfoV4 info{};
    FramePtr completedFrame;
    {
        std::lock_guard<std::mutex> api(impl_->apiMutex);
        if (!impl_->handle ||
            impl_->state.load(std::memory_order_acquire) != CameraState::Streaming) {
            std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
            impl_->stillCaptureDesynchronized = true;
            return fail(error, "captureStill", -1,
                        "The camera stopped before the still image could be pulled.");
        }

        // The vendor still-image examples query dimensions using a null buffer,
        // then pull the image into a format-sized output buffer.
        HRESULT pullResult = Amcam_PullImageV4(
            impl_->handle, nullptr, 1, pullBits, 0, &info);
        if (FAILED(pullResult)) {
            std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
            impl_->stillCaptureDesynchronized = true;
            return failHr(error, "Amcam_PullImageV4(still metadata)", pullResult);
        }

        if (info.v3.width == 0 || info.v3.height == 0 ||
            (expectedWidth != 0 &&
             (info.v3.width != static_cast<unsigned>(expectedWidth) ||
              info.v3.height != static_cast<unsigned>(expectedHeight)))) {
            std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
            impl_->stillCaptureDesynchronized = true;
            return fail(error, "Amcam_PullImageV4(still metadata)", -1,
                        "The still-image dimensions are invalid or differ from the selected still resolution.");
        }

        const std::uint8_t containerBitsPerPixel =
            !raw ? 24U : (rawStorageMode == 0 ? 8U : 16U);
        const std::uint64_t bytesPerPixel = containerBitsPerPixel / 8U;
        const std::uint64_t stride64 =
            static_cast<std::uint64_t>(info.v3.width) * bytesPerPixel;
        if (stride64 > std::numeric_limits<std::uint32_t>::max() ||
            stride64 > std::numeric_limits<std::uint64_t>::max() / info.v3.height) {
            std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
            impl_->stillCaptureDesynchronized = true;
            return fail(error, "captureStill", -1,
                        "The still-image layout overflows the supported frame representation.");
        }
        const std::uint64_t byteCount64 = stride64 * info.v3.height;
        if (byteCount64 == 0 ||
            byteCount64 > std::numeric_limits<std::size_t>::max() ||
            byteCount64 > 512ULL * 1024ULL * 1024ULL) {
            std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
            impl_->stillCaptureDesynchronized = true;
            return fail(error, "captureStill", -1,
                        "The still-image payload exceeds the supported allocation limit.");
        }

        try {
            auto mutableFrame = std::make_shared<Frame>();
            mutableFrame->width = info.v3.width;
            mutableFrame->height = info.v3.height;
            mutableFrame->strideBytes = static_cast<std::uint32_t>(stride64);
            mutableFrame->pixelFormat = !raw ? PixelFormat::RGB24 :
                (containerBitsPerPixel == 8U ? PixelFormat::Raw8 : PixelFormat::Raw16);
            mutableFrame->pixelFormatCode = raw ? rawFourCC : 0U;
            mutableFrame->bitsPerPixel = static_cast<std::uint8_t>(
                raw ? rawBitsPerPixel : 24U);
            mutableFrame->containerBitsPerPixel = containerBitsPerPixel;
            mutableFrame->isStill = true;
            mutableFrame->pixels.resize(static_cast<std::size_t>(byteCount64));

            pullResult = Amcam_PullImageV4(
                impl_->handle, mutableFrame->pixels.data(),
                1, pullBits, kPackedRows, &info);
            if (FAILED(pullResult)) {
                std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
                impl_->stillCaptureDesynchronized = true;
                return failHr(error, "Amcam_PullImageV4(still image)", pullResult);
            }

            mutableFrame->sequence = info.v3.seq;
            mutableFrame->timestampMicroseconds = info.v3.timestamp;
            mutableFrame->sdkFlags = info.v3.flag;
            mutableFrame->exposureMicroseconds = info.v3.expotime;
            mutableFrame->exposureGainPercent = info.v3.expogain;
            mutableFrame->blackLevel = info.v3.blacklevel;
            completedFrame = std::move(mutableFrame);
        } catch (const std::exception& exception) {
            std::lock_guard<std::mutex> captureLock(impl_->stillCaptureMutex);
            impl_->stillCaptureDesynchronized = true;
            return fail(error, "captureStill", -1,
                        std::string("Still-image buffer allocation failed: ") + exception.what());
        }
    }

    frame = std::move(completedFrame);
    return true;
}



camera::CameraFeatureResult<std::vector<camera::CameraFeatureDescriptor>>
AmcamCamera::featureDescriptors() {
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    return buildAmcamFeatureDescriptors(
        impl_->handle,
        impl_->state.load(std::memory_order_acquire),
        impl_->modelFlags,
        impl_->hasModelDescriptor,
        impl_->maximumSpeedLevel,
        impl_->maximumFanSpeed,
        impl_->previewResolutions,
        impl_->profileRegistry,
        impl_->modelCode);
}

camera::CameraFeatureResult<camera::CameraFeatureValue>
AmcamCamera::readFeature(std::string_view name) {
    using camera::CameraFeatureAccessError;
    using camera::CameraFeatureValue;
    using camera::FeatureEnumeration;

    std::lock_guard<std::mutex> api(impl_->apiMutex);

    // Model facts are immutable for the open session. Resolve them directly
    // from the cached model descriptor rather than rebuilding the full SDK
    // feature inventory for each of these read-only metadata fields.
    for (const auto& capability : kModelFlagCapabilities) {
        if (name == capability.name) {
            const bool supported = impl_->hasModelDescriptor &&
                (impl_->modelFlags & capability.flag) != 0;
            if (!supported ||
                !impl_->profileRegistry.allows(
                    impl_->modelCode, capability.profileFeature)) {
                return featureError(CameraFeatureAccessError::Code::FeatureUnavailable,
                                    std::string(name),
                                    "This model does not expose the requested capability.");
            }
            return CameraFeatureValue{true};
        }
    }

    auto described = buildAmcamFeatureDescriptors(
        impl_->handle,
        impl_->state.load(std::memory_order_acquire),
        impl_->modelFlags,
        impl_->hasModelDescriptor,
        impl_->maximumSpeedLevel,
        impl_->maximumFanSpeed,
        impl_->previewResolutions,
        impl_->profileRegistry,
        impl_->modelCode);
    if (const auto* error = std::get_if<CameraFeatureAccessError>(&described))
        return *error;

    const auto& descriptors =
        std::get<std::vector<camera::CameraFeatureDescriptor>>(described);
    const auto descriptor = std::find_if(descriptors.begin(), descriptors.end(),
        [name](const auto& item) { return item.name == name; });
    if (descriptor == descriptors.end())
        return featureError(CameraFeatureAccessError::Code::FeatureNotFound,
                            std::string(name), "Camera feature was not found.");
    if (!descriptor->isAvailable)
        return featureError(CameraFeatureAccessError::Code::FeatureUnavailable,
                            std::string(name), "Camera feature is not available on this device.");
    if (!descriptor->isReadable)
        return featureError(CameraFeatureAccessError::Code::FeatureNotReadable,
                            std::string(name), "Camera feature is not readable in the current state.");



    if (name == "AmcamFlatFieldCorrectionEnabled" ||
        name == "AmcamDarkFieldCorrectionEnabled") {
        const unsigned option =
            name == "AmcamFlatFieldCorrectionEnabled"
                ? AMCAM_OPTION_FFC : AMCAM_OPTION_DFC;
        int raw = 0;
        const HRESULT hr = Amcam_get_Option(impl_->handle, option, &raw);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Option(correction)", hr);
        const auto state = static_cast<std::uint32_t>(raw) & 0xffU;
        if (state > 2U)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The SDK returned an unknown correction state.");
        return CameraFeatureValue{state != 0U};
    }
    if (name == "AmcamDefectPixelCorrectionEnabled" ||
        name == "AmcamUpsideDownEnabled") {
        const unsigned option =
            name == "AmcamDefectPixelCorrectionEnabled"
                ? AMCAM_OPTION_DEFECT_PIXEL : AMCAM_OPTION_UPSIDE_DOWN;
        int value = -1;
        const HRESULT hr = Amcam_get_Option(impl_->handle, option, &value);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Option", hr);
        if (value != 0 && value != 1)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The SDK returned an invalid Boolean value.");
        return CameraFeatureValue{value != 0};
    }
    if (name == "AmcamSharpeningParameters") {
        int packed = 0;
        const HRESULT hr =
            Amcam_get_Option(impl_->handle, AMCAM_OPTION_SHARPENING, &packed);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Option(SHARPENING)", hr);
        const auto bits = static_cast<std::uint32_t>(packed);
        return CameraFeatureValue{std::vector<std::int64_t>{
            static_cast<std::int64_t>(bits & 0xffffU),
            static_cast<std::int64_t>((bits >> 16U) & 0xffU),
            static_cast<std::int64_t>((bits >> 24U) & 0xffU)}};
    }
    if (name == "AmcamRotationDegrees" ||
        name == "AmcamTestPattern") {
        const unsigned option =
            name == "AmcamRotationDegrees"
                ? AMCAM_OPTION_ROTATE : AMCAM_OPTION_TESTPATTERN;
        int value = -1;
        const HRESULT hr = Amcam_get_Option(impl_->handle, option, &value);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Option", hr);
        if (name == "AmcamRotationDegrees") {
            if (value != 0 && value != 90 && value != 180 && value != 270)
                return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                    std::string(name), "The SDK returned an unknown rotation.");
            return CameraFeatureValue{FeatureEnumeration{std::to_string(value)}};
        }
        switch (value) {
        case 0: return CameraFeatureValue{FeatureEnumeration{"Off"}};
        case 3: return CameraFeatureValue{FeatureEnumeration{"MonochromeDiagonal"}};
        case 5: return CameraFeatureValue{FeatureEnumeration{"MonochromeVertical"}};
        case 7: return CameraFeatureValue{FeatureEnumeration{"MonochromeHorizontal"}};
        case 9: return CameraFeatureValue{FeatureEnumeration{"ChromaticDiagonal"}};
        default:
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The SDK returned an unknown test-pattern mode.");
        }
    }
    if (name == "AmcamAutoExposureThreshold") {
        int value = -1;
        const HRESULT hr = Amcam_get_Option(
            impl_->handle, AMCAM_OPTION_AUTOEXP_THRESHOLD, &value);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Option(AUTOEXP_THRESHOLD)", hr);
        if (value < 2 || value > 15)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The SDK returned an out-of-range auto-exposure threshold.");
        return CameraFeatureValue{static_cast<std::int64_t>(value)};
    }
    if (name == "ExposureAuto") {
        int mode = -1;
        const HRESULT hr = Amcam_get_AutoExpoEnable(impl_->handle, &mode);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_AutoExpoEnable", hr);
        if (mode < 0 || mode > 2)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The camera returned an unknown auto-exposure mode.");
        return CameraFeatureValue{FeatureEnumeration{
            std::string(amcamAutoExposureModeSymbol(
                static_cast<AmcamAutoExposureMode>(mode)))}};
    }
    if (name == "ExposureTime") {
        unsigned value = 0;
        const HRESULT hr = Amcam_get_ExpoTime(impl_->handle, &value);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_ExpoTime", hr);
        impl_->updateExpectedExposure(value);
        return CameraFeatureValue{static_cast<std::int64_t>(value)};
    }
    if (name == "AmcamAnalogGainPercent") {
        unsigned short value = 0;
        const HRESULT hr = Amcam_get_ExpoAGain(impl_->handle, &value);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_ExpoAGain", hr);
        return CameraFeatureValue{static_cast<std::int64_t>(value)};
    }
    if (name == "AmcamColorMatrixEnabled") {
        int value = 0;
        const HRESULT hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_COLORMATIX, &value);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(COLORMATIX)", hr);
        return CameraFeatureValue{value != 0};
    }
    if (name == "AmcamPixelFormat") {
        int value = -1;
        const HRESULT hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_PIXEL_FORMAT, &value);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(PIXEL_FORMAT)", hr);
        return CameraFeatureValue{FeatureEnumeration{std::to_string(value)}};
    }

    if (name == "AmcamWhiteBalanceTemperature" ||
        name == "AmcamWhiteBalanceTint") {
        int temperature = 0, tint = 0;
        const HRESULT hr = Amcam_get_TempTint(impl_->handle, &temperature, &tint);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_TempTint", hr);
        return CameraFeatureValue{static_cast<std::int64_t>(
            name == "AmcamWhiteBalanceTemperature" ? temperature : tint)};
    }
    if (name == "AmcamWhiteBalanceRGBGain") {
        int values[3]{};
        const HRESULT hr = Amcam_get_WhiteBalanceGain(impl_->handle, values);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_WhiteBalanceGain", hr);
        return CameraFeatureValue{std::vector<std::int64_t>{
            values[0], values[1], values[2]}};
    }
    if (name == "AmcamFixedFrameRate") {
        int value = 0;
        const HRESULT hr = Amcam_get_Option(
            impl_->handle, AMCAM_OPTION_PRECISE_FRAMERATE, &value);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(PRECISE_FRAMERATE)", hr);
        return CameraFeatureValue{static_cast<double>(value) / 10.0};
    }
    if (name == "AmcamFrameSpeedLevel") {
        unsigned short value = 0;
        const HRESULT hr = Amcam_get_Speed(impl_->handle, &value);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Speed", hr);
        return CameraFeatureValue{static_cast<std::int64_t>(value)};
    }

    if (name == "AmcamBinning") {
        const char* valueText = nullptr;
        const char* methodText = nullptr;
        const HRESULT hr = Amcam_get_Binning(
            impl_->handle, &valueText, &methodText);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Binning", hr);
        const auto symbol = binningFeatureSymbol(valueText, methodText);
        if (symbol.empty())
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The SDK returned an invalid binning value/method pair.");
        return CameraFeatureValue{FeatureEnumeration{symbol}};
    }
    if (name == "AmcamHardwareROI") {
        unsigned x = 0, y = 0, width = 0, height = 0;
        const HRESULT hr = Amcam_get_Roi(
            impl_->handle, &x, &y, &width, &height);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Roi", hr);
        return CameraFeatureValue{std::vector<std::int64_t>{
            static_cast<std::int64_t>(x),
            static_cast<std::int64_t>(y),
            static_cast<std::int64_t>(width),
            static_cast<std::int64_t>(height)}};
    }
    if (name == "AmcamResolutionIndex") {
        unsigned index = 0;
        const HRESULT hr = Amcam_get_eSize(impl_->handle, &index);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_eSize", hr);
        if (index >= impl_->previewResolutions.size())
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The camera returned an unknown preview-resolution index.");
        return CameraFeatureValue{FeatureEnumeration{
            resolutionFeatureSymbol(index, impl_->previewResolutions[index])}};
    }
    if (name == "AmcamRawOutputEnabled") {
        int value = 0;
        const HRESULT hr = Amcam_get_Option(
            impl_->handle, AMCAM_OPTION_RAW, &value);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(RAW)", hr);
        return CameraFeatureValue{value != 0};
    }
    if (name == "AmcamRawBitDepth") {
        int value = 0;
        const HRESULT hr = Amcam_get_Option(
            impl_->handle, AMCAM_OPTION_BITDEPTH, &value);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(BITDEPTH)", hr);
        if (value != 0 && value != 1)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The camera returned an unknown RAW bit-depth mode.");
        return CameraFeatureValue{FeatureEnumeration{value == 0 ? "8-bit" : "16-bit"}};
    }
    if (name == "AmcamRawSensorFormat") {
        unsigned fourCC = 0, bitsPerPixel = 0;
        const HRESULT hr = Amcam_get_RawFormat(
            impl_->handle, &fourCC, &bitsPerPixel);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_RawFormat", hr);
        return CameraFeatureValue{FeatureEnumeration{
            rawFormatFeatureSymbol(fourCC, bitsPerPixel)}};
    }

    if (name == "AmcamLinearToneMappingEnabled" ||
        name == "AmcamLowNoiseModeEnabled" ||
        name == "AmcamHighFullWellEnabled" ||
        name == "AmcamTecEnabled") {
        const unsigned option =
            name == "AmcamLinearToneMappingEnabled" ? AMCAM_OPTION_LINEAR :
            name == "AmcamLowNoiseModeEnabled" ? AMCAM_OPTION_LOW_NOISE :
            name == "AmcamHighFullWellEnabled" ? AMCAM_OPTION_HIGH_FULLWELL :
                                                  AMCAM_OPTION_TEC;
        int value = -1;
        const HRESULT hr = Amcam_get_Option(impl_->handle, option, &value);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Option", hr);
        if (value != 0 && value != 1)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The SDK returned an invalid Boolean option value.");
        return CameraFeatureValue{value != 0};
    }
    if (name == "AmcamCurveMode" ||
        name == "AmcamDemosaicVideo" ||
        name == "AmcamDemosaicStill" ||
        name == "AmcamConversionGainMode") {
        const unsigned option =
            name == "AmcamCurveMode" ? AMCAM_OPTION_CURVE :
            name == "AmcamDemosaicVideo" ? AMCAM_OPTION_DEMOSAIC_VIDEO :
            name == "AmcamDemosaicStill" ? AMCAM_OPTION_DEMOSAIC_STILL :
                                           AMCAM_OPTION_CG;
        int value = -1;
        const HRESULT hr = Amcam_get_Option(impl_->handle, option, &value);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Option", hr);
        std::string symbol;
        if (name == "AmcamCurveMode") {
            static const char* modes[] = {"Off", "Polynomial", "Logarithmic"};
            if (value < 0 || value > 2)
                return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                    std::string(name), "The SDK returned an unknown curve mode.");
            symbol = modes[value];
        } else if (name == "AmcamConversionGainMode") {
            if (value == 0) symbol = "LCG";
            else if (value == 1) symbol = "HCG";
            else if (value == 2 && (impl_->modelFlags & AMCAM_FLAG_CGHDR) != 0)
                symbol = "HDR";
            else if (value == 2 && (impl_->modelFlags & AMCAM_FLAG_GHOPTO) != 0)
                symbol = "MCG";
            else
                return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                    std::string(name), "The SDK returned an unsupported conversion-gain mode.");
        } else {
            static const char* methods[] = {"Bilinear", "VNG", "PPG", "AHD", "EdgeAware"};
            if (value < 0 || value > 4)
                return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                    std::string(name), "The SDK returned an unknown demosaic method.");
            symbol = methods[value];
        }
        return CameraFeatureValue{FeatureEnumeration{std::move(symbol)}};
    }
    if (name == "AmcamDenoiseStrength" ||
        name == "AmcamBlackLevel" ||
        name == "AmcamTecTargetTemperature" ||
        name == "AmcamFanSpeed" ||
        name == "AmcamHeatAntiFogLevel" ||
        name == "AmcamTecVoltage") {
        const unsigned option =
            name == "AmcamDenoiseStrength" ? AMCAM_OPTION_DENOISE :
            name == "AmcamBlackLevel" ? AMCAM_OPTION_BLACKLEVEL :
            name == "AmcamTecTargetTemperature" ? AMCAM_OPTION_TECTARGET :
            name == "AmcamFanSpeed" ? AMCAM_OPTION_FAN :
            name == "AmcamHeatAntiFogLevel" ? AMCAM_OPTION_HEAT :
                                               AMCAM_OPTION_TEC_VOLTAGE;
        int value = 0;
        const HRESULT hr = Amcam_get_Option(impl_->handle, option, &value);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Option", hr);
        return CameraFeatureValue{static_cast<std::int64_t>(value)};
    }
    if (name == "AmcamSensorTemperature") {
        short value = 0;
        const HRESULT hr = Amcam_get_Temperature(impl_->handle, &value);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Temperature", hr);
        return CameraFeatureValue{static_cast<std::int64_t>(value)};
    }

    return featureError(CameraFeatureAccessError::Code::FeatureNotFound,
                        std::string(name), "Camera feature was not found.");
}

camera::CameraFeatureResult<std::monostate>
AmcamCamera::writeFeature(std::string_view name, const camera::CameraFeatureValue& value) {
    using camera::CameraFeatureAccessError;
    using camera::CameraFeatureKind;
    using camera::FeatureEnumeration;

    std::lock_guard<std::mutex> api(impl_->apiMutex);
    auto described = buildAmcamFeatureDescriptors(
        impl_->handle,
        impl_->state.load(std::memory_order_acquire),
        impl_->modelFlags,
        impl_->hasModelDescriptor,
        impl_->maximumSpeedLevel,
        impl_->maximumFanSpeed,
        impl_->previewResolutions,
        impl_->profileRegistry,
        impl_->modelCode);
    if (const auto* error = std::get_if<CameraFeatureAccessError>(&described))
        return *error;

    const auto& descriptors =
        std::get<std::vector<camera::CameraFeatureDescriptor>>(described);
    const auto descriptor = std::find_if(descriptors.begin(), descriptors.end(),
        [name](const auto& item) { return item.name == name; });
    if (descriptor == descriptors.end())
        return featureError(CameraFeatureAccessError::Code::FeatureNotFound,
                            std::string(name), "Camera feature was not found.");
    if (!descriptor->isAvailable)
        return featureError(CameraFeatureAccessError::Code::FeatureUnavailable,
                            std::string(name), "Camera feature is not available on this device.");
    if (!descriptor->isWritable)
        return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                            std::string(name), "Camera feature is not writable in the current state.");

    auto validation = camera::validateFeatureWrite(*descriptor, value);
    if (const auto* error = std::get_if<CameraFeatureAccessError>(&validation))
        return *error;

    if (name == "ExposureAuto") {
        const auto& symbol = std::get<FeatureEnumeration>(value).symbol;
        AmcamAutoExposureMode mode = AmcamAutoExposureMode::Off;
        if (!parseAmcamAutoExposureMode(symbol, mode))
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "ExposureAuto accepts Off, Once or Continuous.");
        const auto raw = static_cast<int>(mode);
        HRESULT hr = Amcam_put_AutoExpoEnable(impl_->handle, raw);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_AutoExpoEnable", hr);
        int readback = -1;
        hr = Amcam_get_AutoExpoEnable(impl_->handle, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_AutoExpoEnable", hr);
        if (readback != raw)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Auto-exposure readback did not match the requested mode.");
        unsigned exposureMicroseconds = 0;
        if (SUCCEEDED(Amcam_get_ExpoTime(impl_->handle, &exposureMicroseconds)))
            impl_->updateExpectedExposure(exposureMicroseconds);
        return std::monostate{};
    }

    if (name == "ExposureTime") {
        const auto requested = std::get<std::int64_t>(value);
        if (requested < 0 || requested > std::numeric_limits<std::uint32_t>::max())
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "ExposureTime must fit the SDK unsigned 32-bit value.");
        int autoMode = -1;
        HRESULT hr = Amcam_get_AutoExpoEnable(impl_->handle, &autoMode);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_AutoExpoEnable", hr);
        if (autoMode != static_cast<int>(AmcamAutoExposureMode::Off))
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Exposure time requires manual exposure mode.");
        unsigned minimum = 0, maximum = 0, defaultValue = 0;
        hr = Amcam_get_ExpTimeRange(impl_->handle, &minimum, &maximum, &defaultValue);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_ExpTimeRange", hr);
        if (minimum == 0 || maximum < minimum ||
            requested < minimum || requested > maximum)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Exposure time is outside the current camera range.");
        hr = Amcam_put_ExpoTime(impl_->handle, static_cast<unsigned>(requested));
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_ExpoTime", hr);
        unsigned readback = 0;
        hr = Amcam_get_ExpoTime(impl_->handle, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_ExpoTime", hr);
        if (readback != static_cast<unsigned>(requested))
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Exposure-time readback did not match the requested value.");
        impl_->updateExpectedExposure(readback);
        return std::monostate{};
    }

    if (name == "AmcamAnalogGainPercent") {
        const auto requested = std::get<std::int64_t>(value);
        if (requested < 0 || requested > std::numeric_limits<std::uint16_t>::max())
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Analog gain must fit the SDK unsigned 16-bit value.");
        unsigned short minimum = 0, maximum = 0, defaultValue = 0;
        HRESULT hr = Amcam_get_ExpoAGainRange(impl_->handle, &minimum, &maximum, &defaultValue);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_ExpoAGainRange", hr);
        if (maximum < minimum || requested < minimum || requested > maximum)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Analog gain is outside the current camera range.");
        hr = Amcam_put_ExpoAGain(impl_->handle, static_cast<unsigned short>(requested));
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_ExpoAGain", hr);
        unsigned short readback = 0;
        hr = Amcam_get_ExpoAGain(impl_->handle, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_ExpoAGain", hr);
        if (readback != static_cast<unsigned short>(requested))
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Analog-gain readback did not match the requested value.");
        return std::monostate{};
    }

    if (name == "AmcamColorMatrixEnabled") {
        const int requested = std::get<bool>(value) ? 1 : 0;
        HRESULT hr = Amcam_put_Option(impl_->handle, AMCAM_OPTION_COLORMATIX, requested);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_Option(COLORMATIX)", hr);
        int readback = -1;
        hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_COLORMATIX, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(COLORMATIX)", hr);
        if (readback != requested)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Color-matrix readback did not match the requested value.");
        return std::monostate{};
    }

    if (name == "AmcamPixelFormat") {
        std::int32_t code = 0;
        if (!parseSignedInteger(std::get<FeatureEnumeration>(value).symbol, code))
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Pixel format symbol must be a numeric Amcam SDK code.");
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Pixel format can only be changed while acquisition is stopped.");
        HRESULT hr = Amcam_put_Option(impl_->handle, AMCAM_OPTION_PIXEL_FORMAT, code);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_Option(PIXEL_FORMAT)", hr);
        int readback = -1;
        hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_PIXEL_FORMAT, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(PIXEL_FORMAT)", hr);
        if (readback != code)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Pixel-format readback did not match the requested code.");
        return std::monostate{};
    }

    if (name == "AmcamRawOutputEnabled") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "RAW output can only be changed while acquisition is stopped.");
        const int requested = std::get<bool>(value) ? 1 : 0;
        HRESULT hr = Amcam_put_Option(impl_->handle, AMCAM_OPTION_RAW, requested);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_Option(RAW)", hr);
        int readback = -1;
        hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_RAW, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(RAW)", hr);
        if (readback != requested)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "RAW-output readback did not match the requested mode.");
        return std::monostate{};
    }

    if (name == "AmcamRawBitDepth") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "RAW bit depth can only be changed while acquisition is stopped.");
        int rawMode = 0;
        HRESULT hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_RAW, &rawMode);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(RAW)", hr);
        if (rawMode != 1)
            return featureError(CameraFeatureAccessError::Code::FeatureUnavailable,
                                std::string(name), "Enable RAW sensor output before selecting its bit-depth mode.");
        const auto& symbol = std::get<FeatureEnumeration>(value).symbol;
        const int requested = symbol == "8-bit" ? 0 : symbol == "16-bit" ? 1 : -1;
        if (requested < 0)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "RAW bit depth accepts 8-bit or 16-bit.");
        hr = Amcam_put_Option(impl_->handle, AMCAM_OPTION_BITDEPTH, requested);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_Option(BITDEPTH)", hr);
        int readback = -1;
        hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_BITDEPTH, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(BITDEPTH)", hr);
        if (readback != requested)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "RAW bit-depth readback did not match the requested mode.");
        return std::monostate{};
    }

    if (name == "AmcamLinearToneMappingEnabled" ||
        name == "AmcamLowNoiseModeEnabled" ||
        name == "AmcamHighFullWellEnabled" ||
        name == "AmcamTecEnabled") {
        const unsigned option =
            name == "AmcamLinearToneMappingEnabled" ? AMCAM_OPTION_LINEAR :
            name == "AmcamLowNoiseModeEnabled" ? AMCAM_OPTION_LOW_NOISE :
            name == "AmcamHighFullWellEnabled" ? AMCAM_OPTION_HIGH_FULLWELL :
                                                  AMCAM_OPTION_TEC;
        const int requested = std::get<bool>(value) ? 1 : 0;
        const HRESULT hr = Amcam_put_Option(impl_->handle, option, requested);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Option", hr);
        int readback = -1;
        const HRESULT readHr = Amcam_get_Option(impl_->handle, option, &readback);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Option(readback)", readHr);
        if (readback != requested)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The Boolean option readback did not match the request.");
        return std::monostate{};
    }
    if (name == "AmcamCurveMode" ||
        name == "AmcamDemosaicVideo" ||
        name == "AmcamDemosaicStill" ||
        name == "AmcamConversionGainMode") {
        const auto& symbol = std::get<FeatureEnumeration>(value).symbol;
        int requested = -1;
        unsigned option = 0;
        if (name == "AmcamCurveMode") {
            option = AMCAM_OPTION_CURVE;
            if (symbol == "Off") requested = 0;
            else if (symbol == "Polynomial") requested = 1;
            else if (symbol == "Logarithmic") requested = 2;
        } else if (name == "AmcamConversionGainMode") {
            option = AMCAM_OPTION_CG;
            if (symbol == "LCG") requested = 0;
            else if (symbol == "HCG") requested = 1;
            else if (symbol == "HDR" &&
                     (impl_->modelFlags & AMCAM_FLAG_CGHDR) != 0) requested = 2;
            else if (symbol == "MCG" &&
                     (impl_->modelFlags & AMCAM_FLAG_GHOPTO) != 0) requested = 2;
        } else {
            option = name == "AmcamDemosaicVideo"
                ? AMCAM_OPTION_DEMOSAIC_VIDEO : AMCAM_OPTION_DEMOSAIC_STILL;
            if (symbol == "Bilinear") requested = 0;
            else if (symbol == "VNG") requested = 1;
            else if (symbol == "PPG") requested = 2;
            else if (symbol == "AHD") requested = 3;
            else if (symbol == "EdgeAware") requested = 4;
        }
        if (requested < 0)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "The requested enumeration symbol is not supported by this camera.");
        const HRESULT hr = Amcam_put_Option(impl_->handle, option, requested);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Option", hr);
        int readback = -1;
        const HRESULT readHr = Amcam_get_Option(impl_->handle, option, &readback);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Option(readback)", readHr);
        if (readback != requested)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The enumeration readback did not match the request.");
        return std::monostate{};
    }
    if (name == "AmcamDenoiseStrength" ||
        name == "AmcamBlackLevel" ||
        name == "AmcamTecTargetTemperature" ||
        name == "AmcamFanSpeed" ||
        name == "AmcamHeatAntiFogLevel") {
        const auto requested = std::get<std::int64_t>(value);
        if (requested < std::numeric_limits<int>::min() ||
            requested > std::numeric_limits<int>::max())
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "The requested value does not fit the SDK signed integer.");
        const int target = static_cast<int>(requested);
        unsigned option = 0;
        if (name == "AmcamDenoiseStrength") option = AMCAM_OPTION_DENOISE;
        else if (name == "AmcamBlackLevel") option = AMCAM_OPTION_BLACKLEVEL;
        else if (name == "AmcamTecTargetTemperature") option = AMCAM_OPTION_TECTARGET;
        else if (name == "AmcamFanSpeed") option = AMCAM_OPTION_FAN;
        else option = AMCAM_OPTION_HEAT;

        const HRESULT hr = Amcam_put_Option(impl_->handle, option, target);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Option", hr);
        int readback = 0;
        const HRESULT readHr = Amcam_get_Option(impl_->handle, option, &readback);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Option(readback)", readHr);
        if (readback != target)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The numeric option readback did not match the request.");
        return std::monostate{};
    }


    if (name == "AmcamFlatFieldCorrectionEnabled" ||
        name == "AmcamDarkFieldCorrectionEnabled") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Field correction can only be changed while acquisition is stopped.");
        const unsigned option =
            name == "AmcamFlatFieldCorrectionEnabled"
                ? AMCAM_OPTION_FFC : AMCAM_OPTION_DFC;
        const int requested = std::get<bool>(value) ? 1 : 0;
        const HRESULT hr = Amcam_put_Option(impl_->handle, option, requested);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Option(correction)", hr);
        int readback = -1;
        const HRESULT readHr = Amcam_get_Option(impl_->handle, option, &readback);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Option(correction readback)", readHr);
        const auto state = static_cast<std::uint32_t>(readback) & 0xffU;
        if ((requested == 0 && state != 0U) ||
            (requested == 1 && state == 0U))
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Correction state readback did not match the request.");
        return std::monostate{};
    }
    if (name == "AmcamDefectPixelCorrectionEnabled" ||
        name == "AmcamUpsideDownEnabled") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Image correction can only be changed while acquisition is stopped.");
        const unsigned option =
            name == "AmcamDefectPixelCorrectionEnabled"
                ? AMCAM_OPTION_DEFECT_PIXEL : AMCAM_OPTION_UPSIDE_DOWN;
        const int requested = std::get<bool>(value) ? 1 : 0;
        const HRESULT hr = Amcam_put_Option(impl_->handle, option, requested);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Option", hr);
        int readback = -1;
        const HRESULT readHr = Amcam_get_Option(impl_->handle, option, &readback);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Option(readback)", readHr);
        if (readback != requested)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Image-correction readback did not match the request.");
        return std::monostate{};
    }
    if (name == "AmcamSharpeningParameters") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Sharpening can only be changed while acquisition is stopped.");
        const auto& parameters = std::get<std::vector<std::int64_t>>(value);
        if (parameters.size() != 3 ||
            parameters[0] < 0 || parameters[0] > 500 ||
            parameters[1] < 1 || parameters[1] > 10 ||
            parameters[2] < 0 || parameters[2] > 255) {
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Sharpening requires [strength 0..500, radius 1..10, threshold 0..255].");
        }
        const std::uint32_t packed =
            (static_cast<std::uint32_t>(parameters[2]) << 24U) |
            (static_cast<std::uint32_t>(parameters[1]) << 16U) |
            static_cast<std::uint32_t>(parameters[0]);
        const int requested = static_cast<int>(static_cast<std::int32_t>(packed));
        const HRESULT hr = Amcam_put_Option(
            impl_->handle, AMCAM_OPTION_SHARPENING, requested);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Option(SHARPENING)", hr);
        int readback = 0;
        const HRESULT readHr = Amcam_get_Option(
            impl_->handle, AMCAM_OPTION_SHARPENING, &readback);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Option(SHARPENING readback)", readHr);
        if (static_cast<std::uint32_t>(readback) != packed)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Sharpening readback did not match the requested parameters.");
        return std::monostate{};
    }
    if (name == "AmcamRotationDegrees" ||
        name == "AmcamTestPattern") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Image orientation and test-pattern settings can only be changed while acquisition is stopped.");
        const auto& symbol = std::get<FeatureEnumeration>(value).symbol;
        int requested = -1;
        unsigned option = 0;
        if (name == "AmcamRotationDegrees") {
            option = AMCAM_OPTION_ROTATE;
            if (symbol == "0") requested = 0;
            else if (symbol == "90") requested = 90;
            else if (symbol == "180") requested = 180;
            else if (symbol == "270") requested = 270;
        } else {
            option = AMCAM_OPTION_TESTPATTERN;
            if (symbol == "Off") requested = 0;
            else if (symbol == "MonochromeDiagonal") requested = 3;
            else if (symbol == "MonochromeVertical") requested = 5;
            else if (symbol == "MonochromeHorizontal") requested = 7;
            else if (symbol == "ChromaticDiagonal") requested = 9;
        }
        if (requested < 0)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "The requested mode is not supported.");
        const HRESULT hr = Amcam_put_Option(impl_->handle, option, requested);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Option", hr);
        int readback = -1;
        const HRESULT readHr = Amcam_get_Option(impl_->handle, option, &readback);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Option(readback)", readHr);
        if (readback != requested)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The selected mode readback did not match the request.");
        return std::monostate{};
    }
    if (name == "AmcamAutoExposureThreshold") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Auto-exposure threshold can only be changed while acquisition is stopped.");
        const auto requested = std::get<std::int64_t>(value);
        if (requested < 2 || requested > 15)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Auto-exposure threshold must be between 2 and 15.");
        const HRESULT hr = Amcam_put_Option(
            impl_->handle, AMCAM_OPTION_AUTOEXP_THRESHOLD,
            static_cast<int>(requested));
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Option(AUTOEXP_THRESHOLD)", hr);
        int readback = -1;
        const HRESULT readHr = Amcam_get_Option(
            impl_->handle, AMCAM_OPTION_AUTOEXP_THRESHOLD, &readback);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Option(AUTOEXP_THRESHOLD readback)", readHr);
        if (readback != requested)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Auto-exposure threshold readback did not match the request.");
        return std::monostate{};
    }
    if (name == "AmcamBinning") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Binning can only be changed while acquisition is stopped.");
        const auto& symbol = std::get<FeatureEnumeration>(value).symbol;
        std::string requestedValue;
        std::string requestedMethod;
        if (!parseBinningFeatureSymbol(symbol, requestedValue, requestedMethod))
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Binning value must identify an advertised value/method pair.");
        const HRESULT hr = Amcam_put_Binning(
            impl_->handle, requestedValue.c_str(), requestedMethod.c_str());
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Binning", hr);
        const char* readbackValue = nullptr;
        const char* readbackMethod = nullptr;
        const HRESULT readHr = Amcam_get_Binning(
            impl_->handle, &readbackValue, &readbackMethod);
        if (FAILED(readHr))
            return featureBackendError(std::string(name), "Amcam_get_Binning(readback)", readHr);
        if (binningFeatureSymbol(readbackValue, readbackMethod) != symbol)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Binning readback did not match the requested value/method pair.");
        return std::monostate{};
    }
    if (name == "AmcamHardwareROI") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Hardware ROI can only be changed while acquisition is stopped.");

        const auto& requested = std::get<std::vector<std::int64_t>>(value);
        if (requested.size() != 4)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Hardware ROI requires x, y, width and height.");
        for (const auto component : requested) {
            if (component < 0 ||
                component > std::numeric_limits<std::uint32_t>::max() ||
                (component & 1) != 0) {
                return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                    std::string(name), "ROI offsets and dimensions must be non-negative even 32-bit values.");
            }
        }
        const auto x = static_cast<std::uint32_t>(requested[0]);
        const auto y = static_cast<std::uint32_t>(requested[1]);
        const auto width = static_cast<std::uint32_t>(requested[2]);
        const auto height = static_cast<std::uint32_t>(requested[3]);
        if (width == 0 || height == 0)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "ROI width and height must be positive.");

        unsigned previewIndex = std::numeric_limits<unsigned>::max();
        HRESULT hr = Amcam_get_eSize(impl_->handle, &previewIndex);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_eSize", hr);
        if (previewIndex >= impl_->previewResolutions.size())
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "The selected preview-resolution index is unknown.");
        const auto& base = impl_->previewResolutions[previewIndex];
        const std::uint64_t right = static_cast<std::uint64_t>(x) + width;
        const std::uint64_t bottom = static_cast<std::uint64_t>(y) + height;
        if (base.width == 0 || base.height == 0 ||
            right > base.width || bottom > base.height) {
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "ROI rectangle exceeds the selected preview-resolution bounds.");
        }

        hr = Amcam_put_Roi(impl_->handle, x, y, width, height);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_put_Roi", hr);
        unsigned readX = 0, readY = 0, readWidth = 0, readHeight = 0;
        hr = Amcam_get_Roi(impl_->handle, &readX, &readY, &readWidth, &readHeight);
        if (FAILED(hr))
            return featureBackendError(std::string(name), "Amcam_get_Roi(readback)", hr);
        if (readX != x || readY != y || readWidth != width || readHeight != height)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "ROI readback did not match the requested rectangle.");
        return std::monostate{};
    }
    if (name == "AmcamWhiteBalanceTemperature" ||
        name == "AmcamWhiteBalanceTint") {
        const auto requested = std::get<std::int64_t>(value);
        if (requested < std::numeric_limits<std::int32_t>::min() ||
            requested > std::numeric_limits<std::int32_t>::max())
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "White-balance value must fit the SDK signed 32-bit value.");
        int temperature = 0, tint = 0;
        HRESULT hr = Amcam_get_TempTint(impl_->handle, &temperature, &tint);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_TempTint", hr);
        if (name == "AmcamWhiteBalanceTemperature") {
            if (requested < 2000 || requested > 15000)
                return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                    std::string(name), "Temperature must be between 2000 and 15000 K.");
            temperature = static_cast<int>(requested);
        } else {
            if (requested < 200 || requested > 2500)
                return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                    std::string(name), "Tint must be between 200 and 2500.");
            tint = static_cast<int>(requested);
        }
        hr = Amcam_put_TempTint(impl_->handle, temperature, tint);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_TempTint", hr);
        int readbackTemperature = 0, readbackTint = 0;
        hr = Amcam_get_TempTint(impl_->handle, &readbackTemperature, &readbackTint);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_TempTint", hr);
        const int expected = static_cast<int>(requested);
        const int actual = name == "AmcamWhiteBalanceTemperature"
            ? readbackTemperature : readbackTint;
        if (actual != expected)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "White-balance readback did not match the requested value.");
        return std::monostate{};
    }

    if (name == "AmcamWhiteBalanceRGBGain") {
        const auto& requestedValues = std::get<std::vector<std::int64_t>>(value);
        int requested[3]{};
        for (std::size_t i = 0; i < requestedValues.size(); ++i) {
            if (requestedValues[i] < std::numeric_limits<int>::min() ||
                requestedValues[i] > std::numeric_limits<int>::max())
                return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                    std::string(name), "RGB gain values must fit signed SDK integers.");
            requested[i] = static_cast<int>(requestedValues[i]);
        }
        HRESULT hr = Amcam_put_WhiteBalanceGain(impl_->handle, requested);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_WhiteBalanceGain", hr);
        int readback[3]{};
        hr = Amcam_get_WhiteBalanceGain(impl_->handle, readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_WhiteBalanceGain", hr);
        if (!std::equal(std::begin(requested), std::end(requested), std::begin(readback)))
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "RGB white-balance readback did not match the requested values.");
        return std::monostate{};
    }

    if (name == "AmcamFixedFrameRate") {
        const double requested = std::get<double>(value);
        const double scaled = requested * 10.0;
        const double rounded = std::round(scaled);
        if (!std::isfinite(scaled) || rounded < 1.0 ||
            rounded > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
            std::abs(scaled - rounded) >= 0.000001)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Fixed frame rate must be a positive multiple of 0.1 FPS.");
        const auto tenths = static_cast<std::int32_t>(rounded);
        int minimum = 0, maximum = 0;
        HRESULT hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_MIN_PRECISE_FRAMERATE, &minimum);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(MIN_PRECISE_FRAMERATE)", hr);
        hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_MAX_PRECISE_FRAMERATE, &maximum);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(MAX_PRECISE_FRAMERATE)", hr);
        if (minimum <= 0 || maximum < minimum || tenths < minimum || tenths > maximum)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Fixed frame rate is outside the current camera range.");
        hr = Amcam_put_Option(impl_->handle, AMCAM_OPTION_PRECISE_FRAMERATE, tenths);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_Option(PRECISE_FRAMERATE)", hr);
        int readback = 0;
        hr = Amcam_get_Option(impl_->handle, AMCAM_OPTION_PRECISE_FRAMERATE, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Option(PRECISE_FRAMERATE)", hr);
        if (readback != tenths)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Fixed-frame-rate readback did not match the requested value.");
        return std::monostate{};
    }

    if (name == "AmcamFrameSpeedLevel") {
        const auto requested = std::get<std::int64_t>(value);
        if (!impl_->maximumSpeedLevel ||
            requested < 0 || requested > *impl_->maximumSpeedLevel ||
            requested > std::numeric_limits<std::uint16_t>::max())
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Frame-speed level is outside the model-reported range.");
        const auto level = static_cast<unsigned short>(requested);
        HRESULT hr = Amcam_put_Speed(impl_->handle, level);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_Speed", hr);
        unsigned short readback = 0;
        hr = Amcam_get_Speed(impl_->handle, &readback);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Speed", hr);
        if (readback != level)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Frame-speed readback did not match the requested level.");
        return std::monostate{};
    }

    if (name == "AmcamResolutionIndex") {
        if (impl_->state.load(std::memory_order_acquire) != CameraState::Open)
            return featureError(CameraFeatureAccessError::Code::FeatureNotWritable,
                                std::string(name), "Resolution can only be changed while acquisition is stopped.");
        std::uint32_t index = 0, expectedWidth = 0, expectedHeight = 0;
        if (!parseResolutionFeatureSymbol(
                std::get<FeatureEnumeration>(value).symbol,
                index, expectedWidth, expectedHeight) ||
            index >= impl_->previewResolutions.size())
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Resolution symbol is invalid.");
        const auto& modelResolution = impl_->previewResolutions[index];
        if (modelResolution.width != expectedWidth || modelResolution.height != expectedHeight)
            return featureError(CameraFeatureAccessError::Code::InvalidValue,
                                std::string(name), "Resolution symbol does not match the current model descriptor.");
        HRESULT hr = Amcam_put_eSize(impl_->handle, index);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_put_eSize", hr);
        int readbackWidth = 0, readbackHeight = 0;
        hr = Amcam_get_Size(impl_->handle, &readbackWidth, &readbackHeight);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_Size", hr);
        if (readbackWidth != static_cast<int>(expectedWidth) ||
            readbackHeight != static_cast<int>(expectedHeight))
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Resolution size readback did not match the requested model resolution.");
        unsigned readbackIndex = std::numeric_limits<unsigned>::max();
        hr = Amcam_get_eSize(impl_->handle, &readbackIndex);
        if (FAILED(hr)) return featureBackendError(std::string(name), "Amcam_get_eSize", hr);
        if (readbackIndex != index)
            return featureError(CameraFeatureAccessError::Code::BackendFailure,
                                std::string(name), "Resolution index readback did not match the requested index.");
        return std::monostate{};
    }

    return featureError(CameraFeatureAccessError::Code::FeatureNotFound,
                        std::string(name), "Camera feature was not found.");
}

} // namespace amscope
