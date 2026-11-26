#include "camera/qtwidgets/CameraControlPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QString>

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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    auto* deviceRow = new QHBoxLayout();
    deviceRow->setContentsMargins(0, 0, 0, 0);
    deviceRow->setSpacing(5);
    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(5);

    deviceSelector_ = new QComboBox(this);
    deviceSelector_->setObjectName(QStringLiteral("cameraDeviceSelector"));
    deviceSelector_->setMinimumWidth(140);
    deviceSelector_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

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

    deviceRow->addWidget(deviceSelector_, 1);
    deviceRow->addWidget(refreshButton_);
    actionRow->addWidget(openButton_, 1);
    actionRow->addWidget(startButton_, 1);
    actionRow->addWidget(stopButton_, 1);
    actionRow->addWidget(closeButton_, 1);
    layout->addLayout(deviceRow);
    layout->addLayout(actionRow);

    connect(deviceSelector_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        updateControls();
        if (selectionChangedHandler_)
            selectionChangedHandler_(selectedDeviceId());
    });
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
    // Device IDs are opaque enumeration handles, not stable identity across
    // disconnect/replug. Rebuild selection from the new discovery snapshot;
    // never carry an old ID forward as if it identified the same camera.
    {
        const QSignalBlocker blocker(deviceSelector_);
        deviceSelector_->clear();
        for (const auto& device : devices) {
            deviceSelector_->addItem(
                deviceLabel(device),
                QString::fromStdString(device.id));
        }
    }
    updateControls();
    if (selectionChangedHandler_)
        selectionChangedHandler_(selectedDeviceId());
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
    updateControls();
}

void CameraControlPanel::setOpenRequestedHandler(
    std::function<void(const std::string&)> handler) {
    openRequestedHandler_ = std::move(handler);
    updateControls();
}

void CameraControlPanel::setSelectionChangedHandler(
    SelectionChangedHandler handler) {
    selectionChangedHandler_ = std::move(handler);
    if (selectionChangedHandler_)
        selectionChangedHandler_(selectedDeviceId());
}

void CameraControlPanel::setStartRequestedHandler(
    std::function<void()> handler) {
    startRequestedHandler_ = std::move(handler);
    updateControls();
}

void CameraControlPanel::setStopRequestedHandler(
    std::function<void()> handler) {
    stopRequestedHandler_ = std::move(handler);
    updateControls();
}

void CameraControlPanel::setCloseRequestedHandler(
    std::function<void()> handler) {
    closeRequestedHandler_ = std::move(handler);
    updateControls();
}

void CameraControlPanel::updateControls() {
    const bool closed =
        state_.lifecycle == CameraPanelLifecycleState::Closed;
    const bool open =
        state_.lifecycle == CameraPanelLifecycleState::Open;
    const bool streaming =
        state_.lifecycle == CameraPanelLifecycleState::Streaming;
    const bool transitioning =
        state_.lifecycle == CameraPanelLifecycleState::Starting ||
        state_.lifecycle == CameraPanelLifecycleState::Stopping;

    const bool idle =
        !state_.discoveryPending &&
        !state_.lifecycleRequestPending &&
        !state_.otherOperationPending &&
        !transitioning;

    deviceSelector_->setEnabled(closed && idle);
    refreshButton_->setEnabled(
        closed && idle && static_cast<bool>(refreshRequestedHandler_));
    openButton_->setEnabled(
        closed && idle && selectedDeviceId().has_value() &&
        static_cast<bool>(openRequestedHandler_));
    startButton_->setEnabled(
        open && idle && static_cast<bool>(startRequestedHandler_));
    stopButton_->setEnabled(
        streaming && idle && static_cast<bool>(stopRequestedHandler_));
    // Faulted is intentionally closeable, but is not startable/stoppable.
    closeButton_->setEnabled(
        !closed && idle && static_cast<bool>(closeRequestedHandler_));
}

} // namespace camera::qtwidgets
