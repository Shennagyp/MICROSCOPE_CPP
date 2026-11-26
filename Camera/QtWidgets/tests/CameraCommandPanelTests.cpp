#include "camera/qtwidgets/CameraCommandPanel.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void fail(const char* message) {
    std::cerr << message << '\\n';
}

QPushButton* commandButton(
    camera::qtwidgets::CameraCommandPanel& panel,
    const char* name) {
    const auto buttons = panel.findChildren<QPushButton*>(
        QStringLiteral("cameraCommandExecuteButton"));
    for (auto* button : buttons) {
        if (button->property("commandName").toString() ==
            QString::fromLatin1(name)) {
            return button;
        }
    }
    return nullptr;
}

QString commandStateText(
    camera::qtwidgets::CameraCommandPanel& panel,
    const char* name) {
    const auto labels = panel.findChildren<QLabel*>(
        QStringLiteral("cameraCommandExecutionState"));
    for (auto* label : labels) {
        if (label->accessibleName().startsWith(QString::fromLatin1(name) +
                                                QStringLiteral(" state:"))) {
            return label->text();
        }
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    using namespace camera;
    using namespace camera::qtwidgets;

    CameraCommandPanel panel;
    CameraCommandDescriptor run;
    run.name = "RunCalibration";
    run.displayName = "Run calibration";
    run.isAvailable = true;
    run.isExecutable = true;

    CameraCommandDescriptor unavailable;
    unavailable.name = "ResetSensor";
    unavailable.displayName = "Reset sensor";
    unavailable.isAvailable = false;
    unavailable.isExecutable = false;
    unavailable.disabledReason = "Not supported by this device.";

    CameraCommandDescriptor notExecutable;
    notExecutable.name = "SaveCalibration";
    notExecutable.displayName = "Save calibration";
    notExecutable.isAvailable = true;
    notExecutable.isExecutable = false;
    notExecutable.disabledReason = "Command is read-only.";

    panel.setCommands({run, unavailable, notExecutable, run});
    if (panel.commandCount() != 3) {
        fail("Command panel did not reject duplicate command names.");
        return EXIT_FAILURE;
    }

    auto* runButton = commandButton(panel, "RunCalibration");
    auto* resetButton = commandButton(panel, "ResetSensor");
    auto* saveButton = commandButton(panel, "SaveCalibration");
    if (!runButton || !resetButton || !saveButton ||
        runButton->isEnabled() || resetButton->isEnabled() ||
        saveButton->isEnabled()) {
        fail("Initial command button state is incorrect.");
        return EXIT_FAILURE;
    }

    int requests = 0;
    std::string requestedName;
    panel.setExecuteRequestedHandler([&](const std::string& name) {
        ++requests;
        requestedName = name;
    });
    if (!runButton->isEnabled() || resetButton->isEnabled() ||
        saveButton->isEnabled()) {
        fail("Availability/executability gating is incorrect.");
        return EXIT_FAILURE;
    }

    runButton->click();
    if (requests != 1 || requestedName != "RunCalibration" ||
        runButton->isEnabled() ||
        commandStateText(panel, "Run calibration") !=
            QStringLiteral("Request pending")) {
        fail("Command click did not create a pending request state.");
        return EXIT_FAILURE;
    }
    if (!panel.setExecutionState(
            "RunCalibration", CameraCommandExecutionState::Executing) ||
        runButton->isEnabled() ||
        commandStateText(panel, "Run calibration") !=
            QStringLiteral("Executing")) {
        fail("Backend issue was not represented as executing.");
        return EXIT_FAILURE;
    }
    if (!panel.setExecutionState(
            "RunCalibration", CameraCommandExecutionState::Completed) ||
        !runButton->isEnabled() ||
        commandStateText(panel, "Run calibration") !=
            QStringLiteral("Completed")) {
        fail("Device completion was not represented independently.");
        return EXIT_FAILURE;
    }

    panel.setExecutionState(
        "RunCalibration", CameraCommandExecutionState::Failed,
        "Device rejected command");
    if (!runButton->isEnabled() ||
        commandStateText(panel, "Run calibration") !=
            QStringLiteral("Device rejected command")) {
        fail("Command failure state/message was not represented.");
        return EXIT_FAILURE;
    }

    runButton->click();
    if (requests != 2 || requestedName != "RunCalibration" ||
        runButton->isEnabled()) {
        fail("Failed command could not be retried.");
        return EXIT_FAILURE;
    }

    if (panel.setExecutionState(
            "MissingCommand", CameraCommandExecutionState::Completed)) {
        fail("Unknown command state update was incorrectly accepted.");
        return EXIT_FAILURE;
    }

    panel.setCommands({});
    if (panel.commandCount() != 0 ||
        !panel.findChild<QLabel*>(
            QStringLiteral("cameraCommandEmptyLabel"))->isVisible()) {
        fail("Empty command set did not show its empty state.");
        return EXIT_FAILURE;
    }

    std::cout << "All Qt Widgets camera command panel tests passed.\\n";
    return EXIT_SUCCESS;
}
