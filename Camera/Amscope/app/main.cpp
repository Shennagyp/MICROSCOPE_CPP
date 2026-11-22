#include "amscope/QtAmcamCamera.h"
#include "camera/FrameRateEstimator.h"

#include <QByteArray>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QColor>
#include <QPushButton>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::string utf8String(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString featureNameText(const camera::CameraFeatureDescriptor& descriptor) {
    QString result = QString::fromStdString(descriptor.displayName);
    if (descriptor.unit && !descriptor.unit->empty()) {
        result += QStringLiteral(" (") +
                  QString::fromStdString(*descriptor.unit) +
                  QStringLiteral(")");
    }
    return result;
}

QString numericRangeText(const camera::CameraFeatureDescriptor& descriptor) {
    if (!descriptor.numericRange) return {};
    return QStringLiteral("%1 … %2")
        .arg(descriptor.numericRange->minimum, 0, 'g', 12)
        .arg(descriptor.numericRange->maximum, 0, 'g', 12);
}

QString pixelFormatName(camera::PixelFormat format) {
    switch (format) {
    case camera::PixelFormat::Unknown: return QStringLiteral("Unknown");
    case camera::PixelFormat::RGB24: return QStringLiteral("RGB24");
    case camera::PixelFormat::Raw8: return QStringLiteral("Raw8");
    case camera::PixelFormat::Raw16: return QStringLiteral("Raw16");
    }
    return QStringLiteral("Unknown");
}

QString pixelFormatIdentifierText(const camera::Frame& frame) {
    if (!frame.pixelFormatIdentifier)
        return QStringLiteral("unavailable");
    return QStringLiteral("%1:0x%2")
        .arg(QString::fromStdString(frame.pixelFormatIdentifier->scheme))
        .arg(static_cast<qulonglong>(frame.pixelFormatIdentifier->value), 0, 16);
}

QJsonValue optionalUnsignedJson(
    const std::optional<std::uint64_t>& value) {
    if (!value)
        return QJsonValue(QJsonValue::Null);
    // Store unsigned 64-bit values as decimal strings to avoid JSON double
    // precision loss and signed qint64 overflow.
    return QJsonValue(QString::number(static_cast<qulonglong>(*value)));
}

QJsonValue optionalStringJson(const std::optional<std::string>& value) {
    return value ? QJsonValue(QString::fromStdString(*value))
                 : QJsonValue(QJsonValue::Null);
}

// FrameView holds the immutable frame lease while QImage references its pixel
// buffer. Rendering reads the pooled storage directly: no detached QImage,
// per-frame pixel copy, or intermediate QPixmap.
class FrameView final : public QWidget {
public:
    explicit FrameView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(640, 480);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    void setMessage(QString message) {
        image_ = QImage{};
        frame_.reset();
        hasUnpaintedFrame_ = false;
        message_ = std::move(message);
        update();
    }

    void setFramePresentedCallback(std::function<void()> callback) {
        framePresentedCallback_ = std::move(callback);
    }

    void setFrame(amscope::FramePtr frame) {
        image_ = QImage{};
        frame_ = std::move(frame);
        message_.clear();

        const bool validDimensions =
            frame_ && frame_->data() && frame_->width > 0 && frame_->height > 0 &&
            frame_->width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
            frame_->height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
            frame_->strideBytes <= static_cast<std::uint32_t>(std::numeric_limits<int>::max());

        if (validDimensions) {
            const std::uint64_t minimumRowBytes =
                static_cast<std::uint64_t>(frame_->width) *
                (frame_->pixelFormat == amscope::PixelFormat::RGB24 ? 3U :
                 frame_->pixelFormat == amscope::PixelFormat::Raw16 ? 2U : 1U);
            const std::uint64_t requiredBytes =
                static_cast<std::uint64_t>(frame_->strideBytes) * frame_->height;
            const bool validBuffer =
                frame_->strideBytes >= minimumRowBytes &&
                requiredBytes <= frame_->byteSize();

            if (validBuffer) {
                const int width = static_cast<int>(frame_->width);
                const int height = static_cast<int>(frame_->height);
                const int stride = static_cast<int>(frame_->strideBytes);
                switch (frame_->pixelFormat) {
                case amscope::PixelFormat::RGB24:
                    image_ = QImage(frame_->data(), width, height, stride,
                                    QImage::Format_RGB888);
                    break;
                case amscope::PixelFormat::Raw8:
                    // Display the sensor mosaic as monochrome samples. This is
                    // a preview-only interpretation; the leased RAW bytes remain
                    // untouched for capture and downstream image processing.
                    image_ = QImage(frame_->data(), width, height, stride,
                                    QImage::Format_Grayscale8);
                    break;
                case amscope::PixelFormat::Raw16:
                    // The SDK is configured for native byte order on this
                    // platform. Keep the original 16-bit sample buffer intact.
                    image_ = QImage(frame_->data(), width, height, stride,
                                    QImage::Format_Grayscale16);
                    break;
                case amscope::PixelFormat::Unknown:
                    break;
                }
            }
        }

        if (image_.isNull()) {
            const QString frameDescription = frame_
                ? QStringLiteral("%1, %2x%3, stride: %4")
                      .arg(pixelFormatName(frame_->pixelFormat))
                      .arg(frame_->width)
                      .arg(frame_->height)
                      .arg(frame_->strideBytes)
                : QStringLiteral("invalid frame");
            frame_.reset();
            hasUnpaintedFrame_ = false;
            message_ = QStringLiteral("No renderable frame (%1)").arg(frameDescription);
        } else {
            // Count a frame when this newly submitted image is actually painted.
            // Do not use the SDK sequence field as a uniqueness token: some
            // supported devices leave AmcamFrameInfoV3::seq at zero.
            hasUnpaintedFrame_ = true;
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(23, 25, 29));

        if (image_.isNull()) {
            painter.setPen(QColor(214, 217, 223));
            painter.drawText(rect().adjusted(16, 16, -16, -16),
                             Qt::AlignCenter | Qt::TextWordWrap,
                             message_);
            return;
        }

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
            if (framePresentedCallback_)
                framePresentedCallback_();
        }
    }

private:
    // frame_ owns external pixels for the lifetime of image_.
    amscope::FramePtr frame_;
    QImage image_;
    QString message_ = QStringLiteral("Select a camera and press Open, then Start.");
    std::function<void()> framePresentedCallback_;
    bool hasUnpaintedFrame_ = false;
};

int listCameras() {
    amscope::CameraError error;
    const auto cameras = amscope::QtAmcamCamera::enumerate(&error);
    if (!error.message.empty()) {
        std::cerr << error.message << '\n';
        return 1;
    }

    std::cout << cameras.size() << " camera(s) found\n";
    for (const auto& camera : cameras) {
        const std::string label = !camera.displayName.empty()
                                      ? camera.displayName
                                      : camera.modelName;
        std::cout << "  " << label << " [" << camera.id << "]\n";
        for (std::size_t i = 0; i < camera.previewResolutions.size(); ++i) {
            const auto& resolution = camera.previewResolutions[i];
            std::cout << "    " << i << ": "
                      << resolution.width << 'x' << resolution.height << '\n';
        }
    }
    return 0;
}

class PreviewWindow final : public QMainWindow {
public:
    PreviewWindow() {
        setWindowTitle("AmScope Native Camera Preview");
        resize(1280, 820);

        auto* central = new QWidget(this);
        auto* root = new QVBoxLayout(central);
        auto* toolbar = new QHBoxLayout();

        cameraSelector_ = new QComboBox(central);
        cameraSelector_->setMinimumWidth(320);

        refreshCamerasButton_ = new QPushButton("Refresh cameras", central);
        openButton_ = new QPushButton("Open", central);
        startButton_ = new QPushButton("Start", central);
        stopButton_ = new QPushButton("Stop", central);
        closeButton_ = new QPushButton("Close", central);
        stillCaptureModeSelector_ = new QComboBox(central);
        stillResolutionSelector_ = new QComboBox(central);
        stillResolutionSelector_->setToolTip("Still-image resolution reported by the camera SDK.");
        stillCaptureModeSelector_->addItem(
            "Processed still", static_cast<int>(amscope::StillCaptureMode::Processed));
        stillCaptureModeSelector_->addItem(
            "RAW still", static_cast<int>(amscope::StillCaptureMode::Raw));
        captureStillButton_ = new QPushButton("Capture still", central);

        toolbar->addWidget(cameraSelector_, 1);
        toolbar->addWidget(refreshCamerasButton_);
        toolbar->addWidget(openButton_);
        toolbar->addWidget(startButton_);
        toolbar->addWidget(stopButton_);
        toolbar->addWidget(closeButton_);
        toolbar->addWidget(stillResolutionSelector_);
        toolbar->addWidget(stillCaptureModeSelector_);
        toolbar->addWidget(captureStillButton_);

        preview_ = new FrameView(central);

        featureStatus_ = new QLabel("Open a camera to inspect its available features.", central);
        featureStatus_->setWordWrap(true);

        refreshFeaturesButton_ = new QPushButton("Refresh features", central);
        featureScroll_ = new QScrollArea(central);
        featureScroll_->setMinimumWidth(340);
        featureScroll_->setMaximumWidth(460);
        featureScroll_->setWidgetResizable(true);

        featureContent_ = new QWidget(featureScroll_);
        featureForm_ = new QFormLayout(featureContent_);
        featureForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        featureForm_->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        featureScroll_->setWidget(featureContent_);

        accessoryGroup_ = new QGroupBox("Camera accessories", central);
        accessoryForm_ = new QFormLayout(accessoryGroup_);

        embeddedLightSourceRow_ = new QWidget(accessoryGroup_);
        auto* lightSourceLayout = new QHBoxLayout(embeddedLightSourceRow_);
        lightSourceLayout->setContentsMargins(0, 0, 0, 0);
        embeddedLightSourceSlider_ = new QSlider(Qt::Horizontal, embeddedLightSourceRow_);
        embeddedLightSourceSlider_->setRange(0, 0);
        embeddedLightSourceSlider_->setTracking(false);
        embeddedLightSourceValueLabel_ = new QLabel("—", embeddedLightSourceRow_);
        embeddedLightSourceApplyButton_ = new QPushButton("Apply", embeddedLightSourceRow_);
        lightSourceLayout->addWidget(embeddedLightSourceSlider_, 1);
        lightSourceLayout->addWidget(embeddedLightSourceValueLabel_);
        lightSourceLayout->addWidget(embeddedLightSourceApplyButton_);
        accessoryForm_->addRow("Embedded light source", embeddedLightSourceRow_);

        filterWheelRow_ = new QWidget(accessoryGroup_);
        auto* filterWheelLayout = new QHBoxLayout(filterWheelRow_);
        filterWheelLayout->setContentsMargins(0, 0, 0, 0);
        filterWheelPositionSelector_ = new QSpinBox(filterWheelRow_);
        filterWheelPositionSelector_->setPrefix("Slot ");
        filterWheelPositionSelector_->setRange(0, 0);
        filterWheelDirectionSelector_ = new QComboBox(filterWheelRow_);
        filterWheelDirectionSelector_->addItem(
            "Clockwise", static_cast<int>(amscope::FilterWheelDirection::Clockwise));
        filterWheelDirectionSelector_->addItem(
            "Automatic", static_cast<int>(amscope::FilterWheelDirection::Automatic));
        filterWheelMoveButton_ = new QPushButton("Move", filterWheelRow_);
        filterWheelRefreshButton_ = new QPushButton("Refresh", filterWheelRow_);
        filterWheelStatusLabel_ = new QLabel("Position unknown", filterWheelRow_);
        filterWheelLayout->addWidget(filterWheelPositionSelector_);
        filterWheelLayout->addWidget(filterWheelDirectionSelector_);
        filterWheelLayout->addWidget(filterWheelMoveButton_);
        filterWheelLayout->addWidget(filterWheelRefreshButton_);
        accessoryForm_->addRow("Filter wheel", filterWheelRow_);

        astroAutoFocuserGroup_ = new QGroupBox("Astronomy auto-focuser", central);
        auto* astroFocuserForm = new QFormLayout(astroAutoFocuserGroup_);
        astroAutoFocuserRow_ = new QWidget(astroAutoFocuserGroup_);
        auto* astroFocuserLayout = new QHBoxLayout(astroAutoFocuserRow_);
        astroFocuserLayout->setContentsMargins(0, 0, 0, 0);
        astroAutoFocuserTargetSelector_ = new QSpinBox(astroAutoFocuserRow_);
        astroAutoFocuserTargetSelector_->setRange(0, 0);
        astroAutoFocuserMoveButton_ = new QPushButton("Move", astroAutoFocuserRow_);
        astroAutoFocuserHaltButton_ = new QPushButton("Halt", astroAutoFocuserRow_);
        astroAutoFocuserRefreshButton_ = new QPushButton("Refresh", astroAutoFocuserRow_);
        astroFocuserLayout->addWidget(astroAutoFocuserTargetSelector_, 1);
        astroFocuserLayout->addWidget(astroAutoFocuserMoveButton_);
        astroFocuserLayout->addWidget(astroAutoFocuserHaltButton_);
        astroFocuserLayout->addWidget(astroAutoFocuserRefreshButton_);
        astroFocuserForm->addRow("Target position", astroAutoFocuserRow_);
        astroAutoFocuserStatusLabel_ = new QLabel("Position unknown", astroAutoFocuserGroup_);
        astroAutoFocuserStatusLabel_->setWordWrap(true);
        astroFocuserForm->addRow("Status", astroAutoFocuserStatusLabel_);

        lensFocusGroup_ = new QGroupBox("Lens focus motor", central);
        auto* lensFocusForm = new QFormLayout(lensFocusGroup_);
        lensFocusRow_ = new QWidget(lensFocusGroup_);
        auto* lensFocusLayout = new QHBoxLayout(lensFocusRow_);
        lensFocusLayout->setContentsMargins(0, 0, 0, 0);
        lensFocusTargetSelector_ = new QSpinBox(lensFocusRow_);
        lensFocusTargetSelector_->setRange(0, 0);
        lensFocusMoveButton_ = new QPushButton("Move", lensFocusRow_);
        lensFocusRefreshButton_ = new QPushButton("Refresh", lensFocusRow_);
        lensFocusLayout->addWidget(lensFocusTargetSelector_, 1);
        lensFocusLayout->addWidget(lensFocusMoveButton_);
        lensFocusLayout->addWidget(lensFocusRefreshButton_);
        lensFocusForm->addRow("Manual focus position", lensFocusRow_);
        lensApertureRow_ = new QWidget(lensFocusGroup_);
        auto* lensApertureLayout = new QHBoxLayout(lensApertureRow_);
        lensApertureLayout->setContentsMargins(0, 0, 0, 0);
        lensApertureSelector_ = new QComboBox(lensApertureRow_);
        lensApertureSetButton_ = new QPushButton("Set", lensApertureRow_);
        lensApertureLayout->addWidget(lensApertureSelector_, 1);
        lensApertureLayout->addWidget(lensApertureSetButton_);
        lensFocusForm->addRow("Aperture position", lensApertureRow_);
        lensFocusStatusLabel_ = new QLabel("Lens state unknown", lensFocusGroup_);
        lensFocusStatusLabel_->setWordWrap(true);
        lensFocusForm->addRow("Lens / AF state", lensFocusStatusLabel_);

        triggerGroup_ = new QGroupBox("Trigger configuration", central);
        auto* triggerForm = new QFormLayout(triggerGroup_);
        triggerModeSelector_ = new QComboBox(triggerGroup_);
        triggerForm->addRow("Acquisition mode", triggerModeSelector_);

        triggerSourceRow_ = new QWidget(triggerGroup_);
        auto* triggerSourceLayout = new QHBoxLayout(triggerSourceRow_);
        triggerSourceLayout->setContentsMargins(0, 0, 0, 0);
        triggerSourceSelector_ = new QComboBox(triggerSourceRow_);
        triggerSourceLayout->addWidget(triggerSourceSelector_, 1);
        triggerForm->addRow("External trigger source", triggerSourceRow_);

        triggerHeartbeatRow_ = new QWidget(triggerGroup_);
        auto* heartbeatLayout = new QHBoxLayout(triggerHeartbeatRow_);
        heartbeatLayout->setContentsMargins(0, 0, 0, 0);
        triggerHeartbeatInterval_ = new QSpinBox(triggerHeartbeatRow_);
        triggerHeartbeatInterval_->setRange(100, 10000);
        triggerHeartbeatInterval_->setSingleStep(100);
        triggerHeartbeatInterval_->setSuffix(" ms");
        heartbeatLayout->addWidget(triggerHeartbeatInterval_);
        triggerForm->addRow("Heartbeat interval", triggerHeartbeatRow_);

        auto* triggerConfigurationRow = new QWidget(triggerGroup_);
        auto* triggerConfigurationLayout = new QHBoxLayout(triggerConfigurationRow);
        triggerConfigurationLayout->setContentsMargins(0, 0, 0, 0);
        triggerApplyButton_ = new QPushButton("Apply while stopped", triggerConfigurationRow);
        triggerRefreshButton_ = new QPushButton("Refresh", triggerConfigurationRow);
        triggerConfigurationLayout->addWidget(triggerApplyButton_);
        triggerConfigurationLayout->addWidget(triggerRefreshButton_);
        triggerForm->addRow("Configuration", triggerConfigurationRow);

        auto* softwareTriggerRow = new QWidget(triggerGroup_);
        auto* softwareTriggerLayout = new QHBoxLayout(softwareTriggerRow);
        softwareTriggerLayout->setContentsMargins(0, 0, 0, 0);
        softwareTriggerCount_ = new QSpinBox(softwareTriggerRow);
        softwareTriggerCount_->setRange(1, 65534);
        softwareTriggerCount_->setValue(1);
        softwareTriggerButton_ = new QPushButton("Send software trigger", softwareTriggerRow);
        softwareTriggerLayout->addWidget(softwareTriggerCount_);
        softwareTriggerLayout->addWidget(softwareTriggerButton_);
        triggerForm->addRow("Finite image burst", softwareTriggerRow);

        triggerStatusLabel_ = new QLabel("Trigger configuration not loaded.", triggerGroup_);
        triggerStatusLabel_->setWordWrap(true);
        triggerForm->addRow("Status", triggerStatusLabel_);
        triggerGroup_->hide();

        lensFocusGroup_->hide();

        astroAutoFocuserGroup_->hide();        lensFocusGroup_->hide();

        astroAutoFocuserGroup_->hide();

        accessoryGroup_->hide();

        auto* settingsColumn = new QVBoxLayout();
        settingsColumn->addWidget(featureStatus_);
        settingsColumn->addWidget(refreshFeaturesButton_);
        settingsColumn->addWidget(triggerGroup_);
        settingsColumn->addWidget(accessoryGroup_);
        settingsColumn->addWidget(astroAutoFocuserGroup_);
        settingsColumn->addWidget(lensFocusGroup_);
        settingsColumn->addWidget(featureScroll_, 1);

        auto* workspace = new QHBoxLayout();
        workspace->addWidget(preview_, 1);
        workspace->addLayout(settingsColumn);

        frameInfo_ = new QLabel("No frames received", central);
        preview_->setFramePresentedCallback([this]() {
            const double monotonicSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (const auto estimate =
                    previewFrameRateEstimator_.record(monotonicSeconds)) {
                previewFramesPerSecond_ = *estimate;
            }
        });
        frameRateTimer_ = new QTimer(this);
        frameRateTimer_->setInterval(1000);
        connect(frameRateTimer_, &QTimer::timeout, this, [this]() {
            updatePulledFrameRate();
        });

        root->addLayout(toolbar);
        root->addLayout(workspace, 1);
        root->addWidget(frameInfo_);
        setCentralWidget(central);

        connect(refreshCamerasButton_, &QPushButton::clicked, this, [this]() {
            refreshCameras();
        });
        connect(openButton_, &QPushButton::clicked, this, [this]() {
            openCamera();
        });
        connect(startButton_, &QPushButton::clicked, this, [this]() {
            startCamera();
        });
        connect(stopButton_, &QPushButton::clicked, this, [this]() {
            stopCamera();
        });
        connect(closeButton_, &QPushButton::clicked, this, [this]() {
            closeCamera();
        });
        connect(captureStillButton_, &QPushButton::clicked, this, [this]() {
            captureStill();
        });
        connect(refreshFeaturesButton_, &QPushButton::clicked, this, [this]() {
            refreshFeatureDescriptors();
        });

        connect(embeddedLightSourceSlider_, &QSlider::valueChanged,
                this, [this](int level) {
            embeddedLightSourceValueLabel_->setText(QString::number(level));
        });
        connect(embeddedLightSourceApplyButton_, &QPushButton::clicked,
                this, [this]() { applyEmbeddedLightSourceLevel(); });
        connect(filterWheelMoveButton_, &QPushButton::clicked,
                this, [this]() { submitFilterWheelMove(); });
        connect(filterWheelRefreshButton_, &QPushButton::clicked,
                this, [this]() { refreshFilterWheelState(); });

        connect(astroAutoFocuserMoveButton_, &QPushButton::clicked,
                this, [this]() { moveAstroAutoFocuser(); });
        connect(astroAutoFocuserHaltButton_, &QPushButton::clicked,
                this, [this]() { haltAstroAutoFocuser(); });
        connect(astroAutoFocuserRefreshButton_, &QPushButton::clicked,
                this, [this]() { refreshAstroAutoFocuserState(); });
        connect(lensFocusMoveButton_, &QPushButton::clicked,
                this, [this]() { moveLensFocus(); });
        connect(lensFocusRefreshButton_, &QPushButton::clicked,
                this, [this]() { refreshLensFocusState(); });
        connect(lensApertureSetButton_, &QPushButton::clicked,
                this, [this]() { setLensAperturePosition(); });

        connect(triggerModeSelector_, &QComboBox::currentIndexChanged,
                this, [this](int) { updateTriggerModeWidgets(); });
        connect(triggerApplyButton_, &QPushButton::clicked,
                this, [this]() { applyTriggerConfiguration(); });
        connect(triggerRefreshButton_, &QPushButton::clicked,
                this, [this]() { refreshTriggerConfiguration(); });
        connect(softwareTriggerButton_, &QPushButton::clicked,
                this, [this]() { sendSoftwareTrigger(); });

        connect(&camera_, &amscope::QtAmcamCamera::frameAvailable,
                this, [this]() {
            presentLatestFrame();
        });
        connect(&camera_, &amscope::QtAmcamCamera::stateChanged,
                this, [this](amscope::CameraState state) {
            // Any lifecycle notification supersedes an in-flight device
            // discovery, including failed transitions that leave the camera Closed.
            if (cameraEnumerationPending_) {
                ++cameraEnumerationGeneration_;
                cameraEnumerationPending_ = false;
            }
            if (state == amscope::CameraState::Streaming) {
                if (!frameRateTimer_->isActive()) {
                    lastPulledFrameCount_ = camera_.framesPulled();
                    lastImageEventCount_ = camera_.imageEventsReceived();
                    lastPullRateAt_ = std::chrono::steady_clock::now();
                    pulledFramesPerSecond_.reset();
                    imageEventsPerSecond_.reset();
                    frameRateTimer_->start();
                }
                // Do not rebuild the full feature inventory while acquisition
                // is active: feature probes share the SDK lock with PullImage.
                setFeatureControlsEnabled(false);
                featureStatus_->setText(
                    "Acquisition active. Stop streaming to refresh or edit camera settings.");
            } else {
                frameRateTimer_->stop();
                lastPullRateAt_.reset();
                imageEventsPerSecond_.reset();
                pulledFramesPerSecond_.reset();
                resetPreviewFrameRate();
                updateFrameInfoLabel();
            }
            updateControls(state);
            if (state == amscope::CameraState::Open) {
                refreshFeatureDescriptors();
            } else if (state == amscope::CameraState::Closed ||
                       state == amscope::CameraState::Faulted) {
                ++featureUiGeneration_;
                pendingFeatureWrite_ = false;
                clearFeatureControls();
                featureStatus_->setText(
                    state == amscope::CameraState::Closed
                        ? "Open a camera to inspect its available features."
                        : "Camera feature controls are unavailable in the current state.");
            }
        });
        connect(&camera_, &amscope::QtAmcamCamera::errorOccurred,
                this, [this](const QString& message, qint32) {
            statusBar()->showMessage(message);
        });

        refreshCameras();
        updateControls(camera_.state());
    }

    ~PreviewWindow() override {
        ++featureUiGeneration_;
        ++lifecycleUiGeneration_;
        ++cameraEnumerationGeneration_;
        // QtAmcamCamera shuts down its request worker, joins it, then closes
        // the native session. Do not race a synchronous Close from this UI.
    }

private:
    struct FeatureControl {
        camera::CameraFeatureDescriptor descriptor;
        QWidget* editor = nullptr;
        bool hasCurrentValue = false;
    };

    void refreshCameras() {
        if (camera_.state() != amscope::CameraState::Closed ||
            lifecycleRequestPending_ || cameraEnumerationPending_) {
            return;
        }

        const std::uint64_t generation = ++cameraEnumerationGeneration_;
        cameraEnumerationPending_ = true;
        cameraSelector_->setEnabled(false);
        refreshCamerasButton_->setEnabled(false);
        statusBar()->showMessage("Refreshing cameras…");

        const auto requestId = camera_.requestEnumerateCameras(
            [this, generation](
                amscope::QtAmcamCamera::CameraRequestId,
                amscope::QtAmcamCamera::CameraListResult result) {
                if (generation != cameraEnumerationGeneration_) return;
                cameraEnumerationPending_ = false;
                cameraSelector_->clear();

                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    cameras_.clear();
                    statusBar()->showMessage(QString::fromStdString(error->message));
                    updateControls(camera_.state());
                    return;
                }

                cameras_ =
                    std::get<std::vector<amscope::CameraInfo>>(std::move(result));
                for (const auto& camera : cameras_) {
                    const std::string label = !camera.displayName.empty()
                                                  ? camera.displayName
                                                  : camera.modelName;
                    cameraSelector_->addItem(
                        QString::fromStdString(label),
                        QString::fromStdString(camera.id));
                }

                if (cameras_.empty()) {
                    statusBar()->showMessage("No AmScope camera detected.");
                } else {
                    statusBar()->showMessage(
                        QString("%1 camera(s) detected.")
                            .arg(static_cast<qulonglong>(cameras_.size())));
                }
                updateControls(camera_.state());
            });

        if (requestId == 0) {
            cameraEnumerationPending_ = false;
            statusBar()->showMessage("Camera enumeration could not be queued.");
            updateControls(camera_.state());
        } else {
            updateControls(camera_.state());
        }
    }

    void openCamera() {
        if (lifecycleRequestPending_) return;
        const QString id = cameraSelector_->currentData().toString();
        if (id.isEmpty()) {
            statusBar()->showMessage("Select a camera first.");
            return;
        }

        const std::uint64_t generation = ++lifecycleUiGeneration_;
        lifecycleRequestPending_ = true;
        setFeatureControlsEnabled(false);
        featureStatus_->setText("Opening camera…");
        statusBar()->showMessage("Opening camera…");
        updateControls(camera_.state());

        const auto requestId = camera_.requestOpen(
            id,
            [this, generation](amscope::QtAmcamCamera::CameraRequestId,
                               amscope::QtAmcamCamera::LifecycleResult result) {
                if (generation != lifecycleUiGeneration_) return;
                lifecycleRequestPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    statusBar()->showMessage(QString::fromStdString(error->message));
                } else {
                    preview_->setMessage("Camera opened. Press Start to begin acquisition.");
                    statusBar()->showMessage("Camera session opened.");
                    refreshStillResolutions();
                }
                updateControls(camera_.state());
            });

        if (requestId == 0) {
            lifecycleRequestPending_ = false;
            statusBar()->showMessage("The camera open request could not be queued.");
            updateControls(camera_.state());
        }
    }

    void startCamera() {
        if (lifecycleRequestPending_) return;
        resetPreviewFrameRate();
        const std::uint64_t generation = ++lifecycleUiGeneration_;
        lifecycleRequestPending_ = true;
        setFeatureControlsEnabled(false);
        featureStatus_->setText("Starting acquisition…");
        statusBar()->showMessage("Starting acquisition…");
        updateControls(camera_.state());

        const auto requestId = camera_.requestStart(
            [this, generation](amscope::QtAmcamCamera::CameraRequestId,
                               amscope::QtAmcamCamera::LifecycleResult result) {
                if (generation != lifecycleUiGeneration_) return;
                lifecycleRequestPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    statusBar()->showMessage(QString::fromStdString(error->message));
                } else if (camera_.state() == amscope::CameraState::Streaming) {
                    statusBar()->showMessage("Acquisition started.");
                } else if (camera_.state() == amscope::CameraState::Faulted) {
                    statusBar()->showMessage(
                        "Camera acquisition faulted; close and reopen the camera.");
                }
                // A native acquisition can fail immediately after Start returns.
                // In that case the error/state notification owns the visible status;
                // do not overwrite it with a stale "started" completion.
                updateControls(camera_.state());
            });

        if (requestId == 0) {
            lifecycleRequestPending_ = false;
            statusBar()->showMessage("The acquisition start request could not be queued.");
            setFeatureControlsEnabled(true);
            updateControls(camera_.state());
        }
    }

    void stopCamera() {
        if (lifecycleRequestPending_) return;
        const std::uint64_t generation = ++lifecycleUiGeneration_;
        lifecycleRequestPending_ = true;
        setFeatureControlsEnabled(false);
        featureStatus_->setText("Stopping acquisition…");
        statusBar()->showMessage("Stopping acquisition…");
        updateControls(camera_.state());

        const auto requestId = camera_.requestStop(
            [this, generation](amscope::QtAmcamCamera::CameraRequestId,
                               amscope::QtAmcamCamera::LifecycleResult result) {
                if (generation != lifecycleUiGeneration_) return;
                lifecycleRequestPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    statusBar()->showMessage(QString::fromStdString(error->message));
                } else if (camera_.state() == amscope::CameraState::Open) {
                    statusBar()->showMessage("Acquisition stopped.");
                } else if (camera_.state() == amscope::CameraState::Faulted) {
                    statusBar()->showMessage(
                        "Camera session is faulted; close and reopen the camera.");
                }
                // A Stop that races a native fatal transition may succeed at
                // halting the SDK while the owning camera session remains Faulted.
                updateControls(camera_.state());
            });

        if (requestId == 0) {
            lifecycleRequestPending_ = false;
            statusBar()->showMessage("The acquisition stop request could not be queued.");
            setFeatureControlsEnabled(true);
            updateControls(camera_.state());
        }
    }

    void refreshStillResolutions() {
        ++stillResolutionUiGeneration_;
        stillResolutionsPending_ = true;
        stillResolutionSelector_->clear();
        stillResolutionSelector_->setEnabled(false);
        updateControls(camera_.state());

        const std::uint64_t generation = stillResolutionUiGeneration_;
        const auto requestId = camera_.requestStillResolutions(
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::StillResolutionListResult result) {
                if (generation != stillResolutionUiGeneration_) return;
                stillResolutionsPending_ = false;
                stillResolutionSelector_->clear();

                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    statusBar()->showMessage(QString::fromStdString(error->message));
                    updateControls(camera_.state());
                    return;
                }

                const auto resolutions =
                    std::get<amscope::QtAmcamCamera::StillResolutionList>(
                        std::move(result));
                for (const auto& resolution : resolutions) {
                    const QString label = QString("%1 — %2 × %3")
                        .arg(resolution.index)
                        .arg(resolution.width)
                        .arg(resolution.height);
                    stillResolutionSelector_->addItem(
                        label, static_cast<uint>(resolution.index));
                }

                // Explicitly expose matching the current live dimensions, but
                // default to SDK still index zero so capture is usable even
                // when the live mode has no exact counterpart in the still table.
                if (!resolutions.empty()) {
                    stillResolutionSelector_->addItem(
                        "Match live preview",
                        static_cast<uint>(std::numeric_limits<std::uint32_t>::max()));
                    stillResolutionSelector_->setCurrentIndex(0);
                }
                updateControls(camera_.state());
            });

        if (requestId == 0) {
            stillResolutionsPending_ = false;
            stillResolutionSelector_->clear();
            statusBar()->showMessage("Still-resolution discovery could not be queued.");
            updateControls(camera_.state());
        }
    }

    void captureStill() {
        if (lifecycleRequestPending_ || stillCapturePending_ ||
            camera_.state() != amscope::CameraState::Streaming) {
            return;
        }

        if (stillResolutionSelector_->currentIndex() < 0) {
            statusBar()->showMessage("The camera has no available still-resolution mode.");
            return;
        }

        amscope::StillCaptureRequest request;
        request.mode = static_cast<amscope::StillCaptureMode>(
            stillCaptureModeSelector_->currentData().toInt());
        request.resolutionIndex = stillResolutionSelector_->currentData().toUInt();
        request.timeoutMilliseconds = 30000;

        stillCapturePending_ = true;
        statusBar()->showMessage(
            request.mode == amscope::StillCaptureMode::Raw
                ? "Capturing RAW still image…"
                : "Capturing processed still image…");
        updateControls(camera_.state());

        const auto requestId = camera_.requestStillCapture(
            request,
            [this](amscope::QtAmcamCamera::FeatureRequestId,
                   amscope::QtAmcamCamera::StillCaptureResult result) {
                stillCapturePending_ = false;
                updateControls(camera_.state());

                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    statusBar()->showMessage(QString::fromStdString(error->message));
                    return;
                }

                const amscope::FramePtr frame =
                    std::get<amscope::FramePtr>(std::move(result));
                if (!frame || !frame->data()) {
                    statusBar()->showMessage("Still capture returned an empty frame.");
                    return;
                }

                const bool raw =
                    frame->pixelFormat == camera::PixelFormat::Raw8 ||
                    frame->pixelFormat == camera::PixelFormat::Raw16;
                if (!raw && frame->pixelFormat != camera::PixelFormat::RGB24) {
                    statusBar()->showMessage(
                        "Still capture returned an unknown image-buffer layout.");
                    return;
                }
                const QString suggested = raw
                    ? QString("amscope_%1x%2_sensor%3_storage%4.raw")
                          .arg(frame->width).arg(frame->height)
                          .arg(static_cast<unsigned>(frame->significantBitsPerPixel))
                          .arg(static_cast<unsigned>(frame->storageBitsPerPixel))
                    : QString("amscope_%1x%2.png")
                          .arg(frame->width).arg(frame->height);
                const QString path = QFileDialog::getSaveFileName(
                    this,
                    raw ? "Save RAW still image" : "Save processed still image",
                    suggested,
                    raw ? "RAW sensor data (*.raw)" : "PNG image (*.png)");
                if (path.isEmpty()) {
                    statusBar()->showMessage("Still image captured; save cancelled.");
                    return;
                }

                if (!raw) {
                    const QImage image(
                        frame->data(),
                        static_cast<int>(frame->width),
                        static_cast<int>(frame->height),
                        static_cast<int>(frame->strideBytes),
                        QImage::Format_RGB888);
                    if (!image.copy().save(path, "PNG")) {
                        statusBar()->showMessage("Could not save the processed still image.");
                        return;
                    }
                    statusBar()->showMessage("Processed still image saved.");
                    return;
                }

                QSaveFile rawFile(path);
                const auto byteCount = static_cast<qint64>(frame->byteSize());
                if (!rawFile.open(QIODevice::WriteOnly) ||
                    rawFile.write(reinterpret_cast<const char*>(frame->data()),
                                  byteCount) != byteCount ||
                    !rawFile.commit()) {
                    statusBar()->showMessage("Could not save the RAW still payload.");
                    return;
                }

                const auto amcamFrame =
                    std::dynamic_pointer_cast<const amscope::AmcamFrame>(frame);
                QJsonObject metadata{
                    {"width", static_cast<qint64>(frame->width)},
                    {"height", static_cast<qint64>(frame->height)},
                    {"strideBytes", static_cast<qint64>(frame->strideBytes)},
                    {"pixelFormat", pixelFormatName(frame->pixelFormat)},
                    {"significantBitsPerPixel",
                     static_cast<int>(frame->significantBitsPerPixel)},
                    {"storageBitsPerPixel",
                     static_cast<int>(frame->storageBitsPerPixel)},
                    {"pixelFormatIdentifierScheme",
                     frame->pixelFormatIdentifier
                         ? QJsonValue(QString::fromStdString(
                               frame->pixelFormatIdentifier->scheme))
                         : QJsonValue(QJsonValue::Null)},
                    {"pixelFormatIdentifierValue",
                     frame->pixelFormatIdentifier
                         ? optionalUnsignedJson(frame->pixelFormatIdentifier->value)
                         : QJsonValue(QJsonValue::Null)},
                    {"sequenceNumber", optionalUnsignedJson(frame->sequenceNumber)}
                };

                if (frame->timestamp) {
                    metadata.insert("timestamp", QJsonObject{
                        {"ticks", optionalUnsignedJson(frame->timestamp->ticks)},
                        {"ticksPerSecond",
                         optionalUnsignedJson(frame->timestamp->ticksPerSecond)},
                        {"clockDomain",
                         optionalStringJson(frame->timestamp->clockDomain)}
                    });
                } else {
                    metadata.insert("timestamp", QJsonValue(QJsonValue::Null));
                }

                if (frame->exposure) {
                    metadata.insert("exposure", QJsonObject{
                        {"durationTicks",
                         optionalUnsignedJson(frame->exposure->durationTicks)},
                        {"ticksPerSecond",
                         optionalUnsignedJson(frame->exposure->ticksPerSecond)}
                    });
                } else {
                    metadata.insert("exposure", QJsonValue(QJsonValue::Null));
                }

                if (amcamFrame) {
                    metadata.insert("amcam", QJsonObject{
                        {"sdkFlags", static_cast<qint64>(amcamFrame->sdkFlags)},
                        {"exposureGainPercent",
                         amcamFrame->exposureGainPercent
                             ? QJsonValue(static_cast<int>(
                                   *amcamFrame->exposureGainPercent))
                             : QJsonValue(QJsonValue::Null)},
                        {"blackLevel",
                         amcamFrame->blackLevel
                             ? QJsonValue(static_cast<int>(*amcamFrame->blackLevel))
                             : QJsonValue(QJsonValue::Null)}
                    });
                }
                QSaveFile metadataFile(path + ".json");
                const QByteArray json =
                    QJsonDocument(metadata).toJson(QJsonDocument::Indented);
                if (!metadataFile.open(QIODevice::WriteOnly) ||
                    metadataFile.write(json) != json.size() ||
                    !metadataFile.commit()) {
                    statusBar()->showMessage(
                        "RAW payload saved, but its metadata sidecar could not be saved.");
                    return;
                }
                statusBar()->showMessage("RAW still image and metadata sidecar saved.");
            });

        if (requestId == 0) {
            stillCapturePending_ = false;
            statusBar()->showMessage("The still-capture request could not be queued.");
            updateControls(camera_.state());
        }
    }

    void closeCamera() {
        if (lifecycleRequestPending_) return;
        ++featureUiGeneration_;
        pendingFeatureWrite_ = false;
        const std::uint64_t generation = ++lifecycleUiGeneration_;
        ++stillResolutionUiGeneration_;
        stillResolutionsPending_ = false;
        stillResolutionSelector_->clear();
        lifecycleRequestPending_ = true;
        setFeatureControlsEnabled(false);
        clearFeatureControls();
        featureStatus_->setText("Closing camera session…");
        preview_->setMessage("Closing camera session…");
        statusBar()->showMessage("Closing camera session…");
        updateControls(camera_.state());

        const auto requestId = camera_.requestClose(
            [this, generation](amscope::QtAmcamCamera::CameraRequestId,
                               amscope::QtAmcamCamera::LifecycleResult result) {
                if (generation != lifecycleUiGeneration_) return;
                lifecycleRequestPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    statusBar()->showMessage(QString::fromStdString(error->message));
                    featureStatus_->setText(
                        "Camera feature controls are unavailable in the current state.");
                } else {
                    preview_->setMessage("Select a camera and press Open, then Start.");
                    frameInfo_->setText("No frames received");
                    statusBar()->showMessage("Camera session closed.");
                    featureStatus_->setText(
                        "Open a camera to inspect its available features.");
                }
                updateControls(camera_.state());
            });

        if (requestId == 0) {
            lifecycleRequestPending_ = false;
            statusBar()->showMessage("The camera close request could not be queued.");
            setFeatureControlsEnabled(true);
            updateControls(camera_.state());
        }
    }

    void refreshFeatureDescriptors() {
        if (camera_.state() == amscope::CameraState::Closed) {
            ++featureUiGeneration_;
            pendingFeatureWrite_ = false;
            clearFeatureControls();
            featureStatus_->setText("Open a camera to inspect its available features.");
            return;
        }

        const std::uint64_t generation = ++featureUiGeneration_;
        pendingFeatureReads_ = 0;
        pendingFeatureWrite_ = false;
        embeddedLightSourceReadPending_ = false;
        filterWheelReadPending_ = false;
        embeddedLightSourceStateKnown_ = false;
        filterWheelStateKnown_ = false;
        filterWheelMoving_ = false;
        astroAutoFocuserReadPending_ = false;
        astroAutoFocuserStateKnown_ = false;
        astroAutoFocuserMoving_ = false;
        lensFocusReadPending_ = false;
        lensFocusStateKnown_ = false;
        lensFocusManual_ = false;
        lensApertureCalibrated_ = false;
        lensApertureChoicesAvailable_ = false;
        lensApertureSelector_->clear();
        triggerConfigurationReadPending_ = false;
        triggerConfigurationKnown_ = false;
        triggerOperationPending_ = false;
        currentTriggerConfiguration_.reset();
        triggerGroup_->hide();
        triggerConfigurationReadPending_ = false;
        triggerConfigurationKnown_ = false;
        triggerOperationPending_ = false;
        currentTriggerConfiguration_.reset();
        accessoryOperationPending_ = false;
        accessoryGroup_->hide();
        astroAutoFocuserGroup_->hide();
        lensFocusGroup_->hide();
        setFeatureControlsEnabled(false);
        featureStatus_->setText("Reading feature descriptors…");

        const auto requestId = camera_.requestFeatureDescriptors(
            [this, generation](amscope::QtAmcamCamera::FeatureRequestId,
                               amscope::QtAmcamCamera::FeatureDescriptorsResult result) {
                if (generation != featureUiGeneration_) return;

                if (const auto* error =
                        std::get_if<camera::CameraFeatureAccessError>(&result)) {
                    clearFeatureControls();
                    featureStatus_->setText(
                        QString("Could not read camera features: %1")
                            .arg(QString::fromStdString(error->message)));
                    return;
                }

                const auto descriptors =
                    std::get<amscope::QtAmcamCamera::FeatureDescriptors>(std::move(result));
                rebuildFeatureControls(descriptors);
                rebuildAccessoryControls(descriptors, generation);
                refreshFeatureValues(generation);

                featureStatus_->setText(
                    descriptors.empty()
                        ? "The camera exposed no feature descriptors."
                        : QString("%1 available feature(s).").arg(
                              static_cast<qulonglong>(featureControls_.size())));
            });

        if (requestId == 0) {
            // Do not re-enable controls from the previous descriptor snapshot.
            // The caller can retry after the executor queue drains.
            featureStatus_->setText(
                "Feature request was rejected; retry after the request queue drains.");
            setFeatureControlsEnabled(false);
        }
    }

    void clearFeatureControls() {
        while (featureForm_->rowCount() > 0) {
            featureForm_->removeRow(0);
        }
        featureControls_.clear();
        pendingFeatureReads_ = 0;
        clearAccessoryControls();
    }

    void rebuildFeatureControls(
        const std::vector<camera::CameraFeatureDescriptor>& descriptors) {
        clearFeatureControls();

        for (const auto& descriptor : descriptors) {
            // Unavailable features are intentionally omitted. Availability is
            // resolved by the backend from hardware evidence and model policy.
            if (!descriptor.isAvailable) continue;

            QWidget* editor = createFeatureEditor(descriptor);
            featureForm_->addRow(
                featureNameText(descriptor),
                editor);
            featureControls_.emplace(
                descriptor.name,
                FeatureControl{descriptor, editor});
        }

        // Readable features are populated asynchronously before editing is
        // enabled. This prevents an empty editor from being mistaken for a
        // current camera value.
        setFeatureControlsEnabled(false);
    }

    QWidget* createFeatureEditor(
        const camera::CameraFeatureDescriptor& descriptor) {
        const QString featureName = QString::fromStdString(descriptor.name);
        QWidget* editor = nullptr;

        switch (descriptor.kind) {
        case camera::CameraFeatureKind::Boolean: {
            auto* control = new QCheckBox(featureContent_);
            connect(control, &QCheckBox::toggled, this,
                    [this, name = descriptor.name](bool) {
                submitFeatureWrite(name);
            });
            editor = control;
            break;
        }
        case camera::CameraFeatureKind::Integer: {
            if (!descriptor.discreteIntegerValues.empty()) {
                auto* control = new QComboBox(featureContent_);
                for (const auto item : descriptor.discreteIntegerValues) {
                    control->addItem(
                        QString("%1-bit").arg(static_cast<qlonglong>(item)),
                        QVariant::fromValue<qlonglong>(
                            static_cast<qlonglong>(item)));
                }
                connect(control, qOverload<int>(&QComboBox::currentIndexChanged),
                        this, [this, name = descriptor.name](int index) {
                    if (index >= 0) submitFeatureWrite(name);
                });
                editor = control;
            } else {
                auto* control = new QLineEdit(featureContent_);
                control->setPlaceholderText(numericRangeText(descriptor));
                connect(control, &QLineEdit::editingFinished, this,
                        [this, name = descriptor.name]() {
                    submitFeatureWrite(name);
                });
                editor = control;
            }
            break;
        }
        case camera::CameraFeatureKind::IntegerVector:
        case camera::CameraFeatureKind::String: {
            auto* control = new QLineEdit(featureContent_);
            if (descriptor.kind == camera::CameraFeatureKind::IntegerVector) {
                control->setPlaceholderText(
                    descriptor.elementCount
                        ? QString("%1 comma-separated values")
                              .arg(static_cast<qulonglong>(*descriptor.elementCount))
                        : "Comma-separated values");
            }
            connect(control, &QLineEdit::editingFinished, this,
                    [this, name = descriptor.name]() {
                submitFeatureWrite(name);
            });
            editor = control;
            break;
        }
        case camera::CameraFeatureKind::FloatingPoint: {
            if (descriptor.numericRange) {
                auto* control = new QDoubleSpinBox(featureContent_);
                control->setDecimals(12);
                control->setRange(
                    descriptor.numericRange->minimum,
                    descriptor.numericRange->maximum);
                control->setSingleStep(
                    descriptor.numericRange->increment.value_or(1.0));
                control->setKeyboardTracking(false);
                connect(control, &QDoubleSpinBox::editingFinished, this,
                        [this, name = descriptor.name]() {
                    submitFeatureWrite(name);
                });
                editor = control;
            } else {
                auto* control = new QLineEdit(featureContent_);
                control->setPlaceholderText("Numeric value");
                connect(control, &QLineEdit::editingFinished, this,
                        [this, name = descriptor.name]() {
                    submitFeatureWrite(name);
                });
                editor = control;
            }
            break;
        }
        case camera::CameraFeatureKind::Enumeration: {
            if (descriptor.enumerationValues.empty()) {
                auto* control = new QLineEdit(featureContent_);
                control->setReadOnly(true);
                editor = control;
            } else {
                auto* control = new QComboBox(featureContent_);
                for (const auto& symbol : descriptor.enumerationValues) {
                    const QString item = QString::fromStdString(symbol);
                    control->addItem(item, item);
                }
                connect(control, qOverload<int>(&QComboBox::currentIndexChanged),
                        this, [this, name = descriptor.name](int index) {
                    if (index >= 0) submitFeatureWrite(name);
                });
                editor = control;
            }
            break;
        }
        }

        if (!editor) {
            editor = new QLabel("Unsupported feature type", featureContent_);
        }
        editor->setObjectName(featureName);
        editor->setEnabled(descriptor.isWritable);
        return editor;
    }

    void refreshFeatureValues(std::uint64_t generation) {
        pendingFeatureReads_ = 0;
        for (const auto& entry : featureControls_) {
            if (entry.second.descriptor.isReadable) {
                ++pendingFeatureReads_;
            }
        }

        if (pendingFeatureReads_ == 0) {
            setFeatureControlsEnabled(true);
            return;
        }

        for (const auto& entry : featureControls_) {
            const std::string featureName = entry.first;
            if (!entry.second.descriptor.isReadable) continue;

            const auto requestId = camera_.requestReadFeature(
                QString::fromStdString(featureName),
                [this, generation, featureName](
                    amscope::QtAmcamCamera::FeatureRequestId,
                    amscope::QtAmcamCamera::FeatureReadResult result) {
                    if (generation != featureUiGeneration_) return;

                    const auto found = featureControls_.find(featureName);
                    if (found != featureControls_.end()) {
                        if (const auto* error =
                                std::get_if<camera::CameraFeatureAccessError>(&result)) {
                            found->second.hasCurrentValue = false;
                            found->second.editor->setToolTip(
                                QString::fromStdString(error->message));
                        } else {
                            const auto& value =
                                std::get<camera::CameraFeatureValue>(result);
                            found->second.hasCurrentValue =
                                setFeatureEditorValue(found->second, value);
                            if (!found->second.hasCurrentValue) {
                                found->second.editor->setToolTip(
                                    "The backend returned a value that does not match its descriptor.");
                            } else {
                                found->second.editor->setToolTip(QString{});
                            }
                        }
                    }

                    if (pendingFeatureReads_ > 0) {
                        --pendingFeatureReads_;
                    }
                    if (pendingFeatureReads_ == 0) {
                        setFeatureControlsEnabled(true);
                    }
                });

            if (requestId == 0) {
                // Invalidate already queued read completions. A later refresh
                // will obtain a consistent descriptor/value snapshot.
                ++featureUiGeneration_;
                pendingFeatureReads_ = 0;
                setFeatureControlsEnabled(false);
                featureStatus_->setText(
                    "Feature reads could not be queued; refresh the feature list to retry.");
                break;
            }
        }
    }

    bool setFeatureEditorValue(
        const FeatureControl& control,
        const camera::CameraFeatureValue& value) {
        switch (control.descriptor.kind) {
        case camera::CameraFeatureKind::Boolean: {
            const auto* typed = std::get_if<bool>(&value);
            auto* editor = qobject_cast<QCheckBox*>(control.editor);
            if (!typed || !editor) return false;
            const QSignalBlocker blocker(editor);
            editor->setChecked(*typed);
            return true;
        }
        case camera::CameraFeatureKind::Integer: {
            const auto* typed = std::get_if<std::int64_t>(&value);
            if (!typed) return false;
            if (auto* combo = qobject_cast<QComboBox*>(control.editor)) {
                const QSignalBlocker blocker(combo);
                const int index = combo->findData(
                    QVariant::fromValue<qlonglong>(
                        static_cast<qlonglong>(*typed)));
                combo->setCurrentIndex(index);
                return index >= 0;
            }
            if (auto* editor = qobject_cast<QLineEdit*>(control.editor)) {
                const QSignalBlocker blocker(editor);
                editor->setText(QString::number(static_cast<qlonglong>(*typed)));
                return true;
            }
            return false;
        }
        case camera::CameraFeatureKind::IntegerVector: {
            const auto* typed = std::get_if<std::vector<std::int64_t>>(&value);
            auto* editor = qobject_cast<QLineEdit*>(control.editor);
            if (!typed || !editor) return false;
            QStringList parts;
            for (const auto item : *typed) parts.push_back(QString::number(static_cast<qlonglong>(item)));
            const QSignalBlocker blocker(editor);
            editor->setText(parts.join(", "));
            return true;
        }
        case camera::CameraFeatureKind::FloatingPoint: {
            const auto* typed = std::get_if<double>(&value);
            if (!typed) return false;
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(control.editor)) {
                const QSignalBlocker blocker(spin);
                spin->setValue(*typed);
                return true;
            }
            if (auto* line = qobject_cast<QLineEdit*>(control.editor)) {
                const QSignalBlocker blocker(line);
                line->setText(QString::number(*typed, 'g', 12));
                return true;
            }
            return false;
        }
        case camera::CameraFeatureKind::String: {
            const auto* typed = std::get_if<std::string>(&value);
            auto* editor = qobject_cast<QLineEdit*>(control.editor);
            if (!typed || !editor) return false;
            const QSignalBlocker blocker(editor);
            editor->setText(QString::fromUtf8(
                typed->data(), static_cast<qsizetype>(typed->size())));
            return true;
        }
        case camera::CameraFeatureKind::Enumeration: {
            const auto* typed = std::get_if<camera::FeatureEnumeration>(&value);
            if (!typed) return false;
            if (auto* combo = qobject_cast<QComboBox*>(control.editor)) {
                const QSignalBlocker blocker(combo);
                const int index = combo->findData(QString::fromStdString(typed->symbol));
                combo->setCurrentIndex(index);
                return index >= 0;
            }
            if (auto* line = qobject_cast<QLineEdit*>(control.editor)) {
                const QSignalBlocker blocker(line);
                line->setText(QString::fromStdString(typed->symbol));
                return true;
            }
            return false;
        }
        }
        return false;
    }

    void submitFeatureWrite(const std::string& featureName) {
        if (pendingFeatureWrite_) return;

        const auto found = featureControls_.find(featureName);
        if (found == featureControls_.end() ||
            !found->second.descriptor.isWritable) {
            return;
        }

        const FeatureControl& control = found->second;
        camera::CameraFeatureValue value;
        QString validationMessage;

        switch (control.descriptor.kind) {
        case camera::CameraFeatureKind::Boolean: {
            const auto* editor = qobject_cast<QCheckBox*>(control.editor);
            if (!editor) return;
            value = editor->isChecked();
            break;
        }
        case camera::CameraFeatureKind::Integer: {
            bool ok = false;
            qlonglong number = 0;
            if (const auto* combo = qobject_cast<QComboBox*>(control.editor)) {
                if (combo->currentIndex() >= 0)
                    number = combo->currentData().toLongLong(&ok);
            } else if (const auto* editor =
                           qobject_cast<QLineEdit*>(control.editor)) {
                number = editor->text().trimmed().toLongLong(&ok);
            }
            if (!ok) {
                validationMessage = control.descriptor.discreteIntegerValues.empty()
                    ? "Enter a valid signed integer."
                    : "Select one of the advertised integer values.";
                break;
            }
            value = static_cast<std::int64_t>(number);
            break;
        }
        case camera::CameraFeatureKind::IntegerVector: {
            const auto* editor = qobject_cast<QLineEdit*>(control.editor);
            if (!editor) return;

            std::vector<std::int64_t> values;
            const QStringList parts = editor->text().split(',', Qt::KeepEmptyParts);
            bool valid = !parts.isEmpty();
            for (const auto& part : parts) {
                bool ok = false;
                const qlonglong number = part.trimmed().toLongLong(&ok);
                if (!ok) {
                    valid = false;
                    break;
                }
                values.push_back(static_cast<std::int64_t>(number));
            }
            if (control.descriptor.elementCount &&
                values.size() != *control.descriptor.elementCount) {
                valid = false;
            }
            if (!valid) {
                validationMessage = "Enter the required number of comma-separated integers.";
                break;
            }
            value = std::move(values);
            break;
        }
        case camera::CameraFeatureKind::FloatingPoint: {
            bool ok = false;
            double number = 0.0;
            if (const auto* spin = qobject_cast<QDoubleSpinBox*>(control.editor)) {
                number = spin->value();
                ok = true;
            } else if (const auto* line = qobject_cast<QLineEdit*>(control.editor)) {
                number = line->text().trimmed().toDouble(&ok);
            }
            if (!ok || !std::isfinite(number)) {
                validationMessage = "Enter a finite numeric value.";
                break;
            }
            value = number;
            break;
        }
        case camera::CameraFeatureKind::String: {
            const auto* editor = qobject_cast<QLineEdit*>(control.editor);
            if (!editor) return;
            value = utf8String(editor->text());
            break;
        }
        case camera::CameraFeatureKind::Enumeration: {
            if (const auto* combo = qobject_cast<QComboBox*>(control.editor)) {
                if (combo->currentIndex() < 0) {
                    validationMessage = "Select one of the advertised enumeration values.";
                    break;
                }
                value = camera::FeatureEnumeration{
                    utf8String(combo->currentData().toString())};
            } else {
                validationMessage = "This enumeration has no writable values.";
            }
            break;
        }
        }

        if (!validationMessage.isEmpty()) {
            featureStatus_->setText(validationMessage);
            return;
        }

        pendingFeatureWrite_ = true;
        setFeatureControlsEnabled(false);
        featureStatus_->setText(
            QString("Writing %1…").arg(QString::fromStdString(featureName)));

        const std::uint64_t generation = featureUiGeneration_;
        const auto requestId = camera_.requestWriteFeature(
            QString::fromStdString(featureName),
            std::move(value),
            [this, generation, featureName](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::FeatureWriteResult result) {
                if (generation != featureUiGeneration_) return;

                pendingFeatureWrite_ = false;
                if (const auto* error =
                        std::get_if<camera::CameraFeatureAccessError>(&result)) {
                    featureStatus_->setText(
                        QString("Write failed for %1: %2")
                            .arg(QString::fromStdString(featureName),
                                 QString::fromStdString(error->message)));
                } else {
                    featureStatus_->setText(
                        QString("Updated %1.")
                            .arg(QString::fromStdString(featureName)));
                }

                // Feature writes may change the writable state of related
                // descriptors (for example, automatic/manual exposure).
                refreshFeatureDescriptors();
            });

        if (requestId == 0) {
            pendingFeatureWrite_ = false;
            featureStatus_->setText(
                "The feature write could not be queued.");
            setFeatureControlsEnabled(true);
        }
    }

    void setFeatureControlsEnabled(bool enabled) {
        const bool canEnable = enabled && !lifecycleRequestPending_ &&
                                camera_.state() == amscope::CameraState::Open;
        for (auto& entry : featureControls_) {
            const auto& descriptor = entry.second.descriptor;
            const bool valueIsKnown =
                !descriptor.isReadable || entry.second.hasCurrentValue;
            entry.second.editor->setEnabled(
                canEnable && descriptor.isWritable && valueIsKnown);
        }
        refreshFeaturesButton_->setEnabled(
            canEnable && camera_.state() == amscope::CameraState::Open);
    }

    void updatePulledFrameRate() {
        if (camera_.state() != amscope::CameraState::Streaming)
            return;

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = lastPullRateAt_
            ? std::chrono::duration<double>(now - *lastPullRateAt_).count()
            : 0.0;
        const std::uint64_t pulledTotal = camera_.framesPulled();
        const std::uint64_t eventTotal = camera_.imageEventsReceived();
        if (elapsed > 0.0) {
            if (pulledTotal >= lastPulledFrameCount_) {
                pulledFramesPerSecond_ =
                    static_cast<double>(pulledTotal - lastPulledFrameCount_) / elapsed;
            }
            if (eventTotal >= lastImageEventCount_) {
                imageEventsPerSecond_ =
                    static_cast<double>(eventTotal - lastImageEventCount_) / elapsed;
            }
        }
        lastPulledFrameCount_ = pulledTotal;
        lastImageEventCount_ = eventTotal;
        lastPullRateAt_ = now;
        updateFrameInfoLabel();
    }

    void updateFrameInfoLabel() {
        const QString sequenceText = lastFrameSequence_
            ? QString("sequence %1")
                  .arg(static_cast<qulonglong>(*lastFrameSequence_))
            : QStringLiteral("sequence unavailable");
        QString text = hasReceivedFrame_
            ? QString("%1 x %2 | %3 | %4 bytes")
                  .arg(static_cast<qulonglong>(lastFrameWidth_))
                  .arg(static_cast<qulonglong>(lastFrameHeight_))
                  .arg(sequenceText)
                  .arg(static_cast<qulonglong>(lastFrameBytes_))
            : QStringLiteral("No frames received");

        if (imageEventsPerSecond_) {
            text += QString(" | event %1 FPS")
                .arg(*imageEventsPerSecond_, 0, 'f', 1);
        }
        if (pulledFramesPerSecond_) {
            text += QString(" | pull %1 FPS")
                .arg(*pulledFramesPerSecond_, 0, 'f', 1);
        }
        if (lastExposureMicroseconds_) {
            text += QString(" | exposure %1 us")
                .arg(static_cast<qulonglong>(*lastExposureMicroseconds_));
        }
        if (previewFramesPerSecond_) {
            text += QString(" | paint %1 FPS")
                .arg(*previewFramesPerSecond_, 0, 'f', 1);
        }
        const std::uint64_t dropped = camera_.droppedFrames();
        if (dropped != 0) {
            text += QString(" | dropped %1")
                .arg(static_cast<qulonglong>(dropped));
        }
        frameInfo_->setText(text);
    }

    void presentLatestFrame() {
        amscope::FramePtr frame = camera_.takeLatestFrame();
        if (!frame || frame->data() == nullptr ||
            frame->width == 0 || frame->height == 0) {
            return;
        }

        lastFrameWidth_ = frame->width;
        lastFrameHeight_ = frame->height;
        lastFrameSequence_ = frame->sequenceNumber;
        lastFrameBytes_ = frame->byteSize();
        lastExposureMicroseconds_.reset();
        if (frame->exposure &&
            frame->exposure->ticksPerSecond == 1000000ULL) {
            lastExposureMicroseconds_ = frame->exposure->durationTicks;
        }
        hasReceivedFrame_ = true;

        if (frame->pixelFormat == camera::PixelFormat::Raw8 ||
            frame->pixelFormat == camera::PixelFormat::Raw16) {
            preview_->setMessage(
                QString("RAW sensor stream active | %1 x %2 | source %3-bit / storage %4-bit | %5")
                    .arg(static_cast<qulonglong>(frame->width))
                    .arg(static_cast<qulonglong>(frame->height))
                    .arg(static_cast<unsigned>(frame->significantBitsPerPixel))
                    .arg(static_cast<unsigned>(frame->storageBitsPerPixel))
                    .arg(pixelFormatIdentifierText(*frame)));
            return;
        }
        if (frame->pixelFormat != camera::PixelFormat::RGB24) {
            preview_->setMessage(
                QString("Unknown frame layout | %1 x %2 | %3")
                    .arg(static_cast<qulonglong>(frame->width))
                    .arg(static_cast<qulonglong>(frame->height))
                    .arg(pixelFormatIdentifierText(*frame)));
            return;
        }

        // FrameView retains the frame lease and paints directly from pooled
        // memory. No per-frame image copy or QPixmap conversion.
        preview_->setFrame(std::move(frame));
    }

    void resetPreviewFrameRate() {
        previewFrameRateEstimator_.reset();
        previewFramesPerSecond_.reset();
    }


    void clearAccessoryControls() {
        triggerPanelAvailable_ = false;
        triggerExternalSupported_ = false;
        triggerExternalAllowed_ = false;
        triggerSoftwareSupported_ = false;
        triggerSoftwareAllowed_ = false;
        triggerHardwareEventsAllowed_ = false;
        triggerSingleImage_ = false;
        triggerConfigurationReadPending_ = false;
        triggerConfigurationKnown_ = false;
        triggerOperationPending_ = false;
        currentTriggerConfiguration_.reset();
        triggerGroup_->hide();
        triggerModeSelector_->clear();
        triggerSourceSelector_->clear();
        triggerStatusLabel_->setText("Trigger configuration not loaded.");
        embeddedLightSourceAvailable_ = false;
        filterWheelAvailable_ = false;
        embeddedLightSourceReadPending_ = false;
        filterWheelReadPending_ = false;
        embeddedLightSourceStateKnown_ = false;
        filterWheelStateKnown_ = false;
        filterWheelMoving_ = false;
        astroAutoFocuserAvailable_ = false;
        astroAutoFocuserReadPending_ = false;
        astroAutoFocuserStateKnown_ = false;
        astroAutoFocuserMoving_ = false;
        lensFocusAvailable_ = false;
        lensFocusReadPending_ = false;
        lensFocusStateKnown_ = false;
        lensFocusManual_ = false;
        lensApertureCalibrated_ = false;
        lensApertureChoicesAvailable_ = false;
        accessoryOperationPending_ = false;
        accessoryGroup_->hide();
        lensFocusGroup_->hide();
        lensFocusTargetSelector_->setRange(0, 0);
        lensFocusTargetSelector_->setValue(0);
        lensApertureSelector_->clear();
        lensFocusStatusLabel_->setText("Lens state unknown");
        astroAutoFocuserGroup_->hide();
        astroAutoFocuserTargetSelector_->setRange(0, 0);
        astroAutoFocuserTargetSelector_->setValue(0);
        astroAutoFocuserStatusLabel_->setText("Position unknown");
        embeddedLightSourceSlider_->setRange(0, 0);
        embeddedLightSourceSlider_->setValue(0);
        embeddedLightSourceValueLabel_->setText("—");
        filterWheelPositionSelector_->setRange(0, 0);
        filterWheelPositionSelector_->setValue(0);
        filterWheelStatusLabel_->setText("Position unknown");
    }

    void rebuildAccessoryControls(
        const std::vector<camera::CameraFeatureDescriptor>& descriptors,
        std::uint64_t generation) {
        const auto descriptorFor = [&descriptors](const char* name)
            -> const camera::CameraFeatureDescriptor* {
            const auto item = std::find_if(
                descriptors.begin(), descriptors.end(),
                [name](const auto& descriptor) { return descriptor.name == name; });
            return item == descriptors.end() ? nullptr : &*item;
        };
        const auto available = [&descriptorFor](const char* name) {
            const auto* descriptor = descriptorFor(name);
            return descriptor != nullptr && descriptor->isAvailable;
        };
        const auto supported = [&descriptorFor](const char* name) {
            const auto* descriptor = descriptorFor(name);
            return descriptor != nullptr && descriptor->isReadable;
        };
        triggerExternalSupported_ = supported("AmcamExternalTriggerAvailable");
        triggerExternalAllowed_ = available("AmcamExternalTriggerAvailable");
        triggerSoftwareSupported_ = supported("AmcamSoftwareTriggerAvailable");
        triggerSoftwareAllowed_ = available("AmcamSoftwareTriggerAvailable");
        triggerHardwareEventsAllowed_ = available("AmcamHardwareEventsAvailable");
        triggerSingleImage_ = supported("AmcamSingleTriggerAvailable");
        triggerPanelAvailable_ = triggerExternalSupported_ ||
            triggerSoftwareSupported_ || supported("AmcamHardwareEventsAvailable");
        rebuildTriggerModeChoices();
        triggerGroup_->setVisible(triggerPanelAvailable_);
        embeddedLightSourceAvailable_ =
            available("AmcamEmbeddedLightSourceAvailable");
        filterWheelAvailable_ = available("AmcamFilterWheelAvailable");
        astroAutoFocuserAvailable_ =
            available("AmcamAstroAutoFocuserAvailable");
        lensFocusAvailable_ = available("AmcamFocusMotorAvailable");
        astroAutoFocuserGroup_->setVisible(astroAutoFocuserAvailable_);
        lensFocusGroup_->setVisible(lensFocusAvailable_);

        embeddedLightSourceRow_->setVisible(embeddedLightSourceAvailable_);
        if (QWidget* label = accessoryForm_->labelForField(embeddedLightSourceRow_))
            label->setVisible(embeddedLightSourceAvailable_);
        filterWheelRow_->setVisible(filterWheelAvailable_);
        if (QWidget* label = accessoryForm_->labelForField(filterWheelRow_))
            label->setVisible(filterWheelAvailable_);

        const bool anyAccessory =
            embeddedLightSourceAvailable_ || filterWheelAvailable_ ||
            lensFocusAvailable_;
        accessoryGroup_->setVisible(anyAccessory);
        if (anyAccessory)
            refreshAccessoryState(generation);
        if (astroAutoFocuserAvailable_)
            refreshAstroAutoFocuserState(generation);
        if (triggerPanelAvailable_)
            refreshTriggerConfiguration(generation);
    }

    void refreshAccessoryState(std::uint64_t generation) {
        if (embeddedLightSourceAvailable_)
            refreshEmbeddedLightSource(generation);
        if (filterWheelAvailable_)
            refreshFilterWheelState(generation);
        if (astroAutoFocuserAvailable_)
            refreshAstroAutoFocuserState(generation);
        if (lensFocusAvailable_)
            refreshLensFocusState(generation);
    }

    void refreshEmbeddedLightSource(std::uint64_t generation) {
        if (!embeddedLightSourceAvailable_ || embeddedLightSourceReadPending_)
            return;
        embeddedLightSourceReadPending_ = true;
        updateControls(camera_.state());

        const auto requestId = camera_.requestEmbeddedLightSource(
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::EmbeddedLightSourceResult result) {
                if (generation != featureUiGeneration_) return;
                embeddedLightSourceReadPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    embeddedLightSourceStateKnown_ = false;
                    embeddedLightSourceValueLabel_->setText("Unavailable");
                    featureStatus_->setText(
                        QString("Embedded light-source read failed: %1")
                            .arg(QString::fromStdString(error->message)));
                } else {
                    const auto& state =
                        std::get<amscope::EmbeddedLightSourceState>(result);
                    embeddedLightSourceStateKnown_ = true;
                    {
                        const QSignalBlocker blocker(embeddedLightSourceSlider_);
                        embeddedLightSourceSlider_->setRange(
                            0, static_cast<int>(state.maximumLevel));
                        embeddedLightSourceSlider_->setValue(
                            static_cast<int>(state.level));
                    }
                    embeddedLightSourceValueLabel_->setText(
                        QString("%1 / %2")
                            .arg(static_cast<qulonglong>(state.level))
                            .arg(static_cast<qulonglong>(state.maximumLevel)));
                }
                updateControls(camera_.state());
            });
        if (requestId == 0) {
            embeddedLightSourceReadPending_ = false;
            embeddedLightSourceStateKnown_ = false;
            embeddedLightSourceValueLabel_->setText("Request rejected");
            updateControls(camera_.state());
        }
    }

    void refreshFilterWheelState(std::uint64_t generation) {
        if (!filterWheelAvailable_ || filterWheelReadPending_)
            return;
        filterWheelReadPending_ = true;
        updateControls(camera_.state());

        const auto requestId = camera_.requestFilterWheelState(
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::FilterWheelStateResult result) {
                if (generation != featureUiGeneration_) return;
                filterWheelReadPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    filterWheelStateKnown_ = false;
                    filterWheelMoving_ = false;
                    filterWheelStatusLabel_->setText("Unavailable");
                    featureStatus_->setText(
                        QString("Filter-wheel read failed: %1")
                            .arg(QString::fromStdString(error->message)));
                } else {
                    const auto& state =
                        std::get<amscope::FilterWheelState>(result);
                    filterWheelStateKnown_ = true;
                    filterWheelMoving_ = state.moving;
                    const QSignalBlocker blocker(filterWheelPositionSelector_);
                    filterWheelPositionSelector_->setRange(
                        0, static_cast<int>(state.slotCount) - 1);
                    if (state.position) {
                        filterWheelPositionSelector_->setValue(
                            static_cast<int>(*state.position));
                    }
                    if (state.moving) {
                        filterWheelStatusLabel_->setText("Moving…");
                    } else if (state.position) {
                        filterWheelPositionSelector_->setValue(
                            static_cast<int>(*state.position));
                        filterWheelStatusLabel_->setText(
                            QString("At slot %1")
                                .arg(static_cast<qulonglong>(*state.position)));
                    } else {
                        filterWheelStatusLabel_->setText("Position unknown");
                    }
                }
                updateControls(camera_.state());
            });
        if (requestId == 0) {
            filterWheelReadPending_ = false;
            filterWheelStateKnown_ = false;
            filterWheelMoving_ = false;
            filterWheelStatusLabel_->setText("Request rejected");
            updateControls(camera_.state());
        }
    }

    void refreshFilterWheelState() {
        refreshFilterWheelState(featureUiGeneration_);
    }

    void applyEmbeddedLightSourceLevel() {
        if (!embeddedLightSourceAvailable_ || accessoryOperationPending_)
            return;
        accessoryOperationPending_ = true;
        updateControls(camera_.state());
        const std::uint64_t generation = featureUiGeneration_;
        const auto level = static_cast<std::uint32_t>(
            embeddedLightSourceSlider_->value());
        const auto requestId = camera_.requestSetEmbeddedLightSourceLevel(
            level,
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AccessoryCommandResult result) {
                if (generation != featureUiGeneration_) return;
                accessoryOperationPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    featureStatus_->setText(
                        QString("Embedded light-source update failed: %1")
                            .arg(QString::fromStdString(error->message)));
                    updateControls(camera_.state());
                } else {
                    featureStatus_->setText("Embedded light-source level updated.");
                    refreshFeatureDescriptors();
                }
            });
        if (requestId == 0) {
            accessoryOperationPending_ = false;
            featureStatus_->setText("Embedded light-source command could not be queued.");
            updateControls(camera_.state());
        }
    }

    void submitFilterWheelMove() {
        if (!filterWheelAvailable_ || accessoryOperationPending_ ||
            filterWheelDirectionSelector_->currentIndex() < 0)
            return;
        accessoryOperationPending_ = true;
        updateControls(camera_.state());
        const std::uint64_t generation = featureUiGeneration_;
        const auto position = static_cast<std::uint32_t>(
            filterWheelPositionSelector_->value());
        const auto direction = static_cast<amscope::FilterWheelDirection>(
            filterWheelDirectionSelector_->currentData().toInt());
        const auto requestId = camera_.requestMoveFilterWheel(
            position, direction,
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AccessoryCommandResult result) {
                if (generation != featureUiGeneration_) return;
                accessoryOperationPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    featureStatus_->setText(
                        QString("Filter-wheel command failed: %1")
                            .arg(QString::fromStdString(error->message)));
                    updateControls(camera_.state());
                } else {
                    filterWheelStatusLabel_->setText("Move accepted; refreshing position…");
                    refreshFilterWheelState(generation);
                }
            });
        if (requestId == 0) {
            accessoryOperationPending_ = false;
            featureStatus_->setText("Filter-wheel command could not be queued.");
            updateControls(camera_.state());
        }
    }



    void refreshLensFocusState(std::uint64_t generation) {
        if (!lensFocusAvailable_ || lensFocusReadPending_)
            return;
        lensFocusReadPending_ = true;
        updateControls(camera_.state());

        const auto requestId = camera_.requestLensFocusState(
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::LensFocusStateResult result) {
                if (generation != featureUiGeneration_) return;
                lensFocusReadPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    lensFocusStateKnown_ = false;
                    lensFocusManual_ = false;
                    lensApertureCalibrated_ = false;
                    lensApertureChoicesAvailable_ = false;
                    lensApertureSelector_->clear();
                    lensFocusStatusLabel_->setText("Unavailable");
                    featureStatus_->setText(
                        QString("Lens focus-state read failed: %1")
                            .arg(QString::fromStdString(error->message)));
                } else {
                    const auto& state =
                        std::get<amscope::LensFocusState>(result);
                    lensFocusStateKnown_ = true;
                    lensFocusManual_ = state.manualFocusSelected;
                    lensApertureCalibrated_ =
                        state.apertureCalibrated.value_or(false);
                    lensApertureChoicesAvailable_ = !state.apertureLabels.empty();
                    {
                        const QSignalBlocker blocker(lensApertureSelector_);
                        lensApertureSelector_->clear();
                        for (std::size_t index = 0;
                             index < state.apertureLabels.size(); ++index) {
                            const auto& label = state.apertureLabels[index];
                            const QString display = label.empty()
                                ? QString("Index %1").arg(static_cast<qulonglong>(index))
                                : QString::fromStdString(label);
                            lensApertureSelector_->addItem(
                                display, static_cast<qulonglong>(index));
                        }
                        if (state.aperturePositionIndex <
                            static_cast<std::uint32_t>(lensApertureSelector_->count())) {
                            lensApertureSelector_->setCurrentIndex(
                                static_cast<int>(state.aperturePositionIndex));
                        }
                    }
                    const auto minimumPosition = std::min(
                        state.nearFocusMotorPosition, state.farFocusMotorPosition);
                    const auto maximumPosition = std::max(
                        state.nearFocusMotorPosition, state.farFocusMotorPosition);
                    {
                        const QSignalBlocker blocker(lensFocusTargetSelector_);
                        lensFocusTargetSelector_->setRange(
                            minimumPosition, maximumPosition);
                        lensFocusTargetSelector_->setValue(
                            state.currentFocusMotorPosition);
                    }

                    QString status =
                        QString("Lens %1 | %2 focus | motor %3 (range %4…%5)")
                            .arg(static_cast<qulonglong>(state.lensId))
                            .arg(state.manualFocusSelected ? "Manual" : "Auto")
                            .arg(state.currentFocusMotorPosition)
                            .arg(minimumPosition)
                            .arg(maximumPosition);
                    if (state.currentFocalLength != 0 ||
                        state.minimumFocalLength != 0 ||
                        state.maximumFocalLength != 0) {
                        status += QString(" | focal length %1 (range %2…%3)")
                            .arg(static_cast<qulonglong>(state.currentFocalLength))
                            .arg(static_cast<qulonglong>(state.minimumFocalLength))
                            .arg(static_cast<qulonglong>(state.maximumFocalLength));
                    }
                    if (state.autofocusMode) {
                        status += QString(" | AF mode %1 / status %2")
                            .arg(static_cast<qulonglong>(*state.autofocusMode))
                            .arg(static_cast<qulonglong>(
                                state.autofocusStatus.value_or(0)));
                    }
                    if (state.apertureCalibrated) {
                        status += *state.apertureCalibrated
                            ? " | aperture calibrated"
                            : " | aperture not calibrated";
                    }
                    lensFocusStatusLabel_->setText(status);
                }
                updateControls(camera_.state());
            });
        if (requestId == 0) {
            lensFocusReadPending_ = false;
            lensFocusStateKnown_ = false;
            lensFocusManual_ = false;
            lensApertureCalibrated_ = false;
            lensApertureChoicesAvailable_ = false;
            lensApertureSelector_->clear();
            lensFocusStatusLabel_->setText("Request rejected");
            updateControls(camera_.state());
        }
    }

    void refreshLensFocusState() {
        refreshLensFocusState(featureUiGeneration_);
    }

    void moveLensFocus() {
        if (!lensFocusAvailable_ || accessoryOperationPending_ ||
            lensFocusReadPending_ || !lensFocusStateKnown_ || !lensFocusManual_)
            return;
        accessoryOperationPending_ = true;
        updateControls(camera_.state());
        const auto generation = featureUiGeneration_;
        const auto position = static_cast<std::int32_t>(
            lensFocusTargetSelector_->value());
        const auto requestId = camera_.requestSetManualLensFocusPosition(
            position,
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AccessoryCommandResult result) {
                if (generation != featureUiGeneration_) return;
                accessoryOperationPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    featureStatus_->setText(
                        QString("Manual lens-focus move failed: %1")
                            .arg(QString::fromStdString(error->message)));
                    updateControls(camera_.state());
                } else {
                    lensFocusStateKnown_ = false;
                    lensFocusStatusLabel_->setText(
                        "Move command accepted; Refresh to query the motor position.");
                    updateControls(camera_.state());
                }
            });
        if (requestId == 0) {
            accessoryOperationPending_ = false;
            featureStatus_->setText("Manual lens-focus command could not be queued.");
            updateControls(camera_.state());
        }
    }



    void rebuildTriggerModeChoices() {
        const QSignalBlocker blocker(triggerModeSelector_);
        triggerModeSelector_->clear();
        triggerModeSelector_->addItem(
            "Video", static_cast<int>(amscope::AmcamTriggerMode::Video));

        if (triggerHardwareEventsAllowed_ &&
            (!triggerSoftwareSupported_ || triggerSoftwareAllowed_)) {
            triggerModeSelector_->addItem(
                "Software / simulated",
                static_cast<int>(amscope::AmcamTriggerMode::SoftwareOrSimulated));
        }
        if (triggerHardwareEventsAllowed_ && triggerExternalAllowed_) {
            triggerModeSelector_->addItem(
                "External", static_cast<int>(amscope::AmcamTriggerMode::External));
        }
        if (triggerHardwareEventsAllowed_ && triggerExternalAllowed_ &&
            triggerSoftwareAllowed_) {
            triggerModeSelector_->addItem(
                "External + software",
                static_cast<int>(amscope::AmcamTriggerMode::ExternalAndSoftware));
        }

        const QSignalBlocker sourceBlocker(triggerSourceSelector_);
        triggerSourceSelector_->clear();
        triggerSourceSelector_->addItem(
            "Opto-isolated input",
            static_cast<int>(amscope::AmcamTriggerSource::OptoIsolatedInput));
        triggerSourceSelector_->addItem(
            "GPIO 0", static_cast<int>(amscope::AmcamTriggerSource::GPIO0));
        triggerSourceSelector_->addItem(
            "GPIO 1", static_cast<int>(amscope::AmcamTriggerSource::GPIO1));
        triggerSourceSelector_->addItem(
            "Counter", static_cast<int>(amscope::AmcamTriggerSource::Counter));
        triggerSourceSelector_->addItem(
            "PWM", static_cast<int>(amscope::AmcamTriggerSource::PWM));
        if (triggerSoftwareAllowed_) {
            triggerSourceSelector_->addItem(
                "Software", static_cast<int>(amscope::AmcamTriggerSource::Software));
        }

        softwareTriggerCount_->setRange(1, triggerSingleImage_ ? 1 : 65534);
        updateTriggerModeWidgets();
    }

    bool selectedTriggerModeUsesExternalSource() const {
        if (triggerModeSelector_->currentIndex() < 0) return false;
        const auto mode = static_cast<amscope::AmcamTriggerMode>(
            triggerModeSelector_->currentData().toInt());
        return mode == amscope::AmcamTriggerMode::External ||
               mode == amscope::AmcamTriggerMode::ExternalAndSoftware;
    }

    void updateTriggerModeWidgets() {
        const bool externalMode = selectedTriggerModeUsesExternalSource();
        const bool videoMode = triggerModeSelector_->currentIndex() >= 0 &&
            triggerModeSelector_->currentData().toInt() ==
                static_cast<int>(amscope::AmcamTriggerMode::Video);
        triggerSourceRow_->setVisible(externalMode);
        if (QWidget* label = triggerGroup_->layout()
                ? qobject_cast<QFormLayout*>(triggerGroup_->layout())
                      ->labelForField(triggerSourceRow_)
                : nullptr) {
            label->setVisible(externalMode);
        }
        triggerHeartbeatRow_->setVisible(!videoMode);
        if (QWidget* label = triggerGroup_->layout()
                ? qobject_cast<QFormLayout*>(triggerGroup_->layout())
                      ->labelForField(triggerHeartbeatRow_)
                : nullptr) {
            label->setVisible(!videoMode);
        }
    }

    bool configurationAllowsSoftwareTrigger(
        const amscope::AmcamTriggerConfiguration& configuration) const {
        switch (configuration.mode) {
        case amscope::AmcamTriggerMode::SoftwareOrSimulated:
            return !triggerSoftwareSupported_ || triggerSoftwareAllowed_;
        case amscope::AmcamTriggerMode::ExternalAndSoftware:
            return triggerSoftwareAllowed_;
        case amscope::AmcamTriggerMode::External:
            return configuration.externalSource ==
                       amscope::AmcamTriggerSource::Software &&
                   triggerSoftwareSupported_ && triggerSoftwareAllowed_;
        case amscope::AmcamTriggerMode::Video:
            return false;
        }
        return false;
    }

    void refreshTriggerConfiguration(std::uint64_t generation) {
        if (!triggerPanelAvailable_ || triggerConfigurationReadPending_ ||
            triggerOperationPending_)
            return;
        triggerConfigurationReadPending_ = true;
        updateControls(camera_.state());

        const auto requestId = camera_.requestTriggerConfiguration(
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::TriggerConfigurationResult result) {
                if (generation != featureUiGeneration_) return;
                triggerConfigurationReadPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    triggerConfigurationKnown_ = false;
                    currentTriggerConfiguration_.reset();
                    triggerStatusLabel_->setText(
                        QString("Trigger configuration read failed: %1")
                            .arg(QString::fromStdString(error->message)));
                } else {
                    const auto& configuration =
                        std::get<amscope::AmcamTriggerConfiguration>(result);
                    currentTriggerConfiguration_ = configuration;
                    triggerConfigurationKnown_ = true;

                    int modeIndex = triggerModeSelector_->findData(
                        static_cast<int>(configuration.mode));
                    if (modeIndex < 0) {
                        triggerModeSelector_->addItem(
                            "Current mode (not enabled by this model/profile)",
                            static_cast<int>(configuration.mode));
                        modeIndex = triggerModeSelector_->count() - 1;
                    }
                    {
                        const QSignalBlocker blocker(triggerModeSelector_);
                        triggerModeSelector_->setCurrentIndex(modeIndex);
                    }

                    if (configuration.externalSource) {
                        int sourceIndex = triggerSourceSelector_->findData(
                            static_cast<int>(*configuration.externalSource));
                        if (sourceIndex < 0) {
                            triggerSourceSelector_->addItem(
                                "Current source (not enabled by this model/profile)",
                                static_cast<int>(*configuration.externalSource));
                            sourceIndex = triggerSourceSelector_->count() - 1;
                        }
                        const QSignalBlocker blocker(triggerSourceSelector_);
                        triggerSourceSelector_->setCurrentIndex(sourceIndex);
                    }
                    if (configuration.heartbeatIntervalMilliseconds >= 100 &&
                        configuration.heartbeatIntervalMilliseconds <= 10000) {
                        triggerHeartbeatInterval_->setValue(
                            static_cast<int>(configuration.heartbeatIntervalMilliseconds));
                    } else {
                        triggerHeartbeatInterval_->setValue(1000);
                    }

                    QString status;
                    switch (configuration.mode) {
                    case amscope::AmcamTriggerMode::Video:
                        status = "Video acquisition";
                        break;
                    case amscope::AmcamTriggerMode::SoftwareOrSimulated:
                        status = "Software / simulated trigger mode";
                        break;
                    case amscope::AmcamTriggerMode::External:
                        status = "External trigger mode";
                        break;
                    case amscope::AmcamTriggerMode::ExternalAndSoftware:
                        status = "External + software trigger mode";
                        break;
                    }
                    if (configuration.externalSource) {
                        status += QString(" | source %1")
                            .arg(triggerSourceSelector_->currentText());
                    }
                    if (configuration.mode != amscope::AmcamTriggerMode::Video) {
                        status += QString(" | heartbeat %1 ms")
                            .arg(configuration.heartbeatIntervalMilliseconds);
                    }
                    if (!triggerHardwareEventsAllowed_ &&
                        configuration.mode != amscope::AmcamTriggerMode::Video) {
                        status += " | hardware-event capability/profile unavailable";
                    }
                    triggerStatusLabel_->setText(status);
                    updateTriggerModeWidgets();
                }
                updateControls(camera_.state());
            });
        if (requestId == 0) {
            triggerConfigurationReadPending_ = false;
            triggerConfigurationKnown_ = false;
            triggerStatusLabel_->setText("Trigger configuration request was rejected.");
            updateControls(camera_.state());
        }
    }

    void refreshTriggerConfiguration() {
        refreshTriggerConfiguration(featureUiGeneration_);
    }

    void applyTriggerConfiguration() {
        if (!triggerPanelAvailable_ || triggerConfigurationReadPending_ ||
            triggerOperationPending_ ||
            camera_.state() != amscope::CameraState::Open ||
            triggerModeSelector_->currentIndex() < 0)
            return;

        amscope::AmcamTriggerConfiguration configuration;
        configuration.mode = static_cast<amscope::AmcamTriggerMode>(
            triggerModeSelector_->currentData().toInt());
        if (configuration.mode == amscope::AmcamTriggerMode::External ||
            configuration.mode == amscope::AmcamTriggerMode::ExternalAndSoftware) {
            if (triggerSourceSelector_->currentIndex() < 0) return;
            configuration.externalSource =
                static_cast<amscope::AmcamTriggerSource>(
                    triggerSourceSelector_->currentData().toInt());
        }
        configuration.heartbeatIntervalMilliseconds =
            static_cast<std::uint32_t>(triggerHeartbeatInterval_->value());

        triggerOperationPending_ = true;
        updateControls(camera_.state());
        const auto generation = featureUiGeneration_;
        const auto requestId = camera_.requestConfigureTrigger(
            configuration,
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AccessoryCommandResult result) {
                if (generation != featureUiGeneration_) return;
                triggerOperationPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    triggerStatusLabel_->setText(
                        QString("Trigger configuration failed: %1")
                            .arg(QString::fromStdString(error->message)));
                    updateControls(camera_.state());
                } else {
                    triggerConfigurationKnown_ = false;
                    currentTriggerConfiguration_.reset();
                    triggerStatusLabel_->setText(
                        "Configuration accepted; reading back the applied trigger mode…");
                    refreshTriggerConfiguration(generation);
                }
            });
        if (requestId == 0) {
            triggerOperationPending_ = false;
            triggerStatusLabel_->setText("Trigger configuration command could not be queued.");
            updateControls(camera_.state());
        }
    }

    void sendSoftwareTrigger() {
        if (!triggerConfigurationKnown_ || !currentTriggerConfiguration_ ||
            !configurationAllowsSoftwareTrigger(*currentTriggerConfiguration_) ||
            triggerOperationPending_ || triggerConfigurationReadPending_ ||
            camera_.state() != amscope::CameraState::Streaming)
            return;

        const auto count = static_cast<std::uint16_t>(softwareTriggerCount_->value());
        triggerOperationPending_ = true;
        updateControls(camera_.state());
        const auto generation = featureUiGeneration_;
        const auto requestId = camera_.requestSoftwareTrigger(
            count,
            [this, generation, count](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AccessoryCommandResult result) {
                if (generation != featureUiGeneration_) return;
                triggerOperationPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    triggerStatusLabel_->setText(
                        QString("Software trigger failed: %1")
                            .arg(QString::fromStdString(error->message)));
                } else {
                    triggerStatusLabel_->setText(
                        QString("Software trigger command accepted for %1 image(s).")
                            .arg(count));
                }
                updateControls(camera_.state());
            });
        if (requestId == 0) {
            triggerOperationPending_ = false;
            triggerStatusLabel_->setText("Software trigger command could not be queued.");
            updateControls(camera_.state());
        }
    }

    void setLensAperturePosition() {
        if (!lensFocusAvailable_ || accessoryOperationPending_ ||
            lensFocusReadPending_ || !lensFocusStateKnown_ ||
            !lensApertureChoicesAvailable_ || !lensApertureCalibrated_ ||
            lensApertureSelector_->currentIndex() < 0)
            return;
        accessoryOperationPending_ = true;
        updateControls(camera_.state());
        const auto generation = featureUiGeneration_;
        const auto positionIndex = static_cast<std::uint32_t>(
            lensApertureSelector_->currentData().toUInt());
        const auto requestId = camera_.requestSetLensAperturePosition(
            positionIndex,
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AccessoryCommandResult result) {
                if (generation != featureUiGeneration_) return;
                accessoryOperationPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    featureStatus_->setText(
                        QString("Lens aperture command failed: %1")
                            .arg(QString::fromStdString(error->message)));
                    updateControls(camera_.state());
                } else {
                    lensFocusStateKnown_ = false;
                    lensFocusStatusLabel_->setText(
                        "Aperture command accepted; Refresh to query the current aperture position.");
                    updateControls(camera_.state());
                }
            });
        if (requestId == 0) {
            accessoryOperationPending_ = false;
            featureStatus_->setText("Lens aperture command could not be queued.");
            updateControls(camera_.state());
        }
    }

    void refreshAstroAutoFocuserState(std::uint64_t generation) {
        if (!astroAutoFocuserAvailable_ || astroAutoFocuserReadPending_)
            return;
        astroAutoFocuserReadPending_ = true;
        updateControls(camera_.state());

        const auto requestId = camera_.requestAstroAutoFocuserState(
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AstroAutoFocuserResult result) {
                if (generation != featureUiGeneration_) return;
                astroAutoFocuserReadPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    astroAutoFocuserStateKnown_ = false;
                    astroAutoFocuserMoving_ = false;
                    astroAutoFocuserStatusLabel_->setText("Unavailable");
                    featureStatus_->setText(
                        QString("Astro auto-focuser read failed: %1")
                            .arg(QString::fromStdString(error->message)));
                } else {
                    const auto& state =
                        std::get<amscope::AstroAutoFocuserState>(result);
                    astroAutoFocuserStateKnown_ = true;
                    astroAutoFocuserMoving_ = state.moving;
                    {
                        const QSignalBlocker blocker(astroAutoFocuserTargetSelector_);
                        astroAutoFocuserTargetSelector_->setRange(
                            state.minimumPosition, state.maximumPosition);
                        astroAutoFocuserTargetSelector_->setSingleStep(
                            std::max(1, state.stepSize));
                        astroAutoFocuserTargetSelector_->setValue(state.position);
                    }

                    QString status = state.moving ? "Moving" : "Idle";
                    status += QString(" | position %1 | range %2…%3 | step %4")
                        .arg(state.position)
                        .arg(state.minimumPosition)
                        .arg(state.maximumPosition)
                        .arg(state.stepSize);
                    if (state.temperatureTenthsC) {
                        status += QString(" | focuser %1 °C")
                            .arg(*state.temperatureTenthsC / 10.0, 0, 'f', 1);
                    }
                    if (state.ambientTemperatureSdkUnits) {
                        status += QString(" | ambient SDK value %1")
                            .arg(*state.ambientTemperatureSdkUnits);
                    }
                    astroAutoFocuserStatusLabel_->setText(status);
                }
                updateControls(camera_.state());
            });
        if (requestId == 0) {
            astroAutoFocuserReadPending_ = false;
            astroAutoFocuserStateKnown_ = false;
            astroAutoFocuserMoving_ = false;
            astroAutoFocuserStatusLabel_->setText("Request rejected");
            updateControls(camera_.state());
        }
    }

    void refreshAstroAutoFocuserState() {
        refreshAstroAutoFocuserState(featureUiGeneration_);
    }

    void moveAstroAutoFocuser() {
        if (!astroAutoFocuserAvailable_ || accessoryOperationPending_ ||
            astroAutoFocuserReadPending_ || !astroAutoFocuserStateKnown_ ||
            astroAutoFocuserMoving_)
            return;
        accessoryOperationPending_ = true;
        updateControls(camera_.state());
        const auto generation = featureUiGeneration_;
        const auto target = static_cast<std::int32_t>(
            astroAutoFocuserTargetSelector_->value());
        const auto requestId = camera_.requestMoveAstroAutoFocuser(
            target,
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AccessoryCommandResult result) {
                if (generation != featureUiGeneration_) return;
                accessoryOperationPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    featureStatus_->setText(
                        QString("Astro focuser move failed: %1")
                            .arg(QString::fromStdString(error->message)));
                    updateControls(camera_.state());
                } else {
                    astroAutoFocuserStatusLabel_->setText(
                        "Move accepted; refreshing focuser state…");
                    refreshAstroAutoFocuserState(generation);
                }
            });
        if (requestId == 0) {
            accessoryOperationPending_ = false;
            featureStatus_->setText("Astro focuser move could not be queued.");
            updateControls(camera_.state());
        }
    }

    void haltAstroAutoFocuser() {
        if (!astroAutoFocuserAvailable_ || accessoryOperationPending_ ||
            astroAutoFocuserReadPending_ || !astroAutoFocuserStateKnown_ ||
            !astroAutoFocuserMoving_)
            return;
        accessoryOperationPending_ = true;
        updateControls(camera_.state());
        const auto generation = featureUiGeneration_;
        const auto requestId = camera_.requestHaltAstroAutoFocuser(
            [this, generation](
                amscope::QtAmcamCamera::FeatureRequestId,
                amscope::QtAmcamCamera::AccessoryCommandResult result) {
                if (generation != featureUiGeneration_) return;
                accessoryOperationPending_ = false;
                if (const auto* error = std::get_if<amscope::CameraError>(&result)) {
                    featureStatus_->setText(
                        QString("Astro focuser halt failed: %1")
                            .arg(QString::fromStdString(error->message)));
                    updateControls(camera_.state());
                } else {
                    astroAutoFocuserStatusLabel_->setText(
                        "Halt accepted; refreshing focuser state…");
                    refreshAstroAutoFocuserState(generation);
                }
            });
        if (requestId == 0) {
            accessoryOperationPending_ = false;
            featureStatus_->setText("Astro focuser halt could not be queued.");
            updateControls(camera_.state());
        }
    }

    void updateControls(amscope::CameraState state) {
        const bool closed = state == amscope::CameraState::Closed;
        const bool open = state == amscope::CameraState::Open;
        const bool streaming = state == amscope::CameraState::Streaming;

        const bool idle =
            !lifecycleRequestPending_ && !cameraEnumerationPending_ &&
            !stillCapturePending_ && !stillResolutionsPending_;
        cameraSelector_->setEnabled(closed && idle);
        refreshCamerasButton_->setEnabled(closed && idle);
        openButton_->setEnabled(closed && idle && cameraSelector_->count() > 0);
        startButton_->setEnabled(open && idle);
        stopButton_->setEnabled(streaming && idle);
        closeButton_->setEnabled(!closed && idle);
        stillResolutionSelector_->setEnabled(
            streaming && idle && stillResolutionSelector_->count() > 0);
        stillCaptureModeSelector_->setEnabled(streaming && idle);
        captureStillButton_->setEnabled(
            streaming && idle && stillResolutionSelector_->count() > 0);
        refreshFeaturesButton_->setEnabled(open && idle &&
                                            !accessoryOperationPending_);
        const bool accessoriesIdle =
            idle && !accessoryOperationPending_ &&
            !embeddedLightSourceReadPending_ && !filterWheelReadPending_ &&
            !astroAutoFocuserReadPending_ && !lensFocusReadPending_ &&
            !triggerConfigurationReadPending_ && !triggerOperationPending_;
        const bool accessoryControlsIdle = (open || streaming) && accessoriesIdle;
        accessoryGroup_->setEnabled(accessoryControlsIdle);
        astroAutoFocuserGroup_->setEnabled((open || streaming) && accessoriesIdle);
        astroAutoFocuserTargetSelector_->setEnabled(
            accessoryControlsIdle && astroAutoFocuserAvailable_ &&
            astroAutoFocuserStateKnown_ && !astroAutoFocuserMoving_);
        astroAutoFocuserMoveButton_->setEnabled(
            accessoryControlsIdle && astroAutoFocuserAvailable_ &&
            astroAutoFocuserStateKnown_ && !astroAutoFocuserMoving_);
        astroAutoFocuserHaltButton_->setEnabled(
            accessoryControlsIdle && astroAutoFocuserAvailable_ &&
            astroAutoFocuserStateKnown_ && astroAutoFocuserMoving_);
        astroAutoFocuserRefreshButton_->setEnabled(
            accessoryControlsIdle && astroAutoFocuserAvailable_);
        lensFocusGroup_->setEnabled((open || streaming) && accessoriesIdle);
        lensFocusTargetSelector_->setEnabled(
            accessoryControlsIdle && lensFocusAvailable_ &&
            lensFocusStateKnown_ && lensFocusManual_);
        lensFocusMoveButton_->setEnabled(
            accessoryControlsIdle && lensFocusAvailable_ &&
            lensFocusStateKnown_ && lensFocusManual_);
        lensFocusRefreshButton_->setEnabled(
            accessoryControlsIdle && lensFocusAvailable_);
        lensApertureSelector_->setEnabled(
            accessoryControlsIdle && lensFocusAvailable_ &&
            lensFocusStateKnown_ && lensApertureCalibrated_ &&
            lensApertureChoicesAvailable_);
        lensApertureSetButton_->setEnabled(
            accessoryControlsIdle && lensFocusAvailable_ &&
            lensFocusStateKnown_ && lensApertureCalibrated_ &&
            lensApertureChoicesAvailable_ &&
            lensApertureSelector_->currentIndex() >= 0);
        embeddedLightSourceSlider_->setEnabled(
            accessoryControlsIdle && embeddedLightSourceAvailable_ &&
            embeddedLightSourceStateKnown_);
        embeddedLightSourceApplyButton_->setEnabled(
            accessoryControlsIdle && embeddedLightSourceAvailable_ &&
            embeddedLightSourceStateKnown_);
        filterWheelPositionSelector_->setEnabled(
            accessoryControlsIdle && filterWheelAvailable_ &&
            filterWheelStateKnown_ && !filterWheelMoving_);
        filterWheelDirectionSelector_->setEnabled(
            accessoryControlsIdle && filterWheelAvailable_ &&
            filterWheelStateKnown_ && !filterWheelMoving_);
        filterWheelMoveButton_->setEnabled(
            accessoryControlsIdle && filterWheelAvailable_ &&
            filterWheelStateKnown_ && !filterWheelMoving_);
        filterWheelRefreshButton_->setEnabled(
            accessoryControlsIdle && filterWheelAvailable_);

        triggerGroup_->setEnabled(
            (open || streaming) && !triggerConfigurationReadPending_ &&
            !triggerOperationPending_);
        triggerModeSelector_->setEnabled(open && triggerPanelAvailable_ &&
            !triggerConfigurationReadPending_ && !triggerOperationPending_);
        triggerSourceSelector_->setEnabled(open && triggerPanelAvailable_ &&
            selectedTriggerModeUsesExternalSource() &&
            !triggerConfigurationReadPending_ && !triggerOperationPending_);
        triggerHeartbeatInterval_->setEnabled(open && triggerPanelAvailable_ &&
            triggerModeSelector_->currentIndex() >= 0 &&
            triggerModeSelector_->currentData().toInt() !=
                static_cast<int>(amscope::AmcamTriggerMode::Video) &&
            !triggerConfigurationReadPending_ && !triggerOperationPending_);
        triggerApplyButton_->setEnabled(open && triggerPanelAvailable_ &&
            triggerModeSelector_->currentIndex() >= 0 &&
            (!selectedTriggerModeUsesExternalSource() ||
             triggerSourceSelector_->currentIndex() >= 0) &&
            !triggerConfigurationReadPending_ && !triggerOperationPending_);
        triggerRefreshButton_->setEnabled(
            (open || streaming) && triggerPanelAvailable_ &&
            !triggerConfigurationReadPending_ && !triggerOperationPending_);
        softwareTriggerCount_->setEnabled(streaming &&
            triggerConfigurationKnown_ && currentTriggerConfiguration_.has_value() &&
            configurationAllowsSoftwareTrigger(*currentTriggerConfiguration_) &&
            !triggerOperationPending_ && !triggerConfigurationReadPending_);
        softwareTriggerButton_->setEnabled(streaming &&
            triggerConfigurationKnown_ && currentTriggerConfiguration_.has_value() &&
            configurationAllowsSoftwareTrigger(*currentTriggerConfiguration_) &&
            !triggerOperationPending_ && !triggerConfigurationReadPending_);
    }

    amscope::QtAmcamCamera camera_;
    std::vector<amscope::CameraInfo> cameras_;
    camera::FrameRateEstimator previewFrameRateEstimator_;
    std::optional<double> previewFramesPerSecond_;
    std::optional<double> imageEventsPerSecond_;
    std::optional<double> pulledFramesPerSecond_;
    std::optional<std::chrono::steady_clock::time_point> lastPullRateAt_;
    std::uint64_t lastImageEventCount_ = 0;
    std::uint64_t lastPulledFrameCount_ = 0;
    std::optional<std::uint64_t> lastExposureMicroseconds_;
    std::uint32_t lastFrameWidth_ = 0;
    std::uint32_t lastFrameHeight_ = 0;
    std::optional<std::uint64_t> lastFrameSequence_;
    std::size_t lastFrameBytes_ = 0;
    bool hasReceivedFrame_ = false;
    QTimer* frameRateTimer_ = nullptr;

    std::uint64_t featureUiGeneration_ = 0;
    std::uint64_t lifecycleUiGeneration_ = 0;
    bool lifecycleRequestPending_ = false;
    bool stillCapturePending_ = false;
    bool stillResolutionsPending_ = false;
    std::uint64_t stillResolutionUiGeneration_ = 0;
    std::uint64_t cameraEnumerationGeneration_ = 0;
    bool cameraEnumerationPending_ = false;
    std::size_t pendingFeatureReads_ = 0;
    bool pendingFeatureWrite_ = false;
    bool embeddedLightSourceAvailable_ = false;
    bool filterWheelAvailable_ = false;
    bool embeddedLightSourceReadPending_ = false;
    bool filterWheelReadPending_ = false;
    bool embeddedLightSourceStateKnown_ = false;
    bool filterWheelStateKnown_ = false;
    bool filterWheelMoving_ = false;
    bool astroAutoFocuserAvailable_ = false;
    bool astroAutoFocuserReadPending_ = false;
    bool astroAutoFocuserStateKnown_ = false;
    bool astroAutoFocuserMoving_ = false;
    bool lensFocusAvailable_ = false;
    bool lensFocusReadPending_ = false;
    bool lensFocusStateKnown_ = false;
    bool lensFocusManual_ = false;
    bool lensApertureCalibrated_ = false;
    bool lensApertureChoicesAvailable_ = false;
    bool triggerPanelAvailable_ = false;
    bool triggerExternalSupported_ = false;
    bool triggerExternalAllowed_ = false;
    bool triggerSoftwareSupported_ = false;
    bool triggerSoftwareAllowed_ = false;
    bool triggerHardwareEventsAllowed_ = false;
    bool triggerSingleImage_ = false;
    bool triggerConfigurationReadPending_ = false;
    bool triggerConfigurationKnown_ = false;
    bool triggerOperationPending_ = false;
    std::optional<amscope::AmcamTriggerConfiguration> currentTriggerConfiguration_;
    bool accessoryOperationPending_ = false;
    std::unordered_map<std::string, FeatureControl> featureControls_;

    QComboBox* cameraSelector_ = nullptr;
    QPushButton* refreshCamerasButton_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
    QComboBox* stillResolutionSelector_ = nullptr;
    QComboBox* stillCaptureModeSelector_ = nullptr;
    QPushButton* captureStillButton_ = nullptr;
    QPushButton* refreshFeaturesButton_ = nullptr;
    FrameView* preview_ = nullptr;
    QLabel* frameInfo_ = nullptr;
    QLabel* featureStatus_ = nullptr;
    QGroupBox* accessoryGroup_ = nullptr;
    QFormLayout* accessoryForm_ = nullptr;
    QWidget* embeddedLightSourceRow_ = nullptr;
    QSlider* embeddedLightSourceSlider_ = nullptr;
    QLabel* embeddedLightSourceValueLabel_ = nullptr;
    QPushButton* embeddedLightSourceApplyButton_ = nullptr;
    QWidget* filterWheelRow_ = nullptr;
    QSpinBox* filterWheelPositionSelector_ = nullptr;
    QComboBox* filterWheelDirectionSelector_ = nullptr;
    QPushButton* filterWheelMoveButton_ = nullptr;
    QPushButton* filterWheelRefreshButton_ = nullptr;
    QLabel* filterWheelStatusLabel_ = nullptr;
    QGroupBox* astroAutoFocuserGroup_ = nullptr;
    QWidget* astroAutoFocuserRow_ = nullptr;
    QSpinBox* astroAutoFocuserTargetSelector_ = nullptr;
    QPushButton* astroAutoFocuserMoveButton_ = nullptr;
    QPushButton* astroAutoFocuserHaltButton_ = nullptr;
    QPushButton* astroAutoFocuserRefreshButton_ = nullptr;
    QLabel* astroAutoFocuserStatusLabel_ = nullptr;
    QGroupBox* lensFocusGroup_ = nullptr;
    QWidget* lensFocusRow_ = nullptr;
    QSpinBox* lensFocusTargetSelector_ = nullptr;
    QPushButton* lensFocusMoveButton_ = nullptr;
    QPushButton* lensFocusRefreshButton_ = nullptr;
    QWidget* lensApertureRow_ = nullptr;
    QComboBox* lensApertureSelector_ = nullptr;
    QPushButton* lensApertureSetButton_ = nullptr;
    QLabel* lensFocusStatusLabel_ = nullptr;
    QGroupBox* triggerGroup_ = nullptr;
    QComboBox* triggerModeSelector_ = nullptr;
    QWidget* triggerSourceRow_ = nullptr;
    QComboBox* triggerSourceSelector_ = nullptr;
    QWidget* triggerHeartbeatRow_ = nullptr;
    QSpinBox* triggerHeartbeatInterval_ = nullptr;
    QPushButton* triggerApplyButton_ = nullptr;
    QPushButton* triggerRefreshButton_ = nullptr;
    QSpinBox* softwareTriggerCount_ = nullptr;
    QPushButton* softwareTriggerButton_ = nullptr;
    QLabel* triggerStatusLabel_ = nullptr;
    QScrollArea* featureScroll_ = nullptr;
    QWidget* featureContent_ = nullptr;
    QFormLayout* featureForm_ = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--list-cameras") {
        return listCameras();
    }

    QApplication application(argc, argv);
    PreviewWindow window;
    window.show();
    return application.exec();
}
