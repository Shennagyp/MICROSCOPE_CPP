#include "camera/qtwidgets/CameraFeatureEditor.h"

#include "camera/CameraFeatureValidation.h"

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <system_error>
#include <type_traits>
#include <utility>

namespace camera::qtwidgets {

namespace {

QString featureValueText(const CameraFeatureValue& value) {
    return std::visit([](const auto& item) -> QString {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) {
            return item ? QStringLiteral("On") : QStringLiteral("Off");
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            return QString::number(static_cast<qlonglong>(item));
        } else if constexpr (std::is_same_v<T, std::vector<std::int64_t>>) {
            QStringList values;
            values.reserve(static_cast<qsizetype>(item.size()));
            for (const auto number : item) {
                values.push_back(QString::number(static_cast<qlonglong>(number)));
            }
            return values.join(QStringLiteral(", "));
        } else if constexpr (std::is_same_v<T, double>) {
            return QString::number(item, 'g', 15);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return QString::fromStdString(item);
        } else {
            return QString::fromStdString(item.symbol);
        }
    }, value);
}

std::optional<std::int64_t> parseInteger(const QString& text) {
    const QByteArray bytes = text.trimmed().toLatin1();
    if (bytes.isEmpty()) {
        return std::nullopt;
    }

    std::int64_t value = 0;
    const char* begin = bytes.constData();
    const char* end = begin + bytes.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> parseFloatingPoint(const QString& text) {
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

QString floatingRangePlaceholder(const CameraFeatureDescriptor& descriptor) {
    if (!descriptor.numericRange) {
        return QStringLiteral("Numeric value");
    }
    const auto* range =
        std::get_if<CameraFloatingPointRange>(&*descriptor.numericRange);
    if (!range) {
        return QStringLiteral("Invalid floating-point range descriptor");
    }
    return QStringLiteral("%1 … %2")
        .arg(range->minimum, 0, 'g', 12)
        .arg(range->maximum, 0, 'g', 12);
}

} // namespace

CameraFeatureEditor::CameraFeatureEditor(
    CameraFeatureDescriptor descriptor,
    QWidget* parent)
    : QWidget(parent),
      descriptor_(std::move(descriptor)) {
    setObjectName(QStringLiteral("cameraFeatureEditor"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    valueStack_ = new QStackedWidget(this);
    valueStack_->setObjectName(QStringLiteral("cameraFeatureEditorStack"));

    placeholder_ = new QLabel(valueStack_);
    placeholder_->setObjectName(
        QStringLiteral("cameraFeatureEditorPlaceholder"));
    placeholder_->setWordWrap(true);
    placeholder_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueStack_->addWidget(placeholder_);

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("cameraFeatureEditorError"));
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet(QStringLiteral("color: #b42318;"));
    errorLabel_->hide();

    layout->addWidget(valueStack_);
    layout->addWidget(errorLabel_);

    rebuildEditor();
    updateControls();
}

void CameraFeatureEditor::setDescriptor(CameraFeatureDescriptor descriptor) {
    descriptor_ = std::move(descriptor);
    currentValue_.reset();
    currentValueValid_ = false;
    pending_ = false;
    errorMessage_.reset();
    rebuildEditor();
    updateControls();
}

bool CameraFeatureEditor::setCurrentValue(
    std::optional<CameraFeatureValue> value) {
    currentValue_ = std::move(value);
    currentValueValid_ = false;
    errorMessage_.reset();

    if (!currentValue_) {
        updateControls();
        return true;
    }

    if (camera::cameraFeatureKind(*currentValue_) != descriptor_.kind) {
        errorMessage_ =
            "Camera readback type does not match the feature descriptor.";
        updateControls();
        return false;
    }

    // Readback validation must not be rejected merely because the feature is
    // read-only or temporarily unavailable. Validate its value semantics here;
    // updateControls separately enforces the original availability/write flags.
    CameraFeatureDescriptor validationDescriptor = descriptor_;
    validationDescriptor.isAvailable = true;
    validationDescriptor.isWritable = true;
    const auto validation = validateFeatureWrite(
        validationDescriptor, *currentValue_);
    if (const auto* error =
            std::get_if<CameraFeatureAccessError>(&validation)) {
        errorMessage_ = error->message;
        updateControls();
        return false;
    }

    currentValueValid_ = true;
    applyCurrentValue();
    updateControls();
    return true;
}

void CameraFeatureEditor::setPending(bool pending) {
    pending_ = pending;
    updateControls();
}

void CameraFeatureEditor::setErrorMessage(
    std::optional<std::string> message) {
    errorMessage_ = std::move(message);
    updateControls();
}

void CameraFeatureEditor::setWriteRequestedHandler(
    WriteRequestedHandler handler) {
    writeRequestedHandler_ = std::move(handler);
    updateControls();
}

void CameraFeatureEditor::setEnumerationDisplayNameProvider(
    EnumerationDisplayNameProvider provider) {
    enumerationDisplayNameProvider_ = std::move(provider);
    rebuildEditor();
    if (currentValue_ && currentValueValid_) {
        applyCurrentValue();
    }
    updateControls();
}

const CameraFeatureDescriptor& CameraFeatureEditor::descriptor() const noexcept {
    return descriptor_;
}

bool CameraFeatureEditor::hasCurrentValue() const noexcept {
    return currentValue_.has_value();
}

bool CameraFeatureEditor::canWrite() const noexcept {
    const bool currentStateKnown =
        !descriptor_.isReadable ||
        (currentValue_.has_value() && currentValueValid_);
    return editor_ != nullptr &&
        descriptor_.isAvailable &&
        descriptor_.isWritable &&
        currentStateKnown &&
        !pending_ &&
        static_cast<bool>(writeRequestedHandler_);
}

QWidget* CameraFeatureEditor::editorWidget() const noexcept {
    return editor_;
}

void CameraFeatureEditor::rebuildEditor() {
    if (editor_) {
        valueStack_->removeWidget(editor_);
        delete editor_;
        editor_ = nullptr;
    }

    // A missing readable value is not represented by a fabricated editor
    // value. In particular, no checkbox false or numeric zero is presented as
    // camera state before the host supplies a current readback.
    if (!descriptor_.isAvailable || !descriptor_.isWritable) {
        return;
    }

    switch (descriptor_.kind) {
    case CameraFeatureKind::Boolean: {
        auto* control = new QCheckBox(this);
        control->setObjectName(QStringLiteral("cameraFeatureEditorInput"));
        if (!descriptor_.isReadable) {
            // PartiallyChecked means "no readback/current value". It is not
            // displayed as a false device state; the operator chooses a value.
            control->setTristate(true);
            control->setCheckState(Qt::PartiallyChecked);
        }
        // clicked() represents an operator action even when a tri-state
        // checkbox moves from PartiallyChecked to Checked (where the derived
        // boolean may remain true and toggled() need not fire).
        connect(control, &QCheckBox::clicked, this, [this](bool) {
            submitEditedValue();
        });
        editor_ = control;
        break;
    }
    case CameraFeatureKind::Integer: {
        if (!descriptor_.discreteIntegerValues.empty()) {
            auto* control = new QComboBox(this);
            control->setObjectName(QStringLiteral("cameraFeatureEditorInput"));
            for (const auto number : descriptor_.discreteIntegerValues) {
                control->addItem(
                    QString::number(static_cast<qlonglong>(number)),
                    QVariant::fromValue<qlonglong>(
                        static_cast<qlonglong>(number)));
            }
            control->setCurrentIndex(-1);
            connect(control, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this](int index) {
                if (index >= 0) {
                    submitEditedValue();
                }
            });
            editor_ = control;
        } else {
            auto* control = new QLineEdit(this);
            control->setObjectName(QStringLiteral("cameraFeatureEditorInput"));
            control->setPlaceholderText(QStringLiteral("Integer value"));
            connect(control, &QLineEdit::editingFinished,
                    this, [this]() { submitEditedValue(); });
            editor_ = control;
        }
        break;
    }
    case CameraFeatureKind::IntegerVector: {
        auto* control = new QLineEdit(this);
        control->setObjectName(QStringLiteral("cameraFeatureEditorInput"));
        control->setPlaceholderText(
            descriptor_.elementCount
                ? QStringLiteral("%1 comma-separated integers")
                      .arg(static_cast<qulonglong>(*descriptor_.elementCount))
                : QStringLiteral("Comma-separated integers"));
        connect(control, &QLineEdit::editingFinished,
                this, [this]() { submitEditedValue(); });
        editor_ = control;
        break;
    }
    case CameraFeatureKind::FloatingPoint: {
        if (!descriptor_.numericRange || !descriptor_.isReadable) {
            // QDoubleSpinBox starts at a numeric value (normally zero). For a
            // write-only feature that would visually invent a current value.
            auto* control = new QLineEdit(this);
            control->setObjectName(QStringLiteral("cameraFeatureEditorInput"));
            control->setPlaceholderText(
                floatingRangePlaceholder(descriptor_));
            connect(control, &QLineEdit::editingFinished,
                    this, [this]() { submitEditedValue(); });
            editor_ = control;
            break;
        }

        const auto* range =
            std::get_if<CameraFloatingPointRange>(&*descriptor_.numericRange);
        if (!range ||
            !std::isfinite(range->minimum) ||
            !std::isfinite(range->maximum) ||
            range->minimum > range->maximum ||
            (range->increment &&
             (!std::isfinite(*range->increment) || *range->increment <= 0.0))) {
            placeholder_->setText(
                QStringLiteral("Invalid floating-point range descriptor."));
            return;
        }

        auto* control = new QDoubleSpinBox(this);
        control->setObjectName(QStringLiteral("cameraFeatureEditorInput"));
        control->setDecimals(12);
        control->setRange(range->minimum, range->maximum);
        control->setSingleStep(range->increment.value_or(1.0));
        control->setKeyboardTracking(false);
        connect(control, &QDoubleSpinBox::editingFinished,
                this, [this]() { submitEditedValue(); });
        editor_ = control;
        break;
    }
    case CameraFeatureKind::String: {
        auto* control = new QLineEdit(this);
        control->setObjectName(QStringLiteral("cameraFeatureEditorInput"));
        control->setPlaceholderText(QStringLiteral("Text value"));
        connect(control, &QLineEdit::editingFinished,
                this, [this]() { submitEditedValue(); });
        editor_ = control;
        break;
    }
    case CameraFeatureKind::Enumeration: {
        if (descriptor_.enumerationValues.empty()) {
            placeholder_->setText(
                QStringLiteral("No enumeration values reported by the camera."));
            return;
        }

        auto* control = new QComboBox(this);
        control->setObjectName(QStringLiteral("cameraFeatureEditorInput"));
        for (const auto& symbol : descriptor_.enumerationValues) {
            std::string displayName = enumerationDisplayNameProvider_
                ? enumerationDisplayNameProvider_(symbol)
                : symbol;
            if (displayName.empty()) {
                displayName = symbol;
            }
            control->addItem(
                QString::fromStdString(displayName),
                QString::fromStdString(symbol));
        }
        control->setCurrentIndex(-1);
        connect(control, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int index) {
            if (index >= 0) {
                submitEditedValue();
            }
        });
        editor_ = control;
        break;
    }
    }

    if (editor_) {
        if (descriptor_.disabledReason) {
            editor_->setToolTip(
                QString::fromStdString(*descriptor_.disabledReason));
        }
        valueStack_->addWidget(editor_);
    }
}

void CameraFeatureEditor::updateControls() {
    const bool hasUsableValue =
        currentValue_.has_value() && currentValueValid_;
    const bool editorHasRequiredState =
        !descriptor_.isReadable || hasUsableValue;
    const bool canPresentEditor =
        editor_ && descriptor_.isAvailable && descriptor_.isWritable &&
        editorHasRequiredState;

    // Hidden editors are disabled too. This prevents callers/tests from
    // treating a not-yet-populated input as an actionable control.
    if (editor_) {
        editor_->setEnabled(canWrite());
    }

    if (canPresentEditor) {
        valueStack_->setCurrentWidget(editor_);
    } else {
        QString message;
        if (!descriptor_.isAvailable) {
            message = descriptor_.disabledReason
                ? QString::fromStdString(*descriptor_.disabledReason)
                : QStringLiteral("Feature is unavailable.");
        } else if (!descriptor_.isWritable && hasUsableValue) {
            message = featureValueText(*currentValue_);
        } else if (!descriptor_.isReadable) {
            message = editor_
                ? QStringLiteral("Choose a value to submit; current value is not readable.")
                : QStringLiteral(
                      "No writable editor can be created from the reported descriptor.");
        } else if (!currentValue_) {
            message = QStringLiteral(
                "Waiting for the current camera readback.");
        } else if (!currentValueValid_) {
            message = QStringLiteral(
                "Current readback is invalid for this feature descriptor.");
        } else if (!editor_) {
            message = QStringLiteral(
                "No editor can be created from the reported feature descriptor.");
        } else {
            message = featureValueText(*currentValue_);
        }
        placeholder_->setText(message);
        valueStack_->setCurrentWidget(placeholder_);
    }

    if (errorMessage_ && !errorMessage_->empty()) {
        errorLabel_->setText(QString::fromStdString(*errorMessage_));
        errorLabel_->show();
    } else {
        errorLabel_->clear();
        errorLabel_->hide();
    }

    if (descriptor_.disabledReason && editor_) {
        editor_->setToolTip(
            QString::fromStdString(*descriptor_.disabledReason));
    }
}

void CameraFeatureEditor::applyCurrentValue() {
    if (!editor_ || !currentValue_) {
        return;
    }

    const QSignalBlocker blocker(editor_);
    if (auto* control = qobject_cast<QCheckBox*>(editor_)) {
        control->setTristate(false);
        control->setChecked(std::get<bool>(*currentValue_));
        return;
    }

    if (auto* control = qobject_cast<QComboBox*>(editor_)) {
        if (descriptor_.kind == CameraFeatureKind::Integer) {
            const auto number = std::get<std::int64_t>(*currentValue_);
            control->setCurrentIndex(control->findData(
                QVariant::fromValue<qlonglong>(
                    static_cast<qlonglong>(number))));
        } else {
            const auto& symbol =
                std::get<FeatureEnumeration>(*currentValue_).symbol;
            control->setCurrentIndex(control->findData(
                QString::fromStdString(symbol)));
        }
        return;
    }

    if (auto* control = qobject_cast<QDoubleSpinBox*>(editor_)) {
        control->setValue(std::get<double>(*currentValue_));
        return;
    }

    if (auto* control = qobject_cast<QLineEdit*>(editor_)) {
        switch (descriptor_.kind) {
        case CameraFeatureKind::Integer:
            control->setText(QString::number(static_cast<qlonglong>(
                std::get<std::int64_t>(*currentValue_))));
            break;
        case CameraFeatureKind::IntegerVector: {
            QStringList values;
            const auto& vector =
                std::get<std::vector<std::int64_t>>(*currentValue_);
            values.reserve(static_cast<qsizetype>(vector.size()));
            for (const auto number : vector) {
                values.push_back(QString::number(static_cast<qlonglong>(number)));
            }
            control->setText(values.join(QStringLiteral(", ")));
            break;
        }
        case CameraFeatureKind::FloatingPoint:
            control->setText(QString::number(
                std::get<double>(*currentValue_), 'g', 17));
            break;
        case CameraFeatureKind::String:
            control->setText(QString::fromStdString(
                std::get<std::string>(*currentValue_)));
            break;
        default:
            break;
        }
    }
}

std::optional<CameraFeatureValue>
CameraFeatureEditor::parseEditorValue(std::string& error) const {
    if (!editor_) {
        error = "No writable feature editor is available.";
        return std::nullopt;
    }

    if (auto* control = qobject_cast<QCheckBox*>(editor_)) {
        if (control->checkState() == Qt::PartiallyChecked) {
            error = "Choose On or Off before submitting the feature.";
            return std::nullopt;
        }
        return CameraFeatureValue{control->isChecked()};
    }

    if (auto* control = qobject_cast<QComboBox*>(editor_)) {
        if (control->currentIndex() < 0) {
            error = "Select a value before submitting the feature.";
            return std::nullopt;
        }
        if (descriptor_.kind == CameraFeatureKind::Integer) {
            bool ok = false;
            const qlonglong number =
                control->currentData().toLongLong(&ok);
            if (!ok) {
                error = "The selected integer value is invalid.";
                return std::nullopt;
            }
            return CameraFeatureValue{
                static_cast<std::int64_t>(number)};
        }
        return CameraFeatureValue{FeatureEnumeration{
            control->currentData().toString().toStdString()}};
    }

    if (auto* control = qobject_cast<QDoubleSpinBox*>(editor_)) {
        return CameraFeatureValue{control->value()};
    }

    if (auto* control = qobject_cast<QLineEdit*>(editor_)) {
        switch (descriptor_.kind) {
        case CameraFeatureKind::Integer: {
            const auto number = parseInteger(control->text());
            if (!number) {
                error = "Enter a valid base-10 signed 64-bit integer.";
                return std::nullopt;
            }
            return CameraFeatureValue{*number};
        }
        case CameraFeatureKind::IntegerVector: {
            const auto parts = control->text().split(
                QLatin1Char(','), Qt::KeepEmptyParts);
            std::vector<std::int64_t> values;
            values.reserve(static_cast<std::size_t>(parts.size()));
            for (const auto& part : parts) {
                const auto number = parseInteger(part);
                if (!number) {
                    error =
                        "Enter comma-separated base-10 signed 64-bit integers.";
                    return std::nullopt;
                }
                values.push_back(*number);
            }
            return CameraFeatureValue{std::move(values)};
        }
        case CameraFeatureKind::FloatingPoint: {
            const auto number = parseFloatingPoint(control->text());
            if (!number) {
                error = "Enter a finite floating-point value.";
                return std::nullopt;
            }
            return CameraFeatureValue{*number};
        }
        case CameraFeatureKind::String: {
            const QByteArray bytes = control->text().toUtf8();
            return CameraFeatureValue{
                std::string(bytes.constData(),
                            static_cast<std::size_t>(bytes.size()))};
        }
        default:
            break;
        }
    }

    error = "The current feature editor has an unsupported value type.";
    return std::nullopt;
}

void CameraFeatureEditor::submitEditedValue() {
    if (!canWrite()) {
        return;
    }

    std::string parseError;
    const auto value = parseEditorValue(parseError);
    if (!value) {
        errorMessage_ = std::move(parseError);
        updateControls();
        return;
    }

    const auto validation = validateFeatureWrite(descriptor_, *value);
    if (const auto* error =
            std::get_if<CameraFeatureAccessError>(&validation)) {
        errorMessage_ = error->message;
        updateControls();
        return;
    }

    errorMessage_.reset();
    writeRequestedHandler_(descriptor_.name, *value);
    updateControls();
}

} // namespace camera::qtwidgets
