#pragma once

#include "camera/CameraModels.h"

#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QPushButton;

namespace camera::qtwidgets {

// Coarse presentation state for generic Open/Start/Stop/Close controls.
// This is not a backend lifecycle or session-state contract.
enum class CameraPanelLifecycleState : std::uint8_t {
    Closed,
    Open,
    Streaming,
    Faulted
};

// The host owns the actual camera state and pending operations. This value
// only tells the panel which user actions are currently valid to present.
struct CameraControlPanelState final {
    CameraPanelLifecycleState lifecycle =
        CameraPanelLifecycleState::Closed;
    bool discoveryPending = false;
    bool lifecycleRequestPending = false;
    bool otherOperationPending = false;
};

// Backend-neutral discovery and lifecycle controls. The panel owns only its
// widgets and current selection. It does not enumerate devices, open a
// session, schedule requests, or own a camera/backend object.
class CameraControlPanel final : public QWidget {
public:
    explicit CameraControlPanel(QWidget* parent = nullptr);

    void setDevices(std::vector<CameraDeviceInfo> devices);
    [[nodiscard]] std::optional<std::string> selectedDeviceId() const;

    void setState(CameraControlPanelState state);

    void setRefreshRequestedHandler(std::function<void()> handler);
    void setOpenRequestedHandler(
        std::function<void(const std::string&)> handler);
    void setStartRequestedHandler(std::function<void()> handler);
    void setStopRequestedHandler(std::function<void()> handler);
    void setCloseRequestedHandler(std::function<void()> handler);

private:
    void updateControls();

    QComboBox* deviceSelector_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;

    CameraControlPanelState state_;
    std::function<void()> refreshRequestedHandler_;
    std::function<void(const std::string&)> openRequestedHandler_;
    std::function<void()> startRequestedHandler_;
    std::function<void()> stopRequestedHandler_;
    std::function<void()> closeRequestedHandler_;
};

} // namespace camera::qtwidgets
