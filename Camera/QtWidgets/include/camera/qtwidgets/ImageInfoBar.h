#pragma once

#include <QWidget>

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class QHBoxLayout;
class QLabel;
class QScrollArea;
class QWidget;

namespace camera::qtwidgets {

struct ImageInfoBarItem final {
    std::string id;
    std::string label;
    std::string value;
    std::string unit;
    std::string toolTip;
    bool editable = false;
};

// Horizontal imaging-information strip. It renders host-supplied values and
// reports double-click edit intent; it never reads or writes camera features.
class ImageInfoBar final : public QWidget {
public:
    using EditRequestedHandler = std::function<void(const std::string&)>;

    explicit ImageInfoBar(QWidget* parent = nullptr);

    void setItems(std::vector<ImageInfoBarItem> items);
    void setEditRequestedHandler(EditRequestedHandler handler);

    [[nodiscard]] std::size_t itemCount() const noexcept;

private:
    class Field;

    QScrollArea* scrollArea_ = nullptr;
    QWidget* content_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    std::unordered_map<std::string, Field*> fields_;
    EditRequestedHandler editRequestedHandler_;
};

} // namespace camera::qtwidgets
