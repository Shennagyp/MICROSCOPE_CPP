#include "camera/qtwidgets/CameraCapabilityPanel.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <array>

namespace camera::qtwidgets {

namespace {

struct CapabilityDefinition final {
    CameraCapability capability;
    const char* label;
};

constexpr std::array<CapabilityDefinition, CameraCapabilities::count>
    kCapabilityDefinitions{{
        {CameraCapability::VideoAcquisition, "Video acquisition"},
        {CameraCapability::RawPixelOutput, "RAW pixel output"},
        {CameraCapability::HardwareRegionOfInterest, "Hardware region of interest"},
        {CameraCapability::BinningOrSkipping, "Binning or skipping"},
        {CameraCapability::SoftwareTrigger, "Software trigger"},
        {CameraCapability::ExternalTrigger, "External trigger"},
        {CameraCapability::SingleFrameTrigger, "Single-frame trigger"},
        {CameraCapability::SelfTrigger, "Self trigger"},
        {CameraCapability::HardwareEvents, "Hardware events"},
        {CameraCapability::SensorTemperatureTelemetry, "Sensor temperature telemetry"},
        {CameraCapability::ThermoelectricCooling, "Thermoelectric cooling"},
        {CameraCapability::CoolingFan, "Cooling fan"},
        {CameraCapability::FocusMotor, "Focus motor"},
        {CameraCapability::AutoFocus, "Autofocus"},
        {CameraCapability::AstroAutoFocuser, "Astronomy auto-focuser"},
        {CameraCapability::ST4GuidePort, "ST4 guide port"},
        {CameraCapability::FilterWheel, "Filter wheel"},
        {CameraCapability::EmbeddedLightSource, "Embedded light source"},
        {CameraCapability::StandaloneLightSource, "Standalone light source"},
        {CameraCapability::HardwareFrameBuffer, "Hardware frame buffer"},
        {CameraCapability::LargeHardwareFrameBuffer, "Large hardware frame buffer"},
        {CameraCapability::ImageSignalProcessing, "Image signal processing"},
        {CameraCapability::BlackLevelControl, "Black level control"},
        {CameraCapability::PreciseFrameRateControl, "Precise frame-rate control"},
        {CameraCapability::AntiFogHeater, "Anti-fog heater"},
        {CameraCapability::HighFullWell, "High full well"},
        {CameraCapability::HDRConversionGain, "HDR conversion gain"}
    }};

QString capabilityStatusText(CapabilityStatus status) {
    switch (status) {
    case CapabilityStatus::Unknown:
        return QStringLiteral("Unknown");
    case CapabilityStatus::Unsupported:
        return QStringLiteral("Unsupported");
    case CapabilityStatus::Supported:
        return QStringLiteral("Supported");
    }
    return QStringLiteral("Unknown");
}

} // namespace

CameraCapabilityPanel::CameraCapabilityPanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    table_ = new QTableWidget(
        static_cast<int>(kCapabilityDefinitions.size()), 2, this);
    table_->setObjectName(QStringLiteral("cameraCapabilityTable"));
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Capability"), QStringLiteral("Model status")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setAlternatingRowColors(true);
    table_->setSortingEnabled(false);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    table_->setMinimumHeight(180);

    for (std::size_t i = 0; i < kCapabilityDefinitions.size(); ++i) {
        const auto& definition = kCapabilityDefinitions[i];
        auto* nameItem = new QTableWidgetItem(
            QString::fromLatin1(definition.label));
        nameItem->setFlags(Qt::ItemIsEnabled);
        nameItem->setData(
            Qt::UserRole, static_cast<int>(definition.capability));
        table_->setItem(static_cast<int>(i), 0, nameItem);

        auto* statusItem = new QTableWidgetItem(
            QStringLiteral("Unknown"));
        statusItem->setFlags(Qt::ItemIsEnabled);
        statusItem->setData(
            Qt::UserRole, static_cast<int>(CapabilityStatus::Unknown));
        table_->setItem(static_cast<int>(i), 1, statusItem);
    }

    layout->addWidget(table_);
}

void CameraCapabilityPanel::setCapabilities(
    const CameraCapabilities& capabilities) {
    for (std::size_t i = 0; i < kCapabilityDefinitions.size(); ++i) {
        const auto status =
            capabilities.status(kCapabilityDefinitions[i].capability);
        auto* item = table_->item(static_cast<int>(i), 1);
        item->setText(capabilityStatusText(status));
        item->setData(Qt::UserRole, static_cast<int>(status));
    }
}

} // namespace camera::qtwidgets
