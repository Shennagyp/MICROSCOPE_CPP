#include "camera/qtwidgets/ImageInfoBar.h"

#include <QApplication>
#include <QFrame>
#include <QMouseEvent>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void fail(const char* message) {
    std::cerr << message << '\n';
}

bool doubleClick(QWidget* widget) {
    QMouseEvent event(
        QEvent::MouseButtonDblClick,
        QPointF(3.0, 3.0),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    return QApplication::sendEvent(widget, &event);
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    camera::qtwidgets::ImageInfoBar bar;
    int editRequests = 0;
    std::string editedId;
    bar.setEditRequestedHandler([&](const std::string& id) {
        ++editRequests;
        editedId = id;
    });

    bar.setItems({
        {"frame-size", "Frame", "1280 × 960", "px", "Current frame dimensions", false},
        {"exposure-time", "Exposure", "10000", "us", "Double-click to edit exposure", true}
    });

    if (bar.itemCount() != 2) {
        fail("ImageInfoBar item count is incorrect.");
        return EXIT_FAILURE;
    }

    auto* exposure = bar.findChild<QFrame*>(
        QStringLiteral("imageInfoField"), Qt::FindChildrenRecursively);
    if (!exposure) {
        fail("ImageInfoBar did not create a field.");
        return EXIT_FAILURE;
    }

    // Fields are direct children of the content widget. Select the editable
    // item using its accessible name rather than relying on layout internals.
    QFrame* editableField = nullptr;
    const auto fields = bar.findChildren<QFrame*>();
    for (auto* field : fields) {
        if (field->accessibleName().startsWith(QStringLiteral("Exposure:"))) {
            editableField = field;
            break;
        }
    }
    if (!editableField) {
        fail("Editable exposure field was not found.");
        return EXIT_FAILURE;
    }

    doubleClick(editableField);
    if (editRequests != 1 || editedId != "exposure-time") {
        fail("Double-click did not report the editable item ID.");
        return EXIT_FAILURE;
    }

    bar.setItems({
        {"frame-size", "Frame", "1280 × 960", "px", "Read-only", false}
    });
    if (bar.itemCount() != 1) {
        fail("Removed item was not removed from the information bar.");
        return EXIT_FAILURE;
    }

    editRequests = 0;
    const auto remaining = bar.findChildren<QFrame*>();
    QFrame* readOnlyField = nullptr;
    for (auto* field : remaining) {
        if (field->accessibleName().startsWith(QStringLiteral("Frame:"))) {
            readOnlyField = field;
            break;
        }
    }
    if (!readOnlyField) {
        fail("Read-only frame field was not found.");
        return EXIT_FAILURE;
    }

    doubleClick(readOnlyField);
    if (editRequests != 0) {
        fail("Read-only field incorrectly emitted an edit request.");
        return EXIT_FAILURE;
    }

    bar.setItems({});
    if (bar.itemCount() != 0) {
        fail("Empty item list did not clear the bar.");
        return EXIT_FAILURE;
    }

    std::cout << "All Qt Widgets image info bar tests passed.\n";
    return EXIT_SUCCESS;
}
