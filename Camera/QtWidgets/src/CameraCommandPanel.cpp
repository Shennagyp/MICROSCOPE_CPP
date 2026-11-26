#include "camera/qtwidgets/CameraCommandPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <unordered_set>
#include <utility>

namespace camera::qtwidgets {

struct CameraCommandPanel::CommandRow final {
    CameraCommandDescriptor descriptor;
    CameraCommandExecutionState state = CameraCommandExecutionState::Idle;
    std::string message;
    QWidget* widget = nullptr;
    QPushButton* executeButton = nullptr;
    QLabel* stateLabel = nullptr;
};

CameraCommandPanel::CameraCommandPanel(QWidget* parent)
    : QWidget(parent) {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);

    emptyLabel_ = new QLabel(QStringLiteral("No commands available"), this);
    emptyLabel_->setObjectName(QStringLiteral("cameraCommandEmptyLabel"));
    layout_->addWidget(emptyLabel_);
}

CameraCommandPanel::~CameraCommandPanel() {
    clearRows();
}

void CameraCommandPanel::clearRows() {
    for (auto& entry : rows_) {
        if (entry.second->widget) {
            layout_->removeWidget(entry.second->widget);
            delete entry.second->widget;
        }
        delete entry.second;
    }
    rows_.clear();
}

void CameraCommandPanel::setCommands(
    std::vector<CameraCommandDescriptor> descriptors) {
    clearRows();

    std::unordered_set<std::string> seen;
    for (auto& descriptor : descriptors) {
        if (descriptor.name.empty() || !seen.insert(descriptor.name).second)
            continue;

        auto* row = new CommandRow();
        row->descriptor = std::move(descriptor);
        row->widget = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row->widget);
        rowLayout->setContentsMargins(0, 2, 0, 2);
        rowLayout->setSpacing(8);

        auto* nameLabel = new QLabel(
            QString::fromStdString(
                row->descriptor.displayName.empty()
                    ? row->descriptor.name
                    : row->descriptor.displayName),
            row->widget);
        nameLabel->setObjectName(QStringLiteral("cameraCommandName"));

        row->executeButton = new QPushButton(
            QStringLiteral("Execute"), row->widget);
        row->executeButton->setObjectName(
            QStringLiteral("cameraCommandExecuteButton"));
        row->executeButton->setProperty(
            "commandName", QString::fromStdString(row->descriptor.name));

        row->stateLabel = new QLabel(row->widget);
        row->stateLabel->setObjectName(
            QStringLiteral("cameraCommandExecutionState"));
        row->stateLabel->setMinimumWidth(110);

        const std::string commandName = row->descriptor.name;
        connect(row->executeButton, &QPushButton::clicked, this,
                [this, commandName]() {
            const auto found = rows_.find(commandName);
            if (found == rows_.end() || !canExecute(*found->second))
                return;

            // A click means only that the host has been asked to execute.
            // The host must explicitly report backend issue and completion.
            found->second->state = CameraCommandExecutionState::RequestPending;
            found->second->message.clear();
            updateRow(*found->second);
            if (executeRequestedHandler_)
                executeRequestedHandler_(commandName);
        });

        rowLayout->addWidget(nameLabel, 1);
        rowLayout->addWidget(row->executeButton);
        rowLayout->addWidget(row->stateLabel);
        layout_->addWidget(row->widget);
        rows_.emplace(commandName, row);
        updateRow(*row);
    }

    emptyLabel_->setVisible(rows_.empty());
}

bool CameraCommandPanel::setExecutionState(
    const std::string& name,
    CameraCommandExecutionState state,
    std::string message) {
    const auto found = rows_.find(name);
    if (found == rows_.end())
        return false;

    found->second->state = state;
    found->second->message = std::move(message);
    updateRow(*found->second);
    return true;
}

void CameraCommandPanel::setExecuteRequestedHandler(
    ExecuteRequestedHandler handler) {
    executeRequestedHandler_ = std::move(handler);
    for (auto& entry : rows_)
        updateRow(*entry.second);
}

std::size_t CameraCommandPanel::commandCount() const noexcept {
    return rows_.size();
}

bool CameraCommandPanel::canExecute(const CommandRow& row) const noexcept {
    const bool terminal =
        row.state == CameraCommandExecutionState::Idle ||
        row.state == CameraCommandExecutionState::Completed ||
        row.state == CameraCommandExecutionState::Failed;
    return row.descriptor.isAvailable &&
           row.descriptor.isExecutable &&
           terminal &&
           static_cast<bool>(executeRequestedHandler_);
}

void CameraCommandPanel::updateRow(CommandRow& row) {
    if (row.executeButton) {
        row.executeButton->setEnabled(canExecute(row));
        if (!row.descriptor.isAvailable || !row.descriptor.isExecutable) {
            row.executeButton->setToolTip(
                QString::fromStdString(row.descriptor.disabledReason.value_or(
                    "This command is not executable.")));
        } else {
            row.executeButton->setToolTip({});
        }
    }

    if (!row.stateLabel)
        return;

    QString text;
    switch (row.state) {
    case CameraCommandExecutionState::Idle:
        if (row.descriptor.disabledReason) {
            text = QString::fromStdString(*row.descriptor.disabledReason);
        } else if (!row.descriptor.isAvailable) {
            text = QStringLiteral("Unavailable");
        } else if (!row.descriptor.isExecutable) {
            text = QStringLiteral("Not executable");
        } else {
            text = QStringLiteral("Ready");
        }
        break;
    case CameraCommandExecutionState::RequestPending:
        text = QStringLiteral("Request pending");
        break;
    case CameraCommandExecutionState::Executing:
        text = QStringLiteral("Executing");
        break;
    case CameraCommandExecutionState::Completed:
        text = QStringLiteral("Completed");
        break;
    case CameraCommandExecutionState::Failed:
        text = row.message.empty()
            ? QStringLiteral("Failed")
            : QString::fromStdString(row.message);
        break;
    }
    if (!row.message.empty() &&
        row.state != CameraCommandExecutionState::Failed) {
        text += QStringLiteral(": ") + QString::fromStdString(row.message);
    }
    row.stateLabel->setText(text);
    row.stateLabel->setAccessibleName(
        QString("%1 state: %2")
            .arg(QString::fromStdString(row.descriptor.displayName.empty()
                    ? row.descriptor.name
                    : row.descriptor.displayName),
                 text));
}

} // namespace camera::qtwidgets
