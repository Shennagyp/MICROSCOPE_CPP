#include "amscope/AmcamCamera.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t deviceIndex = 0;
    std::uint32_t width = 640;
    std::uint32_t height = 480;
    double warmupSeconds = 3.0;
    double durationSeconds = 15.0;
    double targetFps = 100.0;
};

void printUsage(const char* executable) {
    std::cout
        << "Usage: " << executable
        << " [--device-index N] [--resolution WIDTHxHEIGHT]"
        << " [--warmup SECONDS] [--duration SECONDS] [--target-fps FPS]\n\n"
        << "Runs a physical-camera live preview performance test. The selected\n"
        << "resolution is restored after acquisition. The preview window must\n"
        << "remain visible during the measurement so paint FPS is meaningful.\n";
}

bool parseUnsigned(const std::string& text, std::uint32_t& value) {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} &&
           parsed.ptr == text.data() + text.size();
}

bool parseSize(const std::string& text, std::size_t& value) {
    std::uint32_t parsed = 0;
    if (!parseUnsigned(text, parsed)) return false;
    value = parsed;
    return true;
}

bool parseDouble(const std::string& text, double& value) {
    try {
        std::size_t consumed = 0;
        value = std::stod(text, &consumed);
        return consumed == text.size() &&
               value >= 0.0 &&
               value <= 3600.0;
    } catch (...) {
        return false;
    }
}

bool parseResolution(const std::string& text,
                     std::uint32_t& width,
                     std::uint32_t& height) {
    const auto separator = text.find_first_of("xX");
    if (separator == std::string::npos) return false;
    return parseUnsigned(text.substr(0, separator), width) &&
           parseUnsigned(text.substr(separator + 1), height) &&
           width > 0 && height > 0;
}

bool parseArguments(int argc, char** argv, Options& options, bool& showHelp) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            showHelp = true;
            return true;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << argument << '\n';
            return false;
        }

        const std::string value = argv[++i];
        if (argument == "--device-index") {
            if (!parseSize(value, options.deviceIndex)) {
                std::cerr << "Invalid device index: " << value << '\n';
                return false;
            }
        } else if (argument == "--resolution") {
            if (!parseResolution(value, options.width, options.height)) {
                std::cerr << "Invalid resolution: " << value << '\n';
                return false;
            }
        } else if (argument == "--warmup") {
            if (!parseDouble(value, options.warmupSeconds) ||
                options.warmupSeconds > 120.0) {
                std::cerr << "Warmup must be between 0 and 120 seconds.\n";
                return false;
            }
        } else if (argument == "--duration") {
            if (!parseDouble(value, options.durationSeconds) ||
                options.durationSeconds < 1.0) {
                std::cerr << "Duration must be between 1 and 3600 seconds.\n";
                return false;
            }
        } else if (argument == "--target-fps") {
            if (!parseDouble(value, options.targetFps) ||
                options.targetFps <= 0.0) {
                std::cerr << "Target FPS must be greater than zero.\n";
                return false;
            }
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return false;
        }
    }
    return true;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char c : value) {
        switch (c) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (c < 0x20) {
                output << "\\u"
                       << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned>(c)
                       << std::dec;
            } else {
                output << static_cast<char>(c);
            }
        }
    }
    return output.str();
}

std::string resolutionLabel(std::uint32_t width, std::uint32_t height) {
    return std::to_string(width) + "x" + std::to_string(height);
}

const camera::CameraFeatureDescriptor* findFeature(
    const std::vector<camera::CameraFeatureDescriptor>& descriptors,
    const std::string& name) {
    for (const auto& descriptor : descriptors) {
        if (descriptor.name == name)
            return &descriptor;
    }
    return nullptr;
}

bool featureValueError(
    const camera::CameraFeatureResult<camera::CameraFeatureValue>& result,
    std::string& message) {
    if (const auto* error =
            std::get_if<camera::CameraFeatureAccessError>(&result)) {
        message = error->feature + ": " + error->message;
        return true;
    }
    return false;
}

bool writeFeature(
    amscope::AmcamCamera& camera,
    const std::string& name,
    const camera::CameraFeatureValue& value,
    std::string& message) {
    const auto result = camera.writeFeature(name, value);
    if (const auto* error =
            std::get_if<camera::CameraFeatureAccessError>(&result)) {
        message = error->feature + ": " + error->message;
        return false;
    }
    return true;
}

bool restoreResolution(
    amscope::AmcamCamera& camera,
    const std::string& originalSymbol,
    std::string& message) {
    if (camera.state() != amscope::CameraState::Open) {
        message = "Camera is not in the stopped Open state; original resolution cannot be restored.";
        return false;
    }
    if (!writeFeature(
            camera, "AmcamResolutionIndex",
            camera::FeatureEnumeration{originalSymbol}, message))
        return false;

    const auto readback = camera.readFeature("AmcamResolutionIndex");
    if (const auto* error =
            std::get_if<camera::CameraFeatureAccessError>(&readback)) {
        message = error->feature + ": " + error->message;
        return false;
    }
    const auto* enumeration =
        std::get_if<camera::FeatureEnumeration>(
            &std::get<camera::CameraFeatureValue>(readback));
    if (!enumeration || enumeration->symbol != originalSymbol) {
        message = "Original resolution readback did not match the saved value.";
        return false;
    }
    return true;
}

struct CaptureState {
    struct FrameSnapshot {
        amscope::FramePtr frame;
        std::uint64_t revision = 0;
    };

    std::mutex mutex;
    amscope::FramePtr latestFrame;
    std::uint64_t latestFrameRevision = 0;
    bool measuring = false;
    std::uint64_t callbackFrames = 0;
    bool hasSequence = false;
    std::uint32_t firstSequence = 0;
    std::uint32_t lastSequence = 0;
    bool hasTimestamp = false;
    std::uint64_t firstTimestamp = 0;
    std::uint64_t lastTimestamp = 0;
    std::uint64_t timestampSamples = 0;
    std::uint32_t lastWidth = 0;
    std::uint32_t lastHeight = 0;
    std::uint64_t exposureSum = 0;
    std::uint32_t exposureMinimum = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t exposureMaximum = 0;
    std::uint64_t exposureSamples = 0;
    std::string asynchronousError;

    FrameSnapshot takeLatestFrame() {
        std::lock_guard<std::mutex> lock(mutex);
        return {latestFrame, latestFrameRevision};
    }
};

class HardwarePreview final : public QWidget {
public:
    explicit HardwarePreview(QWidget* parent = nullptr)
        : QWidget(parent) {
        setWindowTitle(QStringLiteral("AmScope hardware rate test"));
        resize(960, 720);
        setMinimumSize(640, 480);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    void setFrame(amscope::FramePtr frame, std::uint64_t revision) {
        if (!frame || revision == 0 || revision == lastSubmittedRevision_)
            return;

        lastSubmittedRevision_ = revision;
        image_ = QImage{};
        frame_ = std::move(frame);
        message_.clear();

        if (frame_->pixelFormat == amscope::PixelFormat::RGB24 &&
            frame_->data() && frame_->width > 0 && frame_->height > 0 &&
            frame_->width <= static_cast<std::uint32_t>(
                std::numeric_limits<int>::max()) &&
            frame_->height <= static_cast<std::uint32_t>(
                std::numeric_limits<int>::max()) &&
            frame_->strideBytes <= static_cast<std::uint32_t>(
                std::numeric_limits<int>::max())) {
            image_ = QImage(
                frame_->data(),
                static_cast<int>(frame_->width),
                static_cast<int>(frame_->height),
                static_cast<int>(frame_->strideBytes),
                QImage::Format_RGB888);
        }

        if (image_.isNull()) {
            hasUnpaintedFrame_ = false;
            message_ = QStringLiteral(
                "Frame received, but this test renderer supports RGB24 only.");
        } else {
            // A host delivery revision identifies new frames independently of
            // the device SDK's optional/stationary sequence metadata.
            hasUnpaintedFrame_ = true;
        }
        update();
    }

    void setStatus(QString status) {
        status_ = std::move(status);
        update();
    }

    std::uint64_t paintedFrames() const noexcept {
        return paintedFrames_.load(std::memory_order_relaxed);
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        event->ignore();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(23, 25, 29));

        if (!image_.isNull()) {
            QSize targetSize = image_.size();
            targetSize.scale(size(), Qt::KeepAspectRatio);
            const QRect targetRect(
                (width() - targetSize.width()) / 2,
                (height() - targetSize.height()) / 2,
                targetSize.width(),
                targetSize.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(targetRect, image_);

            if (hasUnpaintedFrame_) {
                hasUnpaintedFrame_ = false;
                paintedFrames_.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            painter.setPen(QColor(230, 230, 230));
            painter.drawText(rect().adjusted(20, 20, -20, -20),
                             Qt::AlignCenter | Qt::TextWordWrap,
                             message_.isEmpty()
                                 ? QStringLiteral("Waiting for camera frames...")
                                 : message_);
        }

        painter.setPen(QColor(255, 255, 255));
        painter.fillRect(QRect(0, 0, width(), 34), QColor(0, 0, 0, 150));
        painter.drawText(QRect(12, 0, width() - 24, 34),
                         Qt::AlignVCenter | Qt::AlignLeft, status_);
    }

private:
    // Keep the immutable frame alive while QImage references its pixel buffer.
    amscope::FramePtr frame_;
    QImage image_;
    QString message_ = QStringLiteral("Waiting for camera frames...");
    QString status_ = QStringLiteral("Initializing camera...");
    std::atomic<std::uint64_t> paintedFrames_{0};
    std::uint64_t lastSubmittedRevision_ = 0;
    bool hasUnpaintedFrame_ = false;
};

struct Measurement {
    double durationSeconds = 0.0;
    std::uint64_t imageEvents = 0;
    std::uint64_t framesPulled = 0;
    std::uint64_t callbackFrames = 0;
    std::uint64_t paintedFrames = 0;
    std::uint64_t droppedFrames = 0;
    bool hasSequence = false;
    std::uint32_t firstSequence = 0;
    std::uint32_t lastSequence = 0;
    bool hasTimestamp = false;
    std::uint64_t firstTimestamp = 0;
    std::uint64_t lastTimestamp = 0;
    std::uint64_t timestampSamples = 0;
    bool hasConfiguredExposure = false;
    std::uint32_t configuredExposureMicroseconds = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t exposureMinimum = 0;
    std::uint32_t exposureMaximum = 0;
    double exposureAverage = 0.0;
    std::string asynchronousError;
};

void printReport(
    const Options& options,
    const amscope::CameraInfo& info,
    const std::string& selectedSymbol,
    const std::string& originalSymbol,
    const Measurement& measurement,
    bool dimensionsMatch,
    bool stopSucceeded,
    bool resolutionRestored,
    bool closeSucceeded) {
    const auto safeDuration = measurement.durationSeconds > 0.0
        ? measurement.durationSeconds : 1.0;
    const double eventFps =
        static_cast<double>(measurement.imageEvents) / safeDuration;
    const double pullFps =
        static_cast<double>(measurement.framesPulled) / safeDuration;
    const double callbackFps =
        static_cast<double>(measurement.callbackFrames) / safeDuration;
    const double paintFps =
        static_cast<double>(measurement.paintedFrames) / safeDuration;
    const double configuredExposureFpsLimit =
        measurement.hasConfiguredExposure &&
        measurement.configuredExposureMicroseconds > 0
            ? 1000000.0 /
              static_cast<double>(measurement.configuredExposureMicroseconds)
            : 0.0;
    const bool operationalPass =
        measurement.callbackFrames > 0 &&
        dimensionsMatch &&
        measurement.paintedFrames > 0 &&
        stopSucceeded &&
        resolutionRestored &&
        closeSucceeded &&
        measurement.asynchronousError.empty();

    const char* result = !operationalPass
        ? "FAIL"
        : (pullFps >= options.targetFps ? "PASS" : "BELOW_TARGET");

    std::cout << std::fixed << std::setprecision(3);
    std::cout
        << "{\n"
        << "  \"result\": \"" << result << "\",\n"
        << "  \"device_name\": \"" << jsonEscape(info.displayName) << "\",\n"
        << "  \"model_name\": \"" << jsonEscape(info.modelName) << "\",\n"
        << "  \"model_code\": \"" << jsonEscape(info.modelCode) << "\",\n"
        << "  \"model_flags\": " << info.modelFlags << ",\n"
        << "  \"model_maximum_speed_level\": "
        << (info.maximumSpeedLevel
                ? std::to_string(*info.maximumSpeedLevel) : "null") << ",\n"
        << "  \"requested_resolution\": \""
        << resolutionLabel(options.width, options.height) << "\",\n"
        << "  \"selected_resolution_symbol\": \""
        << jsonEscape(selectedSymbol) << "\",\n"
        << "  \"original_resolution_symbol\": \""
        << jsonEscape(originalSymbol) << "\",\n"
        << "  \"resolution_restored\": "
        << (resolutionRestored ? "true" : "false") << ",\n"
        << "  \"measurement_duration_seconds\": "
        << measurement.durationSeconds << ",\n"
        << "  \"event_fps\": " << eventFps << ",\n"
        << "  \"pull_fps\": " << pullFps << ",\n"
        << "  \"callback_fps\": " << callbackFps << ",\n"
        << "  \"paint_fps\": " << paintFps << ",\n"
        << "  \"target_fps\": " << options.targetFps << ",\n"
        << "  \"pull_fps_target_met\": "
        << (pullFps >= options.targetFps ? "true" : "false") << ",\n"
        << "  \"image_events\": " << measurement.imageEvents << ",\n"
        << "  \"frames_pulled\": " << measurement.framesPulled << ",\n"
        << "  \"frames_delivered\": " << measurement.callbackFrames << ",\n"
        << "  \"frames_painted\": " << measurement.paintedFrames << ",\n"
        << "  \"dropped_frames\": " << measurement.droppedFrames << ",\n"
        << "  \"frame_width\": " << measurement.width << ",\n"
        << "  \"frame_height\": " << measurement.height << ",\n"
        << "  \"requested_dimensions_match\": "
        << (dimensionsMatch ? "true" : "false") << ",\n"
        << "  \"first_sequence\": "
        << (measurement.hasSequence
                ? std::to_string(measurement.firstSequence) : "null") << ",\n"
        << "  \"last_sequence\": "
        << (measurement.hasSequence
                ? std::to_string(measurement.lastSequence) : "null") << ",\n"
        << "  \"sequence_changed\": "
        << (measurement.hasSequence &&
            measurement.firstSequence != measurement.lastSequence
                ? "true" : "false") << ",\n"
        << "  \"first_timestamp_us\": "
        << (measurement.hasTimestamp
                ? std::to_string(measurement.firstTimestamp) : "null") << ",\n"
        << "  \"last_timestamp_us\": "
        << (measurement.hasTimestamp
                ? std::to_string(measurement.lastTimestamp) : "null") << ",\n"
        << "  \"timestamp_samples\": " << measurement.timestampSamples << ",\n"
        << "  \"configured_exposure_us\": "
        << (measurement.hasConfiguredExposure
                ? std::to_string(measurement.configuredExposureMicroseconds) : "null") << ",\n"
        << "  \"exposure_fps_limit\": "
        << (configuredExposureFpsLimit > 0.0
                ? std::to_string(configuredExposureFpsLimit) : "null") << ",\n"
        << "  \"sdk_exposure_samples\": " << measurement.exposureSamples << ",\n"
        << "  \"exposure_us_min\": " << measurement.exposureMinimum << ",\n"
        << "  \"exposure_us_max\": " << measurement.exposureMaximum << ",\n"
        << "  \"exposure_us_average\": " << measurement.exposureAverage << ",\n"
        << "  \"stop_succeeded\": " << (stopSucceeded ? "true" : "false") << ",\n"
        << "  \"close_succeeded\": " << (closeSucceeded ? "true" : "false") << ",\n"
        << "  \"asynchronous_error\": \""
        << jsonEscape(measurement.asynchronousError) << "\"\n"
        << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    bool showHelp = false;
    if (!parseArguments(argc, argv, options, showHelp))
        return 2;
    if (showHelp)
        return 0;

    // Do not let QApplication consume the hardware-test command-line options.
    int qtArgc = 1;
    char* qtArgv[] = {argv[0], nullptr};
    QApplication application(qtArgc, qtArgv);

    amscope::CameraError enumerationError;
    const auto cameras = amscope::AmcamCamera::enumerate(&enumerationError);
    if (!enumerationError.message.empty()) {
        std::cerr << "Camera enumeration failed: "
                  << enumerationError.message << '\n';
        return 3;
    }
    if (cameras.empty()) {
        std::cerr << "No AmScope camera found.\n";
        return 3;
    }
    if (options.deviceIndex >= cameras.size()) {
        std::cerr << "Device index " << options.deviceIndex
                  << " is out of range; " << cameras.size()
                  << " camera(s) were found.\n";
        return 3;
    }

    const auto& info = cameras[options.deviceIndex];
    std::cerr << "Selected camera: "
              << (info.displayName.empty() ? info.modelName : info.displayName)
              << " [" << info.modelCode << "]\n";

    amscope::AmcamCamera camera;
    amscope::CameraError cameraError;
    if (!camera.open(info.id, &cameraError)) {
        std::cerr << "Camera open failed: " << cameraError.message << '\n';
        return 4;
    }

    const std::string featureName = "AmcamResolutionIndex";
    const auto descriptorResult = camera.featureDescriptors();
    const auto* descriptors =
        std::get_if<std::vector<camera::CameraFeatureDescriptor>>(
            &descriptorResult);
    if (!descriptors) {
        const auto& error =
            std::get<camera::CameraFeatureAccessError>(descriptorResult);
        std::cerr << "Feature discovery failed: " << error.message << '\n';
        camera.close();
        return 5;
    }


    const auto* rawDescriptor = findFeature(*descriptors, "AmcamRawOutputEnabled");
    if (rawDescriptor && rawDescriptor->isAvailable && rawDescriptor->isReadable) {
        const auto rawResult = camera.readFeature("AmcamRawOutputEnabled");
        if (const auto* rawError =
                std::get_if<camera::CameraFeatureAccessError>(&rawResult)) {
            std::cerr << "Could not read RAW state: " << rawError->message << '\n';
            camera.close();
            return 5;
        }
        const auto* rawEnabled =
            std::get_if<bool>(&std::get<camera::CameraFeatureValue>(rawResult));
        if (rawEnabled && *rawEnabled) {
            std::cerr << "This performance test measures the RGB24 preview path. "
                      << "Disable RAW Sensor Output in the preview first, then rerun.\n";
            camera.close();
            return 5;
        }
    }

    const auto* resolutionDescriptor = findFeature(*descriptors, featureName);
    if (!resolutionDescriptor ||
        !resolutionDescriptor->isAvailable ||
        !resolutionDescriptor->isReadable ||
        !resolutionDescriptor->isWritable) {
        std::cerr << "The camera does not expose a writable live-resolution feature.\n";
        camera.close();
        return 5;
    }

    const auto currentResult = camera.readFeature(featureName);
    std::string featureMessage;
    if (featureValueError(currentResult, featureMessage)) {
        std::cerr << "Could not read the current resolution: "
                  << featureMessage << '\n';
        camera.close();
        return 5;
    }
    const auto* currentEnumeration =
        std::get_if<camera::FeatureEnumeration>(
            &std::get<camera::CameraFeatureValue>(currentResult));
    if (!currentEnumeration) {
        std::cerr << "The current resolution value is not an enumeration.\n";
        camera.close();
        return 5;
    }
    const std::string originalSymbol = currentEnumeration->symbol;

    const auto requestedLabel = resolutionLabel(options.width, options.height);
    std::string selectedSymbol;
    for (const auto& symbol : resolutionDescriptor->enumerationValues) {
        const auto separator = symbol.find(':');
        if (separator != std::string::npos &&
            symbol.substr(separator + 1) == requestedLabel) {
            selectedSymbol = symbol;
            break;
        }
    }
    if (selectedSymbol.empty()) {
        std::cerr << "The camera does not advertise live resolution "
                  << requestedLabel << ". Available modes:\n";
        for (const auto& symbol : resolutionDescriptor->enumerationValues)
            std::cerr << "  " << symbol << '\n';
        camera.close();
        return 6;
    }

    const bool resolutionChanged = selectedSymbol != originalSymbol;
    if (resolutionChanged &&
        !writeFeature(camera, featureName,
                      camera::FeatureEnumeration{selectedSymbol},
                      featureMessage)) {
        std::cerr << "Could not select " << requestedLabel
                  << ": " << featureMessage << '\n';
        if (resolutionChanged) {
            std::string restoreMessage;
            if (!restoreResolution(camera, originalSymbol, restoreMessage))
                std::cerr << "Original resolution restore failed: "
                          << restoreMessage << '\n';
        }
        camera.close();
        return 6;
    }

    const auto selectedReadback = camera.readFeature(featureName);
    if (featureValueError(selectedReadback, featureMessage)) {
        std::cerr << "Resolution readback failed: " << featureMessage << '\n';
        if (resolutionChanged) {
            std::string restoreMessage;
            if (!restoreResolution(camera, originalSymbol, restoreMessage))
                std::cerr << "Original resolution restore failed: "
                          << restoreMessage << '\n';
        }
        camera.close();
        return 6;
    }
    const auto* selectedEnumeration =
        std::get_if<camera::FeatureEnumeration>(
            &std::get<camera::CameraFeatureValue>(selectedReadback));
    if (!selectedEnumeration || selectedEnumeration->symbol != selectedSymbol) {
        std::cerr << "Resolution readback did not match the requested mode.\n";
        if (resolutionChanged) {
            std::string restoreMessage;
            if (!restoreResolution(camera, originalSymbol, restoreMessage))
                std::cerr << "Original resolution restore failed: "
                          << restoreMessage << '\n';
        }
        camera.close();
        return 6;
    }

    bool hasConfiguredExposure = false;
    std::uint32_t configuredExposureMicroseconds = 0;
    const auto exposureResult = camera.readFeature("ExposureTime");
    if (const auto* exposureValue =
            std::get_if<camera::CameraFeatureValue>(&exposureResult)) {
        if (const auto* exposureInteger =
                std::get_if<std::int64_t>(exposureValue)) {
            if (*exposureInteger >= 0 &&
                *exposureInteger <= std::numeric_limits<std::uint32_t>::max()) {
                hasConfiguredExposure = true;
                configuredExposureMicroseconds =
                    static_cast<std::uint32_t>(*exposureInteger);
            }
        }
    } else {
        const auto& exposureError =
            std::get<camera::CameraFeatureAccessError>(exposureResult);
        std::cerr << "Exposure feature readback unavailable: "
                  << exposureError.message << '\n';
    }

    auto capture = std::make_shared<CaptureState>();
    HardwarePreview preview;
    preview.setStatus(QStringLiteral("Warming up | %1 | %2")
        .arg(QString::fromStdString(info.modelName))
        .arg(QString::fromStdString(requestedLabel)));
    preview.show();

    QTimer framePoll;
    framePoll.setTimerType(Qt::PreciseTimer);
    framePoll.setInterval(4);
    QObject::connect(&framePoll, &QTimer::timeout, [&]() {
        const auto snapshot = capture->takeLatestFrame();
        preview.setFrame(snapshot.frame, snapshot.revision);
    });
    framePoll.start();

    const bool started = camera.start(
        [capture](amscope::FramePtr frame) {
            if (!frame) return;
            std::lock_guard<std::mutex> lock(capture->mutex);
            capture->latestFrame = frame;
            ++capture->latestFrameRevision;
            if (!capture->measuring) return;

            ++capture->callbackFrames;
            if (!capture->hasSequence) {
                capture->firstSequence = frame->sequence;
                capture->hasSequence = true;
            }
            capture->lastSequence = frame->sequence;
            if (frame->timestampMicroseconds != 0) {
                if (!capture->hasTimestamp) {
                    capture->firstTimestamp = frame->timestampMicroseconds;
                    capture->hasTimestamp = true;
                }
                capture->lastTimestamp = frame->timestampMicroseconds;
                ++capture->timestampSamples;
            }
            capture->lastWidth = frame->width;
            capture->lastHeight = frame->height;
            if (frame->exposureMicroseconds > 0) {
                capture->exposureSum += frame->exposureMicroseconds;
                ++capture->exposureSamples;
                capture->exposureMinimum = std::min(
                    capture->exposureMinimum, frame->exposureMicroseconds);
                capture->exposureMaximum = std::max(
                    capture->exposureMaximum, frame->exposureMicroseconds);
            }
        },
        [capture](const amscope::CameraError& error) {
            std::lock_guard<std::mutex> lock(capture->mutex);
            capture->asynchronousError =
                error.operation + ": " + error.message;
        },
        &cameraError);

    if (!started) {
        std::cerr << "Camera Start failed: " << cameraError.message << '\n';
        if (resolutionChanged) {
            std::string restoreMessage;
            if (!restoreResolution(camera, originalSymbol, restoreMessage))
                std::cerr << "Original resolution restore failed: "
                          << restoreMessage << '\n';
        }
        camera.close();
        return 7;
    }

    std::uint64_t eventStart = 0;
    std::uint64_t pullStart = 0;
    std::uint64_t dropStart = 0;
    std::uint64_t paintStart = 0;
    Clock::time_point measurementStart;
    bool measurementFinished = false;
    int exitCode = 0;

    QTimer::singleShot(
        static_cast<int>(options.warmupSeconds * 1000.0),
        &application,
        [&]() {
            {
                std::lock_guard<std::mutex> lock(capture->mutex);
                capture->measuring = true;
                capture->callbackFrames = 0;
                capture->hasSequence = false;
                capture->hasTimestamp = false;
                capture->firstTimestamp = 0;
                capture->lastTimestamp = 0;
                capture->timestampSamples = 0;
                capture->exposureSum = 0;
                capture->exposureSamples = 0;
                capture->exposureMinimum =
                    std::numeric_limits<std::uint32_t>::max();
                capture->exposureMaximum = 0;
            }
            eventStart = camera.imageEventsReceived();
            pullStart = camera.framesPulled();
            dropStart = camera.droppedFrames();
            paintStart = preview.paintedFrames();
            measurementStart = Clock::now();

            preview.setStatus(QStringLiteral(
                "MEASURING %1 | %2 seconds | keep this window visible")
                .arg(QString::fromStdString(requestedLabel))
                .arg(options.durationSeconds, 0, 'f', 1));

            QTimer::singleShot(
                static_cast<int>(options.durationSeconds * 1000.0),
                &application,
                [&]() {
                    if (measurementFinished) return;
                    measurementFinished = true;
                    const auto measurementEnd = Clock::now();
                    const double elapsed =
                        std::chrono::duration<double>(
                            measurementEnd - measurementStart).count();

                    Measurement measurement;
                    measurement.durationSeconds = elapsed;
                    measurement.imageEvents =
                        camera.imageEventsReceived() - eventStart;
                    measurement.framesPulled =
                        camera.framesPulled() - pullStart;
                    measurement.droppedFrames =
                        camera.droppedFrames() - dropStart;
                    measurement.paintedFrames =
                        preview.paintedFrames() - paintStart;

                    {
                        std::lock_guard<std::mutex> lock(capture->mutex);
                        capture->measuring = false;
                        measurement.callbackFrames = capture->callbackFrames;
                        measurement.hasSequence = capture->hasSequence;
                        measurement.firstSequence = capture->firstSequence;
                        measurement.lastSequence = capture->lastSequence;
                        measurement.hasTimestamp = capture->hasTimestamp;
                        measurement.firstTimestamp = capture->firstTimestamp;
                        measurement.lastTimestamp = capture->lastTimestamp;
                        measurement.timestampSamples = capture->timestampSamples;
                        measurement.hasConfiguredExposure = hasConfiguredExposure;
                        measurement.configuredExposureMicroseconds =
                            configuredExposureMicroseconds;
                        measurement.width = capture->lastWidth;
                        measurement.height = capture->lastHeight;
                        measurement.asynchronousError =
                            capture->asynchronousError;
                        if (capture->exposureSamples > 0) {
                            measurement.exposureMinimum =
                                capture->exposureMinimum;
                            measurement.exposureMaximum =
                                capture->exposureMaximum;
                            measurement.exposureAverage =
                                static_cast<double>(capture->exposureSum) /
                                static_cast<double>(capture->exposureSamples);
                        }
                    }

                    preview.setStatus(QStringLiteral("Measurement complete; stopping camera..."));
                    const bool stopSucceeded = camera.stop(&cameraError);
                    if (!stopSucceeded) {
                        std::cerr << "Camera Stop failed: "
                                  << cameraError.message << '\n';
                    }

                    bool resolutionRestored = !resolutionChanged;
                    if (resolutionChanged) {
                        std::string restoreMessage;
                        resolutionRestored = restoreResolution(
                            camera, originalSymbol, restoreMessage);
                        if (!resolutionRestored) {
                            std::cerr << "Resolution restore failed: "
                                      << restoreMessage << '\n';
                        }
                    }

                    std::string closeMessage;
                    const bool closeSucceeded = camera.close(&cameraError);
                    if (!closeSucceeded) {
                        closeMessage = cameraError.message;
                        std::cerr << "Camera Close failed: "
                                  << closeMessage << '\n';
                    }

                    const bool dimensionsMatch =
                        measurement.width == options.width &&
                        measurement.height == options.height;

                    printReport(
                        options, info, selectedSymbol, originalSymbol,
                        measurement, dimensionsMatch, stopSucceeded,
                        resolutionRestored, closeSucceeded);

                    const double pullFps = measurement.durationSeconds > 0.0
                        ? static_cast<double>(measurement.framesPulled) /
                          measurement.durationSeconds
                        : 0.0;
                    const bool operationalPass =
                        measurement.callbackFrames > 0 &&
                        dimensionsMatch &&
                        measurement.paintedFrames > 0 &&
                        stopSucceeded &&
                        resolutionRestored &&
                        closeSucceeded &&
                        measurement.asynchronousError.empty();
                    if (!operationalPass)
                        exitCode = 8;
                    else if (pullFps < options.targetFps)
                        exitCode = 10;

                    preview.setStatus(QStringLiteral("Test complete | close this window"));
                    framePoll.stop();
                    application.quit();
                });
        });

    application.exec();
    return exitCode;
}
