#pragma once

#include "camera/CameraCommandAccess.h"

#include <QWidget>

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class QLabel;
class QVBoxLayout;

namespace camera::qtwidgets {

enum class CameraCommandExecutionState : std::uint8_t {
    Idle,
    RequestPending,
    Executing,
    Completed,
    Failed
};

// Backend-neutral command presentation. The host owns command access,
// scheduling, execution polling and authoritative completion reporting.
class CameraCommandPanel final : public QWidget {
public:
    using ExecuteRequestedHandler =
        std::function<void(const std::string& commandName)>;

    explicit CameraCommandPanel(QWidget* parent = nullptr);
    ~CameraCommandPanel() override;

    void setCommands(std::vector<CameraCommandDescriptor> descriptors);
    [[nodiscard]] bool setExecutionState(
        const std::string& name,
        CameraCommandExecutionState state,
        std::string message = {});
    void setExecuteRequestedHandler(ExecuteRequestedHandler handler);

    [[nodiscard]] std::size_t commandCount() const noexcept;

private:
    struct CommandRow;

    void clearRows();
    [[nodiscard]] bool canExecute(const CommandRow& row) const noexcept;
    void updateRow(CommandRow& row);

    QVBoxLayout* layout_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    std::unordered_map<std::string, CommandRow*> rows_;
    ExecuteRequestedHandler executeRequestedHandler_;
};

} // namespace camera::qtwidgets
