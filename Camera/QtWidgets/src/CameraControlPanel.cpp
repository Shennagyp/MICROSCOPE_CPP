#include "camera/qtwidgets/CameraControlPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>

#include <utility>

namespace camera::qtwidgets {

namespace {

QString deviceLabel(const CameraDeviceInfo& device) {
    if (!device.displayName.empty())
        return QString::fromStdString(device.displayName);
    if (device.identity.model && !device.identity.model->empty())
        return QString::fromStdString(*device.identity.model);
    if (device.identity.manufacturer && !device.identity.manufacturer->empty())
        return QString::fromStdString(*device.identity.manufacturer);
    return QStringLiteral("Camera");
}

} // namespace

CameraControlPanel::CameraControlPanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    deviceSelector_ = new QComboBox(this);
    deviceSelector_->setObjectName(QStringLiteral("cameraDeviceSelector"));
    deviceSelector_->setMinimumWidth(320);

    refreshButton_ = new QPushButton(QStringLiteral("Refresh cameras"), this);
    refreshButton_->setObjectName(QStringLiteral("refreshCamerasButton"));

    openButton_ = new QPushButton(QStringLiteral("Open"), this);
    openButton_->setObjectName(QStringLiteral("openCameraButton"));

    startButton_ = new QPushButton(QStringLiteral("Start"), this);
    startButton_->setObjectName(QStringLiteral("startCameraButton"));

    stopButton_ = new QPushButton(QStringLiteral("Stop"), this);
    stopButton_->setObjectName(QStringLiteral("stopCameraButton"));

    closeButton_ = new QPushButton(QStringLiteral("Close"), this);
    closeButton_->setObjectName(QStringLiteral("closeCameraButton"));

    layout->addWidget(deviceSelector_, 1);
    layout->addWidget(refreshButton_);
    layout->addWidget(openButton_);
    layout->addWidget(startButton_);
    layout->addWidget(stopButton_);
    layout->addWidget(closeButton_);

    connect(refreshButton_, &QPushButton::clicked, this, [this]() {
        if (refreshRequestedHandler_)
            refreshRequestedHandler_();
    });
    connect(openButton_, &QPushButton::clicked, this, [this]() {
        const auto deviceId = selectedDeviceId();
        if (deviceId && openRequestedHandler_)
            openRequestedHandler_(*deviceId);
    });
    connect(startButton_, &QPushButton::clicked, this, [this]() {
        if (startRequestedHandler_)
            startRequestedHandler_();
    });
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        if (stopRequestedHandler_)
            stopRequestedHandler_();
    });
    connect(closeButton_, &QPushButton::clicked, this, [this]() {
        if (closeRequestedHandler_)
            closeRequestedHandler_();
    });

    updateControls();
}

void CameraControlPanel::setDevices(
    std::vector<CameraDeviceInfo> devices) {
    deviceSelector_->clear();
    for (const auto& device : devices) {
        deviceSelector_->addItem(
            deviceLabel(device),
            QString::fromStdString(device.id));
    }
    updateControls();
}

std::optional<std::string> CameraControlPanel::selectedDeviceId() const {
    if (deviceSelector_->currentIndex() < 0)
        return std::nullopt;

    const std::string id = deviceSelector_->currentData().toString().toStdString();
    if (id.empty())
        return std::nullopt;
    return id;
}

void CameraControlPanel::setState(CameraControlPanelState state) {
    state_ = state;
    updateControls();
}

void CameraControlPanel::setRefreshRequestedHandler(
    std::function<void()> handler) {
    refreshRequestedHandler_ = std::move(handler);
}

void CameraControlPanel::setOpenRequestedHandler(
    std::function<void(const std::string&)> handler) {
    openRequestedHandler_ = std::move(handler);
}

void CameraControlPanel::setStartRequestedHandler(
    std::function<void()> handler) {
    startRequestedHandler_ = std::move(handler);
}

void CameraControlPanel::setStopRequestedHandler(
    std::function<void()> handler) {
    stopRequestedHandler_ = std::move(handler);
}

void CameraControlPanel::setCloseRequestedHandler(
    std::function<void()> handler) {
    closeRequestedHandler_ = std::move(handler);
}

void CameraControlPanel::updateControls() {
    const bool closed =
        state_.lifecycle == CameraPanelLifecycleState::Closed;
    const bool open =
        state_.lifecycle == CameraPanelLifecycleState::Open;
    const bool streaming =
        state_.lifecycle == CameraPanelLifecycleState::Streaming;

    const bool idle =
        !state_.discoveryPending &&
        !state_.lifecycleRequestPending &&
        !state_.otherOperationPending;

    deviceSelector_->setEnabled(closed && idle);
    refreshButton_->setEnabled(closed && idle);
    openButton_->setEnabled(closed && idle && selectedDeviceId().has_value());
    startButton_->setEnabled(open && idle);
    stopButton_->setEnabled(streaming && idle);
    // Faulted is intentionally closeable, but is not startable/stoppable.
    closeButton_->setEnabled(!closed && idle);
}

} // namespace camera::qtwidgets
