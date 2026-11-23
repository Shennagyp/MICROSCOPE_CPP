#include "camera/qtwidgets/CameraControlPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QPushButton>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void fail(const char* message) {
    std::cerr << message << '\n';
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    camera::qtwidgets::CameraControlPanel panel;

    auto* selector =
        panel.findChild<QComboBox*>(QStringLiteral("cameraDeviceSelector"));
    auto* refresh =
        panel.findChild<QPushButton*>(QStringLiteral("refreshCamerasButton"));
    auto* open =
        panel.findChild<QPushButton*>(QStringLiteral("openCameraButton"));
    auto* start =
        panel.findChild<QPushButton*>(QStringLiteral("startCameraButton"));
    auto* stop =
        panel.findChild<QPushButton*>(QStringLiteral("stopCameraButton"));
    auto* close =
        panel.findChild<QPushButton*>(QStringLiteral("closeCameraButton"));

    if (!selector || !refresh || !open || !start || !stop || !close) {
        fail("CameraControlPanel did not create all expected controls.");
        return EXIT_FAILURE;
    }

    if (!selector->isEnabled() || !refresh->isEnabled() ||
        open->isEnabled() || start->isEnabled() ||
        stop->isEnabled() || close->isEnabled()) {
        fail("Initial closed/no-device control state is incorrect.");
        return EXIT_FAILURE;
    }

    int refreshRequests = 0;
    int openRequests = 0;
    int startRequests = 0;
    int stopRequests = 0;
    int closeRequests = 0;
    std::string openedDeviceId;

    panel.setRefreshRequestedHandler([&]() { ++refreshRequests; });
    panel.setOpenRequestedHandler([&](const std::string& deviceId) {
        ++openRequests;
        openedDeviceId = deviceId;
    });
    panel.setStartRequestedHandler([&]() { ++startRequests; });
    panel.setStopRequestedHandler([&]() { ++stopRequests; });
    panel.setCloseRequestedHandler([&]() { ++closeRequests; });

    refresh->click();
    if (refreshRequests != 1) {
        fail("Refresh did not invoke the host callback.");
        return EXIT_FAILURE;
    }

    camera::CameraDeviceInfo first;
    first.id = "opaque-device-id-A";
    first.displayName = "Microscope A";

    camera::CameraDeviceInfo second;
    second.id = "opaque-device-id-B";
    second.identity.model = "Model B";

    panel.setDevices({first, second});
    if (selector->count() != 2 ||
        selector->itemText(0) != QStringLiteral("Microscope A") ||
        selector->itemText(1) != QStringLiteral("Model B") ||
        selector->itemData(0).toString() != QStringLiteral("opaque-device-id-A")) {
        fail("Device labels or opaque IDs were not preserved.");
        return EXIT_FAILURE;
    }
    if (!open->isEnabled()) {
        fail("Open was not enabled for a selected device.");
        return EXIT_FAILURE;
    }

    selector->setCurrentIndex(1);
    open->click();
    if (openRequests != 1 || openedDeviceId != "opaque-device-id-B") {
        fail("Open did not report the selected opaque device ID unchanged.");
        return EXIT_FAILURE;
    }

    camera::qtwidgets::CameraControlPanelState state;
    state.discoveryPending = true;
    panel.setState(state);
    if (selector->isEnabled() || refresh->isEnabled() || open->isEnabled()) {
        fail("Discovery-pending state did not block discovery/open controls.");
        return EXIT_FAILURE;
    }

    state = {};
    state.lifecycleRequestPending = true;
    panel.setState(state);
    if (selector->isEnabled() || refresh->isEnabled() || open->isEnabled() ||
        start->isEnabled() || stop->isEnabled() || close->isEnabled()) {
        fail("Lifecycle-pending state did not block all lifecycle controls.");
        return EXIT_FAILURE;
    }

    state = {};
    state.lifecycle = camera::qtwidgets::CameraPanelLifecycleState::Open;
    panel.setState(state);
    if (selector->isEnabled() || refresh->isEnabled() || open->isEnabled() ||
        !start->isEnabled() || stop->isEnabled() || !close->isEnabled()) {
        fail("Open-state control enablement is incorrect.");
        return EXIT_FAILURE;
    }
    start->click();
    close->click();
    if (startRequests != 1 || closeRequests != 1) {
        fail("Open-state Start/Close callbacks were not dispatched.");
        return EXIT_FAILURE;
    }

    state.lifecycle = camera::qtwidgets::CameraPanelLifecycleState::Streaming;
    panel.setState(state);
    if (start->isEnabled() || !stop->isEnabled() || !close->isEnabled()) {
        fail("Streaming-state control enablement is incorrect.");
        return EXIT_FAILURE;
    }
    stop->click();
    if (stopRequests != 1) {
        fail("Streaming-state Stop callback was not dispatched.");
        return EXIT_FAILURE;
    }

    state = {};
    state.lifecycle = camera::qtwidgets::CameraPanelLifecycleState::Faulted;
    panel.setState(state);
    if (start->isEnabled() || stop->isEnabled() || !close->isEnabled()) {
        fail("Faulted state must permit Close only.");
        return EXIT_FAILURE;
    }

    state.otherOperationPending = true;
    panel.setState(state);
    if (close->isEnabled()) {
        fail("Other pending operation did not block Close.");
        return EXIT_FAILURE;
    }

    panel.setDevices({});
    state = {};
    panel.setState(state);
    if (selector->count() != 0 || open->isEnabled()) {
        fail("Empty discovery result left Open enabled.");
        return EXIT_FAILURE;
    }

    std::cout << "All Qt Widgets camera control panel tests passed.\n";
    return EXIT_SUCCESS;
}
