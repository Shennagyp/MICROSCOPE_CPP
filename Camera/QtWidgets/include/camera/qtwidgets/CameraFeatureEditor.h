#pragma once

#include "camera/CameraFeatureAccess.h"

#include <QWidget>

#include <functional>
#include <optional>
#include <string>
#include <utility>

class QLabel;
class QStackedWidget;
class QWidget;

namespace camera::qtwidgets {

// Backend-neutral editor for one descriptor-backed camera feature.
//
// The host owns discovery, read/write scheduling, pending state and readback.
// This widget never calls a camera or assumes an initial/default value is the
// device's current value. A writable editor is revealed only after a valid
// current readback has been supplied by the host.
class CameraFeatureEditor final : public QWidget {
public:
    using WriteRequestedHandler = std::function<void(
        const std::string& featureName,
        const CameraFeatureValue& requestedValue)>;

    explicit CameraFeatureEditor(
        CameraFeatureDescriptor descriptor,
        QWidget* parent = nullptr);

    void setDescriptor(CameraFeatureDescriptor descriptor);

    // Returns false when the readback type/value is inconsistent with the
    // descriptor. The value is still retained for diagnostic display, but
    // editing remains disabled until a valid readback arrives.
    [[nodiscard]] bool setCurrentValue(
        std::optional<CameraFeatureValue> value);

    void setPending(bool pending);
    void setErrorMessage(std::optional<std::string> message);
    void setWriteRequestedHandler(WriteRequestedHandler handler);

    // Allows a host to provide display labels while preserving the backend's
    // canonical enumeration symbols as the submitted values.
    using EnumerationDisplayNameProvider =
        std::function<std::string(const std::string& symbol)>;
    void setEnumerationDisplayNameProvider(
        EnumerationDisplayNameProvider provider);

    [[nodiscard]] const CameraFeatureDescriptor& descriptor() const noexcept;
    [[nodiscard]] bool hasCurrentValue() const noexcept;
    [[nodiscard]] bool canWrite() const noexcept;
    [[nodiscard]] QWidget* editorWidget() const noexcept;

private:
    void rebuildEditor();
    void updateControls();
    void applyCurrentValue();
    void submitEditedValue();

    [[nodiscard]] std::optional<CameraFeatureValue> parseEditorValue(
        std::string& error) const;

    CameraFeatureDescriptor descriptor_;
    std::optional<CameraFeatureValue> currentValue_;
    bool currentValueValid_ = false;
    bool pending_ = false;
    std::optional<std::string> errorMessage_;
    WriteRequestedHandler writeRequestedHandler_;
    EnumerationDisplayNameProvider enumerationDisplayNameProvider_;

    QStackedWidget* valueStack_ = nullptr;
    QLabel* placeholder_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QWidget* editor_ = nullptr;
};

} // namespace camera::qtwidgets
