#include "camera/qtwidgets/CameraCapabilityPanel.h"

#include <QApplication>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <cstdlib>
#include <iostream>

namespace {

void fail(const char* message) {
    std::cerr << message << '\n';
}

int rowFor(QTableWidget* table, const QString& label) {
    if (!table) return -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto* item = table->item(row, 0);
        if (item && item->text() == label)
            return row;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    camera::qtwidgets::CameraCapabilityPanel panel;
    auto* table = panel.findChild<QTableWidget*>(
        QStringLiteral("cameraCapabilityTable"));
    if (!table || table->rowCount() !=
            static_cast<int>(camera::CameraCapabilities::count) ||
        table->columnCount() != 2) {
        fail("Capability panel did not render the complete common capability set.");
        return EXIT_FAILURE;
    }

    const int videoRow = rowFor(table, QStringLiteral("Video acquisition"));
    const int rawRow = rowFor(table, QStringLiteral("RAW pixel output"));
    const int triggerRow = rowFor(table, QStringLiteral("External trigger"));
    if (videoRow < 0 || rawRow < 0 || triggerRow < 0 ||
        table->item(videoRow, 1)->text() != QStringLiteral("Unknown") ||
        table->item(rawRow, 1)->text() != QStringLiteral("Unknown")) {
        fail("Unspecified capability states were not preserved as Unknown.");
        return EXIT_FAILURE;
    }

    camera::CameraCapabilities capabilities;
    capabilities.set(camera::CameraCapability::VideoAcquisition, true);
    capabilities.set(camera::CameraCapability::RawPixelOutput, false);
    capabilities.set(camera::CameraCapability::ExternalTrigger, true);
    panel.setCapabilities(capabilities);

    const int filterWheelRow = rowFor(table, QStringLiteral("Filter wheel"));
    if (filterWheelRow < 0 ||
        table->item(videoRow, 1)->text() != QStringLiteral("Supported") ||
        table->item(rawRow, 1)->text() != QStringLiteral("Unsupported") ||
        table->item(triggerRow, 1)->text() != QStringLiteral("Supported") ||
        table->item(filterWheelRow, 1)->text() != QStringLiteral("Unknown")) {
        fail("Capability tri-state projection is incorrect.");
        return EXIT_FAILURE;
    }

    panel.setCapabilities({});
    if (table->item(videoRow, 1)->text() != QStringLiteral("Unknown") ||
        table->item(rawRow, 1)->text() != QStringLiteral("Unknown")) {
        fail("Reset capability projection did not restore Unknown states.");
        return EXIT_FAILURE;
    }

    std::cout << "All Qt Widgets capability panel tests passed.\n";
    return EXIT_SUCCESS;
}
