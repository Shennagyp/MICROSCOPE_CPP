#pragma once

#include "camera/CameraModels.h"

#include <QWidget>

class QTableWidget;

namespace camera::qtwidgets {

// Read-only presentation of the model capability tri-state. Unknown is shown
// explicitly and is never collapsed into Unsupported.
class CameraCapabilityPanel final : public QWidget {
public:
    explicit CameraCapabilityPanel(QWidget* parent = nullptr);

    void setCapabilities(const CameraCapabilities& capabilities);

private:
    QTableWidget* table_ = nullptr;
};

} // namespace camera::qtwidgets
