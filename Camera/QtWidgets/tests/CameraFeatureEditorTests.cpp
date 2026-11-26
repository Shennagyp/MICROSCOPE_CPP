#include "camera/qtwidgets/CameraFeatureEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using camera::CameraFeatureDescriptor;
using camera::CameraFeatureKind;
using camera::CameraFeatureValue;
using camera::CameraFloatingPointRange;
using camera::CameraIntegerRange;
using camera::FeatureEnumeration;
using camera::qtwidgets::CameraFeatureEditor;

void fail(const char* message) {
    std::cerr << message << '\n';
}

CameraFeatureDescriptor descriptor(
    std::string name,
    CameraFeatureKind kind) {
    CameraFeatureDescriptor value;
    value.name = std::move(name);
    value.displayName = value.name;
    value.kind = kind;
    value.isAvailable = true;
    value.isReadable = true;
    value.isWritable = true;
    return value;
}

bool finishEdit(QLineEdit* edit) {
    return edit && QMetaObject::invokeMethod(
        edit, "editingFinished", Qt::DirectConnection);
}

bool finishEdit(QDoubleSpinBox* edit) {
    return edit && QMetaObject::invokeMethod(
        edit, "editingFinished", Qt::DirectConnection);
}

bool hasError(CameraFeatureEditor& editor, const QString& expected) {
    const auto* label = editor.findChild<QLabel*>(
        QStringLiteral("cameraFeatureEditorError"));
    // The parent editor is intentionally not shown in this offscreen test.
    // isHidden() reflects the explicit visibility state without requiring
    // a top-level window to be displayed.
    return label && !label->isHidden() && label->text().contains(expected);
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    // Integer editor: no control value is treated as readback until the host
    // explicitly supplies one; writes are validated against range/increment.
    auto integer = descriptor("ExposureTime", CameraFeatureKind::Integer);
    integer.numericRange = CameraIntegerRange{0, 100, 5};
    CameraFeatureEditor integerEditor(integer);
    int integerWrites = 0;
    std::int64_t requestedInteger = -1;
    integerEditor.setWriteRequestedHandler(
        [&](const std::string& name, const CameraFeatureValue& value) {
            if (name == "ExposureTime") {
                ++integerWrites;
                requestedInteger = std::get<std::int64_t>(value);
            }
        });

    auto* integerInput = integerEditor.findChild<QLineEdit*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!integerInput || integerEditor.canWrite() || integerInput->isEnabled()) {
        fail("Integer editor exposed a writable/default value before readback.");
        return EXIT_FAILURE;
    }
    auto* integerPlaceholder = integerEditor.findChild<QLabel*>(
        QStringLiteral("cameraFeatureEditorPlaceholder"));
    if (!integerPlaceholder ||
        !integerPlaceholder->text().contains(QStringLiteral("readback"))) {
        fail("Readable feature did not explain why its editor is unavailable.");
        return EXIT_FAILURE;
    }
    if (!integerEditor.setCurrentValue(CameraFeatureValue{std::int64_t{20}}) ||
        !integerEditor.canWrite() ||
        integerInput->text() != QStringLiteral("20")) {
        fail("Integer editor did not apply a valid host readback.");
        return EXIT_FAILURE;
    }

    integerInput->setText(QStringLiteral("23"));
    if (!finishEdit(integerInput) || integerWrites != 0 ||
        !hasError(integerEditor, QStringLiteral("increment"))) {
        fail("Integer editor accepted a value that violates the increment.");
        return EXIT_FAILURE;
    }

    integerInput->setText(QStringLiteral("25"));
    if (!finishEdit(integerInput) || integerWrites != 1 ||
        requestedInteger != 25) {
        fail("Integer editor did not submit a validated typed write request.");
        return EXIT_FAILURE;
    }
    if (integerEditor.setCurrentValue(CameraFeatureValue{std::string("bad")} ) ||
        integerEditor.canWrite()) {
        fail("Integer editor accepted a readback with the wrong variant type.");
        return EXIT_FAILURE;
    }
    if (!integerEditor.setCurrentValue(CameraFeatureValue{std::int64_t{25}})) {
        fail("Integer editor did not recover after a valid readback.");
        return EXIT_FAILURE;
    }
    integerEditor.setPending(true);
    if (integerEditor.canWrite() || integerInput->isEnabled()) {
        fail("Pending integer write did not disable the editor.");
        return EXIT_FAILURE;
    }
    integerEditor.setPending(false);
    if (!integerEditor.canWrite()) {
        fail("Integer editor did not re-enable after pending cleared.");
        return EXIT_FAILURE;
    }

    // Discrete integer values are choices, not free-form numeric text.
    auto discrete = descriptor("RawOutputBitDepth", CameraFeatureKind::Integer);
    discrete.discreteIntegerValues = {8, 10, 12};
    CameraFeatureEditor discreteEditor(discrete);
    std::optional<std::int64_t> discreteWrite;
    discreteEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            discreteWrite = std::get<std::int64_t>(value);
        });
    if (!discreteEditor.setCurrentValue(CameraFeatureValue{std::int64_t{10}})) {
        fail("Discrete integer editor rejected an advertised current value.");
        return EXIT_FAILURE;
    }
    auto* discreteInput = discreteEditor.findChild<QComboBox*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!discreteInput || discreteInput->count() != 3) {
        fail("Discrete integer editor did not render the declared choices.");
        return EXIT_FAILURE;
    }
    discreteInput->setCurrentIndex(2);
    if (!discreteWrite || *discreteWrite != 12) {
        fail("Discrete integer editor did not submit the selected value.");
        return EXIT_FAILURE;
    }

    // Boolean editor preserves false as a real readback, not an uninitialized
    // checkbox state.
    auto boolean = descriptor("AutoExposure", CameraFeatureKind::Boolean);
    CameraFeatureEditor booleanEditor(boolean);
    std::optional<bool> booleanWrite;
    booleanEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            booleanWrite = std::get<bool>(value);
        });
    auto* booleanInput = booleanEditor.findChild<QCheckBox*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!booleanInput || booleanEditor.canWrite()) {
        fail("Boolean editor became writable before current state was read.");
        return EXIT_FAILURE;
    }
    if (!booleanEditor.setCurrentValue(CameraFeatureValue{false}) ||
        !booleanEditor.canWrite() || booleanInput->isChecked()) {
        fail("Boolean false readback was not represented correctly.");
        return EXIT_FAILURE;
    }
    booleanInput->click();
    if (!booleanWrite || !*booleanWrite) {
        fail("Boolean editor did not emit the requested true value.");
        return EXIT_FAILURE;
    }

    // Enumeration symbols keep their distinct FeatureEnumeration type.
    auto enumeration = descriptor("TriggerMode", CameraFeatureKind::Enumeration);
    enumeration.enumerationValues = {"Off", "Once", "Continuous"};
    CameraFeatureEditor enumEditor(enumeration);
    std::optional<std::string> enumWrite;
    enumEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            enumWrite = std::get<FeatureEnumeration>(value).symbol;
        });
    if (!enumEditor.setCurrentValue(
            CameraFeatureValue{FeatureEnumeration{"Off"}})) {
        fail("Enumeration editor rejected a declared current symbol.");
        return EXIT_FAILURE;
    }
    auto* enumInput = enumEditor.findChild<QComboBox*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!enumInput || enumInput->count() != 3) {
        fail("Enumeration editor did not show its declared symbols.");
        return EXIT_FAILURE;
    }
    enumEditor.setEnumerationDisplayNameProvider(
        [](const std::string& symbol) {
            return symbol == "Off" ? std::string("Disabled") : symbol;
        });
    enumInput = enumEditor.findChild<QComboBox*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!enumInput || enumInput->itemText(0) != QStringLiteral("Disabled") ||
        enumInput->itemData(0).toString() != QStringLiteral("Off") ||
        enumInput->currentData().toString() != QStringLiteral("Off")) {
        fail("Enumeration display labels changed the canonical submitted symbol.");
        return EXIT_FAILURE;
    }
    enumInput->setCurrentIndex(1);
    if (!enumWrite || *enumWrite != "Once") {
        fail("Enumeration editor did not preserve the selected symbol.");
        return EXIT_FAILURE;
    }

    // Integer vectors retain exact cardinality and signed 64-bit values.
    auto vector = descriptor("WhiteBalanceGains", CameraFeatureKind::IntegerVector);
    vector.elementCount = 3;
    CameraFeatureEditor vectorEditor(vector);
    std::optional<std::vector<std::int64_t>> vectorWrite;
    vectorEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            vectorWrite = std::get<std::vector<std::int64_t>>(value);
        });
    if (!vectorEditor.setCurrentValue(
            CameraFeatureValue{std::vector<std::int64_t>{1, 2, 3}})) {
        fail("Integer-vector editor rejected a valid current value.");
        return EXIT_FAILURE;
    }
    auto* vectorInput = vectorEditor.findChild<QLineEdit*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!vectorInput) {
        fail("Integer-vector editor did not create its text input.");
        return EXIT_FAILURE;
    }
    vectorInput->setText(QStringLiteral("4, 5"));
    if (!finishEdit(vectorInput) || vectorWrite ||
        !hasError(vectorEditor, QStringLiteral("length"))) {
        fail("Integer-vector editor did not reject the wrong vector length.");
        return EXIT_FAILURE;
    }
    vectorInput->setText(QStringLiteral("4, 5, 6"));
    if (!finishEdit(vectorInput) || !vectorWrite ||
        *vectorWrite != std::vector<std::int64_t>{4, 5, 6}) {
        fail("Integer-vector editor did not submit the complete vector.");
        return EXIT_FAILURE;
    }

    // Floating-point descriptors retain their typed range and step.
    auto floating = descriptor("Gain", CameraFeatureKind::FloatingPoint);
    floating.numericRange = CameraFloatingPointRange{0.0, 10.0, 0.5};
    CameraFeatureEditor floatingEditor(floating);
    std::optional<double> floatingWrite;
    floatingEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            floatingWrite = std::get<double>(value);
        });
    if (!floatingEditor.setCurrentValue(CameraFeatureValue{1.5})) {
        fail("Floating-point editor rejected a valid current value.");
        return EXIT_FAILURE;
    }
    auto* floatingInput = floatingEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!floatingInput || std::abs(floatingInput->singleStep() - 0.5) > 1e-12) {
        fail("Floating-point editor did not preserve its advertised increment.");
        return EXIT_FAILURE;
    }
    floatingInput->setValue(2.0);
    if (!finishEdit(floatingInput) || !floatingWrite ||
        std::abs(*floatingWrite - 2.0) > 1e-12) {
        fail("Floating-point editor did not submit the typed value.");
        return EXIT_FAILURE;
    }

    // Strings remain strings and use UTF-8 at the common API boundary.
    auto string = descriptor("UserLabel", CameraFeatureKind::String);
    CameraFeatureEditor stringEditor(string);
    std::optional<std::string> stringWrite;
    stringEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            stringWrite = std::get<std::string>(value);
        });
    if (!stringEditor.setCurrentValue(CameraFeatureValue{std::string("camera")})) {
        fail("String editor rejected a valid current value.");
        return EXIT_FAILURE;
    }
    auto* stringInput = stringEditor.findChild<QLineEdit*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!stringInput) {
        fail("String editor did not create its text input.");
        return EXIT_FAILURE;
    }
    stringInput->setText(QStringLiteral("Δ sensor"));
    if (!finishEdit(stringInput) || !stringWrite || *stringWrite != "Δ sensor") {
        fail("String editor did not preserve UTF-8 text.");
        return EXIT_FAILURE;
    }

    // Read-only, unavailable and write-only features are never presented as
    // writable editors.
    auto readOnly = descriptor("SerialNumber", CameraFeatureKind::String);
    readOnly.isWritable = false;
    CameraFeatureEditor readOnlyEditor(readOnly);
    if (!readOnlyEditor.setCurrentValue(
            CameraFeatureValue{std::string("SN-01")}) ||
        readOnlyEditor.editorWidget() ||
        !readOnlyEditor.findChild<QLabel*>(
            QStringLiteral("cameraFeatureEditorPlaceholder"))->text()
             .contains(QStringLiteral("SN-01"))) {
        fail("Read-only feature did not show its current value without an editor.");
        return EXIT_FAILURE;
    }

    auto unavailable = descriptor("OptionalFeature", CameraFeatureKind::Boolean);
    unavailable.isAvailable = false;
    unavailable.disabledReason = "Unsupported by this camera profile.";
    CameraFeatureEditor unavailableEditor(unavailable);
    if (unavailableEditor.editorWidget() ||
        !unavailableEditor.findChild<QLabel*>(
            QStringLiteral("cameraFeatureEditorPlaceholder"))->text()
             .contains(QStringLiteral("Unsupported"))) {
        fail("Unavailable feature was not represented as unavailable.");
        return EXIT_FAILURE;
    }

    auto writeOnly = descriptor("WriteOnlySetting", CameraFeatureKind::Integer);
    writeOnly.isReadable = false;
    writeOnly.discreteIntegerValues = {1, 2, 4};
    CameraFeatureEditor writeOnlyEditor(writeOnly);
    std::optional<std::int64_t> writeOnlyValue;
    writeOnlyEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            writeOnlyValue = std::get<std::int64_t>(value);
        });
    auto* writeOnlyInput = writeOnlyEditor.findChild<QComboBox*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!writeOnlyInput || !writeOnlyEditor.canWrite() ||
        writeOnlyInput->currentIndex() != -1) {
        fail("Write-only discrete feature did not expose an unset selectable input.");
        return EXIT_FAILURE;
    }
    writeOnlyInput->setCurrentIndex(2);
    if (!writeOnlyValue || *writeOnlyValue != 4) {
        fail("Write-only discrete feature did not submit its selected value.");
        return EXIT_FAILURE;
    }

    auto writeOnlyBoolean =
        descriptor("WriteOnlyBoolean", CameraFeatureKind::Boolean);
    writeOnlyBoolean.isReadable = false;
    CameraFeatureEditor writeOnlyBooleanEditor(writeOnlyBoolean);
    std::optional<bool> writeOnlyBooleanValue;
    writeOnlyBooleanEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            writeOnlyBooleanValue = std::get<bool>(value);
        });
    auto* writeOnlyBooleanInput = writeOnlyBooleanEditor.findChild<QCheckBox*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!writeOnlyBooleanInput ||
        writeOnlyBooleanInput->checkState() != Qt::PartiallyChecked ||
        !writeOnlyBooleanEditor.canWrite()) {
        fail("Write-only boolean did not represent its current value as unset.");
        return EXIT_FAILURE;
    }
    writeOnlyBooleanInput->click();
    if (writeOnlyBooleanInput->checkState() != Qt::Checked ||
        !writeOnlyBooleanValue || !*writeOnlyBooleanValue) {
        fail("Write-only boolean did not submit an explicit On state after click.");
        return EXIT_FAILURE;
    }
    writeOnlyBooleanInput->click();
    if (writeOnlyBooleanInput->checkState() != Qt::Unchecked ||
        !writeOnlyBooleanValue || *writeOnlyBooleanValue) {
        fail("Write-only boolean did not submit an explicit Off state after click.");
        return EXIT_FAILURE;
    }

    auto writeOnlyFloat =
        descriptor("WriteOnlyGain", CameraFeatureKind::FloatingPoint);
    writeOnlyFloat.isReadable = false;
    writeOnlyFloat.numericRange = CameraFloatingPointRange{1.0, 8.0, 0.5};
    CameraFeatureEditor writeOnlyFloatEditor(writeOnlyFloat);
    std::optional<double> writeOnlyFloatValue;
    writeOnlyFloatEditor.setWriteRequestedHandler(
        [&](const std::string&, const CameraFeatureValue& value) {
            writeOnlyFloatValue = std::get<double>(value);
        });
    auto* writeOnlyFloatInput = writeOnlyFloatEditor.findChild<QLineEdit*>(
        QStringLiteral("cameraFeatureEditorInput"));
    if (!writeOnlyFloatInput ||
        !writeOnlyFloatInput->text().isEmpty() ||
        !writeOnlyFloatInput->placeholderText().contains(QStringLiteral("1")) ||
        !writeOnlyFloatEditor.canWrite()) {
        fail("Write-only floating-point editor presented a fabricated numeric value.");
        return EXIT_FAILURE;
    }
    writeOnlyFloatInput->setText(QStringLiteral("3.5"));
    if (!finishEdit(writeOnlyFloatInput) ||
        !writeOnlyFloatValue || std::abs(*writeOnlyFloatValue - 3.5) > 1e-12) {
        fail("Write-only floating-point editor did not submit its typed value.");
        return EXIT_FAILURE;
    }

    auto badEnum = descriptor("BadEnum", CameraFeatureKind::Enumeration);
    badEnum.enumerationValues = {"A", "B"};
    CameraFeatureEditor badEnumEditor(badEnum);
    if (badEnumEditor.setCurrentValue(
            CameraFeatureValue{FeatureEnumeration{"C"}}) ||
        badEnumEditor.canWrite()) {
        fail("Enumeration editor accepted an undeclared readback symbol.");
        return EXIT_FAILURE;
    }

    std::cout << "All descriptor-driven camera feature editor tests passed.\n";
    return EXIT_SUCCESS;
}
