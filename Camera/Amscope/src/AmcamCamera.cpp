#define AMCAM_HRESULT_ERRORCODE_NEEDED
#include "amcam.h"
#include "amscope/AmcamCamera.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr unsigned kWaitTimeoutMs = 50;
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
} // namespace

namespace amscope {
struct AmcamCamera::Impl {
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
    std::atomic<bool> disconnected{false};
    std::atomic<bool> sdkError{false};
    std::atomic<std::uint64_t> dropped{0};

    std::mutex lifecycleMutex;
    std::mutex apiMutex;
    HAmcam handle = nullptr;
    std::thread worker;
    std::shared_ptr<FramePool> pool;
    FrameCallback onFrame;
    ErrorCallback onError;

    bool onWorkerThread() const noexcept { return g_acquisitionOwner == this; }

    static void __stdcall sdkEvent(unsigned event, void* context) {
        auto* self = static_cast<Impl*>(context);
        if (!self) return;
        // SDK-owned callback thread: atomics only. Never Stop/Close here.
        if (event == AMCAM_EVENT_DISCONNECTED)
            self->disconnected.store(true, std::memory_order_release);
        else if (event == AMCAM_EVENT_ERROR)
            self->sdkError.store(true, std::memory_order_release);
    }

    void reportFatal(CameraError error) noexcept {
        if (stopRequested.load(std::memory_order_acquire)) return;

        {
            std::lock_guard<std::mutex> lock(apiMutex);
            if (handle && sdkStarted.load(std::memory_order_acquire)) {
                const HRESULT hr = Amcam_Stop(handle);
                if (SUCCEEDED(hr))
                    sdkStarted.store(false, std::memory_order_release);
                else
                    error.message += " " + hrMessage("Amcam_Stop", hr);
            }
        }

        if (stopRequested.load(std::memory_order_acquire)) return;
        state.store(CameraState::Faulted, std::memory_order_release);
        try {
            if (onError) onError(error);
        } catch (...) {
            // Observer exceptions must not unwind through the acquisition thread.
        }
    }

    void acquisitionLoop(const std::shared_ptr<FramePool>& activePool) noexcept {
        g_acquisitionOwner = this;
        bool fatal = false;
        CameraError error;

        while (!stopRequested.load(std::memory_order_acquire)) {
            auto writable = activePool->tryAcquire();
            void* destination = writable
                ? static_cast<void*>(writable->pixels.data())
                : static_cast<void*>(activePool->discard.data());

            AmcamFrameInfoV4 info{};
            HRESULT hr = E_FAIL;
            {
                std::lock_guard<std::mutex> lock(apiMutex);
                if (!handle) {
                    fatal = true;
                    error = {-1, "Amcam_WaitImageV4", "Camera handle is no longer open."};
                } else {
                    hr = Amcam_WaitImageV4(handle, kWaitTimeoutMs, destination,
                                           0, kRgb24Bits, kPackedRows, &info);
                }
            }
            if (fatal || stopRequested.load(std::memory_order_acquire)) break;

            if (disconnected.load(std::memory_order_acquire)) {
                fatal = true;
                error = {static_cast<std::int32_t>(E_GEN_FAILURE),
                         "Amcam_WaitImageV4", "The camera reported a disconnect."};
                break;
            }
            if (sdkError.load(std::memory_order_acquire)) {
                fatal = true;
                error = {static_cast<std::int32_t>(E_FAIL),
                         "Amcam_WaitImageV4", "The camera SDK reported a generic device error."};
                break;
            }
            if (hr == E_TIMEOUT) continue;
            if (FAILED(hr)) {
                fatal = true;
                error = {static_cast<std::int32_t>(hr),
                         "Amcam_WaitImageV4", hrMessage("Amcam_WaitImageV4", hr)};
                break;
            }

            // Keep draining the SDK when all leases are retained. A slow
            // consumer drops preview frames rather than blocking acquisition.
            if (!writable) {
                dropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            const auto w = info.v3.width;
            const auto h = info.v3.height;
            std::size_t required = 0;
            if (!rgb24Size(w, h, required) || required > writable->pixels.capacity()) {
                fatal = true;
                error = {-1, "Amcam_WaitImageV4",
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

        if (fatal) reportFatal(std::move(error));
        g_acquisitionOwner = nullptr;
    }

    bool stopLocked(CameraError* error) {
        const CameraState before = state.load(std::memory_order_acquire);
        if (before == CameraState::Closed || before == CameraState::Open) return true;

        const bool alreadyFaulted = before == CameraState::Faulted;
        if (!alreadyFaulted) state.store(CameraState::Stopping, std::memory_order_release);
        stopRequested.store(true, std::memory_order_release);

        if (worker.joinable()) worker.join();

        HRESULT hr = S_OK;
        {
            std::lock_guard<std::mutex> lock(apiMutex);
            if (handle && sdkStarted.load(std::memory_order_acquire)) {
                hr = Amcam_Stop(handle);
                if (SUCCEEDED(hr))
                    sdkStarted.store(false, std::memory_order_release);
            }
        }

        pool.reset();
        onFrame = {};
        onError = {};
        if (FAILED(hr)) {
            state.store(CameraState::Faulted, std::memory_order_release);
            return failHr(error, "Amcam_Stop", hr);
        }
        state.store(alreadyFaulted ? CameraState::Faulted : CameraState::Open,
                    std::memory_order_release);
        return true;
    }
};

AmcamCamera::AmcamCamera() : impl_(std::make_unique<Impl>()) {}
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
            if (device.model->name) info.modelName = device.model->name;
            info.modelFlags = device.model->flag;
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

    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    if (impl_->state.load(std::memory_order_acquire) != CameraState::Closed)
        return fail(error, "open", -1, "The camera must be closed before opening.");

    std::lock_guard<std::mutex> api(impl_->apiMutex);
    impl_->handle = Amcam_Open(deviceId.c_str());
    if (!impl_->handle)
        return fail(error, "Amcam_Open", -1,
                    "Amcam_Open failed. Re-enumerate devices and check that the camera is not in use.");

    impl_->stopRequested.store(false, std::memory_order_release);
    impl_->sdkStarted.store(false, std::memory_order_release);
    impl_->disconnected.store(false, std::memory_order_release);
    impl_->sdkError.store(false, std::memory_order_release);
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
    if (impl_->state.load(std::memory_order_acquire) == CameraState::Closed) return true;

    const bool stopped = impl_->stopLocked(error);
    {
        std::lock_guard<std::mutex> api(impl_->apiMutex);
        if (impl_->handle) {
            Amcam_Close(impl_->handle);
            impl_->handle = nullptr;
        }
    }
    impl_->pool.reset();
    impl_->onFrame = {};
    impl_->onError = {};
    impl_->sdkStarted.store(false, std::memory_order_release);
    impl_->state.store(CameraState::Closed, std::memory_order_release);
    return stopped;
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

    impl_->state.store(CameraState::Starting, std::memory_order_release);
    impl_->stopRequested.store(false, std::memory_order_release);
    impl_->disconnected.store(false, std::memory_order_release);
    impl_->sdkError.store(false, std::memory_order_release);
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
        HRESULT stopHr = S_OK;
        {
            std::lock_guard<std::mutex> api(impl_->apiMutex);
            if (impl_->handle && impl_->sdkStarted.load(std::memory_order_acquire)) {
                stopHr = Amcam_Stop(impl_->handle);
                if (SUCCEEDED(stopHr))
                    impl_->sdkStarted.store(false, std::memory_order_release);
            }
        }
        impl_->pool.reset();
        impl_->onFrame = {};
        impl_->onError = {};
        impl_->state.store(FAILED(stopHr) ? CameraState::Faulted : CameraState::Open,
                           std::memory_order_release);
        std::string message = std::string("Could not create acquisition worker: ") + ex.what();
        if (FAILED(stopHr)) message += " " + hrMessage("Amcam_Stop", stopHr);
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
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "setAutoExposure", -1, "The camera must be open.");
    const HRESULT hr = Amcam_put_AutoExpoEnable(impl_->handle, enabled ? 1 : 0);
    return FAILED(hr) ? failHr(error, "Amcam_put_AutoExpoEnable", hr) : true;
}

bool AmcamCamera::setExposureTime(std::uint32_t time, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "setExposureTime", -1, "The camera must be open.");
    const HRESULT hr = Amcam_put_ExpoTime(impl_->handle, time);
    return FAILED(hr) ? failHr(error, "Amcam_put_ExpoTime", hr) : true;
}

bool AmcamCamera::setExposureGain(std::uint16_t gain, CameraError* error) {
    if (error) *error = {};
    std::lock_guard<std::mutex> api(impl_->apiMutex);
    const auto s = impl_->state.load(std::memory_order_acquire);
    if (!impl_->handle || (s != CameraState::Open && s != CameraState::Streaming))
        return fail(error, "setExposureGain", -1, "The camera must be open.");
    const HRESULT hr = Amcam_put_ExpoAGain(impl_->handle, gain);
    return FAILED(hr) ? failHr(error, "Amcam_put_ExpoAGain", hr) : true;
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

    settings.automatic = automatic != 0;
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
} // namespace amscope
