#include "amscope/QtAmcamCamera.h"
#include "camera/FrameRateEstimator.h"

#include <QByteArray>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
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

        toolbar->addWidget(cameraSelector_, 1);
        toolbar->addWidget(refreshCamerasButton_);
        toolbar->addWidget(openButton_);
        toolbar->addWidget(startButton_);
        toolbar->addWidget(stopButton_);
        toolbar->addWidget(closeButton_);

        preview_ = new QLabel("Select a camera and press Open, then Start.", central);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setMinimumSize(640, 480);
        preview_->setStyleSheet("QLabel { background: #17191d; color: #d6d9df; }");

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

        auto* settingsColumn = new QVBoxLayout();
        settingsColumn->addWidget(featureStatus_);
        settingsColumn->addWidget(refreshFeaturesButton_);
        settingsColumn->addWidget(featureScroll_, 1);

        auto* workspace = new QHBoxLayout();
        workspace->addWidget(preview_, 1);
        workspace->addLayout(settingsColumn);

        frameInfo_ = new QLabel("No frames received", central);
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
        connect(refreshFeaturesButton_, &QPushButton::clicked, this, [this]() {
            refreshFeatureDescriptors();
        });

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
            if (state != amscope::CameraState::Streaming)
                resetPreviewFrameRate();
            updateControls(state);
            if (state == amscope::CameraState::Open ||
                state == amscope::CameraState::Streaming) {
                refreshFeatureDescriptors();
            } else {
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
                    preview_->setText("Camera opened. Press Start to begin acquisition.");
                    statusBar()->showMessage("Camera session opened.");
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
                } else {
                    statusBar()->showMessage("Acquisition started.");
                }
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
                } else {
                    statusBar()->showMessage("Acquisition stopped.");
                }
                updateControls(camera_.state());
            });

        if (requestId == 0) {
            lifecycleRequestPending_ = false;
            statusBar()->showMessage("The acquisition stop request could not be queued.");
            setFeatureControlsEnabled(true);
            updateControls(camera_.state());
        }
    }

    void closeCamera() {
        if (lifecycleRequestPending_) return;
        ++featureUiGeneration_;
        pendingFeatureWrite_ = false;
        const std::uint64_t generation = ++lifecycleUiGeneration_;
        lifecycleRequestPending_ = true;
        setFeatureControlsEnabled(false);
        clearFeatureControls();
        featureStatus_->setText("Closing camera session…");
        preview_->setText("Closing camera session…");
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
                    preview_->setText("Select a camera and press Open, then Start.");
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
        case camera::CameraFeatureKind::Integer:
        case camera::CameraFeatureKind::IntegerVector:
        case camera::CameraFeatureKind::String: {
            auto* control = new QLineEdit(featureContent_);
            if (descriptor.kind == camera::CameraFeatureKind::Integer) {
                control->setPlaceholderText(numericRangeText(descriptor));
            } else if (descriptor.kind == camera::CameraFeatureKind::IntegerVector) {
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
            auto* editor = qobject_cast<QLineEdit*>(control.editor);
            if (!typed || !editor) return false;
            const QSignalBlocker blocker(editor);
            editor->setText(QString::number(static_cast<qlonglong>(*typed)));
            return true;
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
            const auto* editor = qobject_cast<QLineEdit*>(control.editor);
            bool ok = false;
            const qlonglong number = editor ? editor->text().trimmed().toLongLong(&ok) : 0;
            if (!ok) {
                validationMessage = "Enter a valid signed integer.";
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
        const bool canEnable = enabled && !lifecycleRequestPending_;
        for (auto& entry : featureControls_) {
            const auto& descriptor = entry.second.descriptor;
            const bool valueIsKnown =
                !descriptor.isReadable || entry.second.hasCurrentValue;
            entry.second.editor->setEnabled(
                canEnable && descriptor.isWritable && valueIsKnown);
        }
        refreshFeaturesButton_->setEnabled(
            canEnable &&
            (camera_.state() == amscope::CameraState::Open ||
             camera_.state() == amscope::CameraState::Streaming));
    }

    void presentLatestFrame() {
        const amscope::FramePtr frame = camera_.takeLatestFrame();
        if (!frame || frame->data() == nullptr ||
            frame->width == 0 || frame->height == 0) {
            return;
        }

        const QImage borrowed(
            frame->data(),
            static_cast<int>(frame->width),
            static_cast<int>(frame->height),
            static_cast<int>(frame->strideBytes),
            QImage::Format_RGB888);

        // Detach before releasing the immutable frame lease so the widget
        // never retains pool memory after the callback returns.
        const QPixmap pixmap = QPixmap::fromImage(borrowed.copy());
        preview_->setPixmap(pixmap.scaled(
            preview_->size(),
            Qt::KeepAspectRatio,
            Qt::FastTransformation));

        const double monotonicSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (const auto estimate = previewFrameRateEstimator_.record(monotonicSeconds))
            previewFramesPerSecond_ = *estimate;

        QString frameInfo = QString("%1 x %2 | frame %3 | %4 bytes")
            .arg(static_cast<qulonglong>(frame->width))
            .arg(static_cast<qulonglong>(frame->height))
            .arg(static_cast<qulonglong>(frame->sequence))
            .arg(static_cast<qulonglong>(frame->byteSize()));
        if (previewFramesPerSecond_) {
            frameInfo += QString(" | preview %1 FPS")
                .arg(*previewFramesPerSecond_, 0, 'f', 1);
        }
        frameInfo_->setText(frameInfo);
    }

    void resetPreviewFrameRate() {
        previewFrameRateEstimator_.reset();
        previewFramesPerSecond_.reset();
    }

    void updateControls(amscope::CameraState state) {
        const bool closed = state == amscope::CameraState::Closed;
        const bool open = state == amscope::CameraState::Open;
        const bool streaming = state == amscope::CameraState::Streaming;

        const bool idle =
            !lifecycleRequestPending_ && !cameraEnumerationPending_;
        cameraSelector_->setEnabled(closed && idle);
        refreshCamerasButton_->setEnabled(closed && idle);
        openButton_->setEnabled(closed && idle && cameraSelector_->count() > 0);
        startButton_->setEnabled(open && idle);
        stopButton_->setEnabled(streaming && idle);
        closeButton_->setEnabled(!closed && idle);
        refreshFeaturesButton_->setEnabled((open || streaming) && idle);
    }

    amscope::QtAmcamCamera camera_;
    std::vector<amscope::CameraInfo> cameras_;
    camera::FrameRateEstimator previewFrameRateEstimator_;
    std::optional<double> previewFramesPerSecond_;

    std::uint64_t featureUiGeneration_ = 0;
    std::uint64_t lifecycleUiGeneration_ = 0;
    bool lifecycleRequestPending_ = false;
    std::uint64_t cameraEnumerationGeneration_ = 0;
    bool cameraEnumerationPending_ = false;
    std::size_t pendingFeatureReads_ = 0;
    bool pendingFeatureWrite_ = false;
    std::unordered_map<std::string, FeatureControl> featureControls_;

    QComboBox* cameraSelector_ = nullptr;
    QPushButton* refreshCamerasButton_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
    QPushButton* refreshFeaturesButton_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* frameInfo_ = nullptr;
    QLabel* featureStatus_ = nullptr;
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
