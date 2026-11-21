#define AMCAM_HRESULT_ERRORCODE_NEEDED
#include "amcam.h"
#include "amscope/AmcamCamera.h"
#include "AmcamEventQueue.h"
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
bool rgb24Size(std::uint32_t w, std::uint32_t h, std::size_t& bytes) {
    if (!w || !h || w > std::numeric_limits<std::uint32_t>::max() / 3U)
        return false;
    const std::uint64_t n = static_cast<std::uint64_t>(w) * h * 3U;
    if (n > std::numeric_limits<std::size_t>::max()) return false;
    bytes = static_cast<std::size_t>(n);
    return true;
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

    std::vector<CameraFeatureDescriptor> descriptors;
    descriptors.reserve(11);

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
        std::nullopt, std::nullopt, std::move(resolutionSymbols)
    });
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
        FramePool(std::uint32_t w, std::uint32_t h, std::size_t bytes)
            : discard(bytes) {
            slots.reserve(kPoolSize);
            for (std::size_t i = 0; i < kPoolSize; ++i) {
                auto frame = std::make_shared<Frame>();
                frame->width = w;
                frame->height = h;
                frame->strideBytes = w * 3U;
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
    };

    std::atomic<CameraState> state{CameraState::Closed};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> sdkStarted{false};
    std::atomic<bool> frameCacheFlushPending{false};
    std::atomic<std::uint32_t> activeSdkCallbacks{0};
    std::atomic<std::uint64_t> dropped{0};

    detail::AmcamEventQueue eventQueue;
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
    std::vector<Resolution> previewResolutions;
    std::thread worker;
    std::shared_ptr<FramePool> pool;
    FrameCallback onFrame;
    ErrorCallback onError;

    bool onWorkerThread() const noexcept { return g_acquisitionOwner == this; }

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

        // Match the Swift event handler's relevant event set. The callback does
        // not call the SDK, pull a frame, invoke observers, or touch Qt.
        switch (event) {
        case AMCAM_EVENT_IMAGE:
        case AMCAM_EVENT_DISCONNECTED:
        case AMCAM_EVENT_ERROR:
        case AMCAM_EVENT_NOFRAMETIMEOUT:
        case AMCAM_EVENT_NOPACKETTIMEOUT:
            try {
                (void)self->eventQueue.push(event);
            } catch (...) {
                // Never let a C++ exception unwind through the vendor callback ABI.
                self->eventQueue.stop();
            }
            break;
        default:
            break;
        }
    }

    bool reportFatal(CameraError error) noexcept {
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

        // The outer loop is important: a fatal event can race a user Stop.
        // If Stop owns the transition, reportFatal declines ownership and the
        // worker waits for the Stop outcome instead of exiting prematurely.
        for (;;) {
            bool fatal = false;
            CameraError error;

            for (;;) {
                std::uint32_t event = 0;
                const auto waitResult = eventQueue.waitNext(event);
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
                        hr = Amcam_PullImageV4(
                            handle, destination, 0, kRgb24Bits, kPackedRows, &info);
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

                // Keep draining the SDK even when all frame leases are retained.
                // Pull into the private discard buffer and account for the drop.
                if (!writable) {
                    dropped.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                const auto w = info.v3.width;
                const auto h = info.v3.height;
                std::size_t required = 0;
                if (!rgb24Size(w, h, required) || required > writable->pixels.capacity()) {
                    fatal = true;
                    error = {-1, "Amcam_PullImageV4",
                             "SDK frame dimensions exceed the allocated RGB24 buffer."};
                    break;
                }

                // Keep the logical byte count equal to this frame's dimensions,
                // while retaining vector capacity for later frames of this size.
                writable->pixels.resize(required);

                writable->width = w;
                writable->height = h;
                writable->strideBytes = w * 3U;
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
        impl_->previewResolutions.clear();
    }

    // Amcam_Close ends callback production. Wait for callbacks that entered
    // before Close returned before releasing any callback/session state.
    impl_->waitForSdkCallbacks();
    impl_->eventQueue.clearPending();
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
    impl_->stopRequested.store(false, std::memory_order_release);
    impl_->dropped.store(0, std::memory_order_relaxed);

    int width = 0, height = 0;
    HRESULT hr = S_OK;
    {
        std::lock_guard<std::mutex> api(impl_->apiMutex);
        if (!impl_->handle) {
            impl_->state.store(CameraState::Closed, std::memory_order_release);
            return fail(error, "start", -1, "Camera handle is not open.");
        }
        hr = Amcam_get_FinalSize(impl_->handle, &width, &height);
    }
    if (FAILED(hr)) {
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return failHr(error, "Amcam_get_FinalSize", hr);
    }

    std::size_t bytes = 0;
    if (width <= 0 || height <= 0 ||
        !rgb24Size(static_cast<std::uint32_t>(width),
                   static_cast<std::uint32_t>(height), bytes)) {
        impl_->state.store(CameraState::Open, std::memory_order_release);
        return fail(error, "start", -1, "The SDK returned invalid final image dimensions.");
    }

    try {
        impl_->pool = std::make_shared<Impl::FramePool>(
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), bytes);
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


camera::CameraFeatureResult<std::vector<camera::CameraFeatureDescriptor>>
AmcamCamera::featureDescriptors() {
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    return buildAmcamFeatureDescriptors(
        impl_->handle,
        impl_->state.load(std::memory_order_acquire),
        impl_->modelFlags,
        impl_->hasModelDescriptor,
        impl_->maximumSpeedLevel,
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
    auto described = buildAmcamFeatureDescriptors(
        impl_->handle,
        impl_->state.load(std::memory_order_acquire),
        impl_->modelFlags,
        impl_->hasModelDescriptor,
        impl_->maximumSpeedLevel,
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
