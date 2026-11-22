#include "amscope/AmcamCamera.h"
#include "amscope/QtAmcamCamera.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int listCameras() {
    amscope::CameraError error;
    const auto cameras = amscope::AmcamCamera::enumerate(&error);
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
        resize(1100, 760);

        auto* central = new QWidget(this);
        auto* root = new QVBoxLayout(central);
        auto* toolbar = new QHBoxLayout();

        cameraSelector_ = new QComboBox(central);
        cameraSelector_->setMinimumWidth(320);

        auto* refreshButton = new QPushButton("Refresh", central);
        openButton_ = new QPushButton("Open", central);
        startButton_ = new QPushButton("Start", central);
        stopButton_ = new QPushButton("Stop", central);
        closeButton_ = new QPushButton("Close", central);

        toolbar->addWidget(cameraSelector_, 1);
        toolbar->addWidget(refreshButton);
        toolbar->addWidget(openButton_);
        toolbar->addWidget(startButton_);
        toolbar->addWidget(stopButton_);
        toolbar->addWidget(closeButton_);

        preview_ = new QLabel("Select a camera and press Open, then Start.", central);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setMinimumSize(640, 480);
        preview_->setStyleSheet("QLabel { background: #17191d; color: #d6d9df; }");

        frameInfo_ = new QLabel("No frames received", central);
        root->addLayout(toolbar);
        root->addWidget(preview_, 1);
        root->addWidget(frameInfo_);
        setCentralWidget(central);

        connect(refreshButton, &QPushButton::clicked, this, [this]() {
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

        connect(&camera_, &amscope::QtAmcamCamera::frameAvailable,
                this, [this]() {
            presentLatestFrame();
        });
        connect(&camera_, &amscope::QtAmcamCamera::stateChanged,
                this, [this](amscope::CameraState state) {
            updateControls(state);
        });
        connect(&camera_, &amscope::QtAmcamCamera::errorOccurred,
                this, [this](const QString& message, qint32) {
            statusBar()->showMessage(message);
        });

        refreshCameras();
        updateControls(camera_.state());
    }

    ~PreviewWindow() override {
        amscope::CameraError ignored;
        camera_.close(&ignored);
    }

private:
    void refreshCameras() {
        if (camera_.state() != amscope::CameraState::Closed) {
            statusBar()->showMessage(
                "Close the current camera session before refreshing device enumeration.");
            return;
        }

        amscope::CameraError error;
        cameras_ = amscope::QtAmcamCamera::enumerate(&error);
        cameraSelector_->clear();

        if (!error.message.empty()) {
            statusBar()->showMessage(QString::fromStdString(error.message));
            return;
        }

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
                QString("%1 camera(s) detected.").arg(cameras_.size()));
        }
    }

    void openCamera() {
        const QString id = cameraSelector_->currentData().toString();
        if (id.isEmpty()) {
            statusBar()->showMessage("Select a camera first.");
            return;
        }

        amscope::CameraError error;
        if (!camera_.open(id, &error)) {
            statusBar()->showMessage(QString::fromStdString(error.message));
            return;
        }

        preview_->setText("Camera opened. Press Start to begin acquisition.");
        statusBar()->showMessage("Camera session opened.");
        updateControls(camera_.state());
    }

    void startCamera() {
        amscope::CameraError error;
        if (!camera_.start(&error)) {
            statusBar()->showMessage(QString::fromStdString(error.message));
            return;
        }
        statusBar()->showMessage("Acquisition started.");
        updateControls(camera_.state());
    }

    void stopCamera() {
        amscope::CameraError error;
        if (!camera_.stop(&error)) {
            statusBar()->showMessage(QString::fromStdString(error.message));
        } else {
            statusBar()->showMessage("Acquisition stopped.");
        }
        updateControls(camera_.state());
    }

    void closeCamera() {
        amscope::CameraError error;
        if (!camera_.close(&error)) {
            statusBar()->showMessage(QString::fromStdString(error.message));
        } else {
            preview_->setText("Select a camera and press Open, then Start.");
            frameInfo_->setText("No frames received");
            statusBar()->showMessage("Camera session closed.");
        }
        updateControls(camera_.state());
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

        frameInfo_->setText(
            QString("%1 x %2 | frame %3 | %4 bytes")
                .arg(frame->width)
                .arg(frame->height)
                .arg(frame->sequence)
                .arg(frame->byteSize()));
    }

    void updateControls(amscope::CameraState state) {
        const bool closed = state == amscope::CameraState::Closed;
        const bool open = state == amscope::CameraState::Open;
        const bool streaming = state == amscope::CameraState::Streaming;

        cameraSelector_->setEnabled(closed);
        openButton_->setEnabled(closed && cameraSelector_->count() > 0);
        startButton_->setEnabled(open);
        stopButton_->setEnabled(streaming);
        closeButton_->setEnabled(!closed);
    }

    amscope::QtAmcamCamera camera_;
    std::vector<amscope::CameraInfo> cameras_;
    QComboBox* cameraSelector_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* frameInfo_ = nullptr;
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
