#include "camera/qtwidgets/ImageInfoBar.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <unordered_set>
#include <utility>

namespace camera::qtwidgets {

class ImageInfoBar::Field final : public QFrame {
public:
    explicit Field(QWidget* parent = nullptr)
        : QFrame(parent) {
        setObjectName(QStringLiteral("imageInfoField"));
        setFrameShape(QFrame::NoFrame);
        setFrameShadow(QFrame::Plain);
        setStyleSheet(QStringLiteral(
            "#imageInfoField { background: transparent; border: none; "
            "border-right: 1px solid #d8dee7; }"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        setFocusPolicy(Qt::StrongFocus);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 1, 8, 1);
        layout->setSpacing(0);
        setMinimumHeight(38);

        label_ = new QLabel(this);
        label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        value_ = new QLabel(this);
        value_->setAttribute(Qt::WA_TransparentForMouseEvents);
        label_->setStyleSheet(QStringLiteral(
            "font-size: 8pt; font-weight: 500; color: #667085;"));
        value_->setStyleSheet(QStringLiteral(
            "font-size: 9pt; font-weight: 600; color: #202938;"));

        layout->addWidget(label_);
        layout->addWidget(value_);
    }

    void setItem(const ImageInfoBarItem& item) {
        id_ = item.id;
        label_->setText(QString::fromStdString(item.label));

        QString valueText = QString::fromStdString(item.value);
        if (!item.unit.empty()) {
            valueText += QStringLiteral(" ") + QString::fromStdString(item.unit);
        }
        value_->setText(valueText);

        editable_ = item.editable;
        setCursor(editable_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(QString::fromStdString(item.toolTip));
        label_->setToolTip(QString::fromStdString(item.toolTip));
        value_->setToolTip(QString::fromStdString(item.toolTip));
        setAccessibleName(QString("%1: %2").arg(label_->text(), value_->text()));
        setAccessibleDescription(editable_
            ? QStringLiteral("Double-click to edit this imaging property.")
            : QStringLiteral("Read-only imaging information."));
    }

    void setEditHandler(std::function<void(const std::string&)> handler) {
        editHandler_ = std::move(handler);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (editable_ &&
            (event->key() == Qt::Key_Return ||
             event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) &&
            editHandler_) {
            editHandler_(id_);
            event->accept();
            return;
        }
        QFrame::keyPressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && editable_ && editHandler_) {
            editHandler_(id_);
            event->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(event);
    }

private:
    QLabel* label_ = nullptr;
    QLabel* value_ = nullptr;
    std::string id_;
    bool editable_ = false;
    std::function<void(const std::string&)> editHandler_;
};

ImageInfoBar::ImageInfoBar(QWidget* parent)
    : QWidget(parent) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName(QStringLiteral("imageInfoScrollArea"));
    scrollArea_->setWidgetResizable(false);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    content_ = new QWidget(scrollArea_);
    content_->setObjectName(QStringLiteral("imageInfoContent"));
    content_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    layout_ = new QHBoxLayout(content_);
    layout_->setContentsMargins(6, 1, 6, 1);
    layout_->setSpacing(2);

    emptyLabel_ = new QLabel(QStringLiteral("No imaging information"), content_);
    emptyLabel_->setObjectName(QStringLiteral("imageInfoEmptyLabel"));
    layout_->addWidget(emptyLabel_);

    scrollArea_->setWidget(content_);
    root->addWidget(scrollArea_);

    setMinimumHeight(42);
    setMaximumHeight(48);
    setStyleSheet(QStringLiteral(
        "QScrollArea { background: #f7f8fa; border: none; }"
        "QWidget#imageInfoContent { background: #f7f8fa; }"
        "QLabel#imageInfoEmptyLabel { color: #667085; font-size: 8pt; }"));
}

void ImageInfoBar::setItems(std::vector<ImageInfoBarItem> items) {
    std::unordered_set<std::string> active;
    active.reserve(items.size());

    while (QLayoutItem* item = layout_->takeAt(0)) {
        delete item;
    }

    for (auto& item : items) {
        if (item.id.empty() || !active.insert(item.id).second) {
            continue;
        }

        auto found = fields_.find(item.id);
        if (found == fields_.end()) {
            auto* field = new Field(content_);
            const std::string id = item.id;
            field->setEditHandler([this](const std::string& fieldId) {
                if (editRequestedHandler_) {
                    editRequestedHandler_(fieldId);
                }
            });
            found = fields_.emplace(id, field).first;
        }

        found->second->setItem(item);
        layout_->addWidget(found->second);
    }

    for (auto it = fields_.begin(); it != fields_.end();) {
        if (active.find(it->first) == active.end()) {
            delete it->second;
            it = fields_.erase(it);
        } else {
            ++it;
        }
    }

    emptyLabel_->setVisible(active.empty());
    if (active.empty()) {
        layout_->addWidget(emptyLabel_);
    }

    content_->adjustSize();
    content_->resize(content_->sizeHint());
    scrollArea_->updateGeometry();
}

void ImageInfoBar::setEditRequestedHandler(EditRequestedHandler handler) {
    editRequestedHandler_ = std::move(handler);
}

std::size_t ImageInfoBar::itemCount() const noexcept {
    return fields_.size();
}

} // namespace camera::qtwidgets
