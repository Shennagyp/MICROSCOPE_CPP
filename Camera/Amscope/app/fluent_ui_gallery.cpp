#include <FluentQt/FluentQt.h>

#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

void applyRawWidgetPalette(QApplication& application) {
    const auto theme = fluent::FluentElement::currentTheme();
    const auto& colors = fluent::ThemeRegistry::instance().colors(theme);

    QPalette palette = application.palette();
    palette.setColor(QPalette::Window, colors.bgCanvas);
    palette.setColor(QPalette::WindowText, colors.textPrimary);
    palette.setColor(QPalette::Base, colors.bgLayer);
    palette.setColor(QPalette::AlternateBase, colors.bgLayerAlt);
    palette.setColor(QPalette::Text, colors.textPrimary);
    palette.setColor(QPalette::Button, colors.controlDefault);
    palette.setColor(QPalette::ButtonText, colors.textPrimary);
    palette.setColor(QPalette::Highlight, colors.accentDefault);
    palette.setColor(QPalette::HighlightedText, colors.textOnAccent);
    palette.setColor(QPalette::ToolTipBase, colors.bgSolid);
    palette.setColor(QPalette::ToolTipText, colors.textPrimary);
    palette.setColor(QPalette::PlaceholderText, colors.textTertiary);
    application.setPalette(palette);
}

QLabel* makeHeading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSizeF(12.0);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    return label;
}

QLabel* makeDescription(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

fluent::layout::Card* makeCard(
    const QString& title,
    const QString& description,
    QWidget* parent,
    QVBoxLayout*& contentLayout) {
    auto* card = new fluent::layout::Card(parent);
    card->setBorderVisible(true);

    contentLayout = new QVBoxLayout(card);
    contentLayout->setContentsMargins(18, 16, 18, 18);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(makeHeading(title, card));
    contentLayout->addWidget(makeDescription(description, card));
    return card;
}

class FluentGalleryWindow final : public QMainWindow {
public:
    explicit FluentGalleryWindow(QApplication& application)
        : application_(application) {
        setWindowTitle(QStringLiteral("AmScope — Component System Proof"));
        resize(1120, 760);

        auto* page = new QWidget(this);
        page->setAutoFillBackground(true);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(24, 22, 24, 24);
        pageLayout->setSpacing(18);

        auto* topRow = new QHBoxLayout();
        auto* titleBlock = new QVBoxLayout();
        auto* title = new QLabel(QStringLiteral("UI components"), page);
        QFont titleFont = title->font();
        titleFont.setPointSizeF(22.0);
        titleFont.setWeight(QFont::DemiBold);
        title->setFont(titleFont);
        auto* subtitle = makeDescription(
            QStringLiteral("Native C++ / Qt Widgets · FluentQt v1.12.2 · isolated visual proof"),
            page);
        titleBlock->addWidget(title);
        titleBlock->addWidget(subtitle);
        topRow->addLayout(titleBlock, 1);

        auto* themeBlock = new QVBoxLayout();
        themeBlock->addWidget(new QLabel(QStringLiteral("Appearance"), page));
        themeSelector_ = new fluent::basicinput::ComboBox(page);
        themeSelector_->addItem(QStringLiteral("Light"), int(fluent::FluentElement::Light));
        themeSelector_->addItem(QStringLiteral("Dark"), int(fluent::FluentElement::Dark));
        themeSelector_->setCurrentIndex(0);
        themeBlock->addWidget(themeSelector_);
        topRow->addLayout(themeBlock);
        pageLayout->addLayout(topRow);

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(16);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);

        QVBoxLayout* buttonLayout = nullptr;
        auto* buttonCard = makeCard(
            QStringLiteral("Actions"),
            QStringLiteral("Primary, standard and subtle actions with consistent sizing and interaction states."),
            page,
            buttonLayout);
        auto* primaryButton =
            new fluent::basicinput::Button(QStringLiteral("Start acquisition"), buttonCard);
        primaryButton->setFluentStyle(fluent::basicinput::Button::Accent);
        auto* secondaryButton =
            new fluent::basicinput::Button(QStringLiteral("Stop acquisition"), buttonCard);
        secondaryButton->setFluentStyle(fluent::basicinput::Button::Standard);
        auto* subtleButton =
            new fluent::basicinput::Button(QStringLiteral("Refresh devices"), buttonCard);
        subtleButton->setFluentStyle(fluent::basicinput::Button::Subtle);
        buttonLayout->addWidget(primaryButton, 0, Qt::AlignLeft);
        buttonLayout->addWidget(secondaryButton, 0, Qt::AlignLeft);
        buttonLayout->addWidget(subtleButton, 0, Qt::AlignLeft);
        buttonLayout->addStretch(1);

        QVBoxLayout* inputLayout = nullptr;
        auto* inputCard = makeCard(
            QStringLiteral("Input controls"),
            QStringLiteral("Selection, numeric input, discrete values and bounded adjustment."),
            page,
            inputLayout);
        auto* modeLabel = new QLabel(QStringLiteral("Acquisition mode"), inputCard);
        auto* modeSelector = new fluent::basicinput::ComboBox(inputCard);
        modeSelector->addItems({
            QStringLiteral("Processed RGB"),
            QStringLiteral("RAW sensor"),
            QStringLiteral("Triggered")
        });
        auto* exposureLabel = new QLabel(QStringLiteral("Exposure (µs)"), inputCard);
        auto* exposure = new fluent::textfields::NumberBox(inputCard);
        exposure->setRange(1.0, 1000000.0);
        exposure->setSmallChange(100.0);
        exposure->setLargeChange(1000.0);
        exposure->setDisplayPrecision(0);
        exposure->setValue(12500.0);
        inputLayout->addWidget(modeLabel);
        inputLayout->addWidget(modeSelector);
        inputLayout->addWidget(exposureLabel);
        inputLayout->addWidget(exposure);

        QVBoxLayout* selectionLayout = nullptr;
        auto* selectionCard = makeCard(
            QStringLiteral("Boolean and option states"),
            QStringLiteral("Check boxes and switches are distinct interaction patterns; each exposes its actual state."),
            page,
            selectionLayout);
        auto* colorCheck =
            new fluent::basicinput::CheckBox(QStringLiteral("Color processing enabled"), selectionCard);
        colorCheck->setChecked(true);
        auto* autoExposure = new fluent::basicinput::ToggleSwitch(selectionCard);
        autoExposure->setOnContent(QStringLiteral("Automatic exposure"));
        autoExposure->setOffContent(QStringLiteral("Manual exposure"));
        autoExposure->setIsOn(false);
        selectionLayout->addWidget(colorCheck);
        selectionLayout->addWidget(autoExposure);
        selectionLayout->addStretch(1);

        QVBoxLayout* rangeLayout = nullptr;
        auto* rangeCard = makeCard(
            QStringLiteral("Continuous value"),
            QStringLiteral("A slider is paired with an exact numeric readout rather than hiding the current value."),
            page,
            rangeLayout);
        auto* gainRow = new QHBoxLayout();
        auto* gainLabel = new QLabel(QStringLiteral("Gain"), rangeCard);
        auto* gainValue = new QLabel(QStringLiteral("42"), rangeCard);
        gainRow->addWidget(gainLabel);
        gainRow->addStretch(1);
        gainRow->addWidget(gainValue);
        auto* gainSlider = new fluent::basicinput::Slider(Qt::Horizontal, rangeCard);
        gainSlider->setRange(0, 100);
        gainSlider->setValue(42);
        rangeLayout->addLayout(gainRow);
        rangeLayout->addWidget(gainSlider);
        rangeLayout->addStretch(1);

        grid->addWidget(buttonCard, 0, 0);
        grid->addWidget(inputCard, 0, 1);
        grid->addWidget(selectionCard, 1, 0);
        grid->addWidget(rangeCard, 1, 1);
        pageLayout->addLayout(grid, 1);

        status_ = new QLabel(
            QStringLiteral("No camera is connected. This gallery is a UI-only experiment."),
            page);
        status_->setWordWrap(true);
        pageLayout->addWidget(status_);

        connect(themeSelector_,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this,
                [this](int index) {
                    const int value = themeSelector_->itemData(index).toInt();
                    const auto theme = static_cast<fluent::FluentElement::Theme>(value);
                    fluent::FluentElement::setTheme(theme);
                    applyRawWidgetPalette(application_);
                });
        connect(primaryButton, &QPushButton::clicked, this, [this]() {
            status_->setText(QStringLiteral("Primary action clicked — no camera operation is connected."));
        });
        connect(secondaryButton, &QPushButton::clicked, this, [this]() {
            status_->setText(QStringLiteral("Standard action clicked — no camera operation is connected."));
        });
        connect(subtleButton, &QPushButton::clicked, this, [this]() {
            status_->setText(QStringLiteral("Refresh clicked — no camera operation is connected."));
        });
        connect(modeSelector, &QComboBox::currentTextChanged, this, [this](const QString& mode) {
            status_->setText(QStringLiteral("Selected presentation: %1").arg(mode));
        });
        connect(exposure, &fluent::textfields::NumberBox::valueChanged, this, [this](double value) {
            status_->setText(QStringLiteral("Numeric input: %1 µs").arg(value, 0, 'f', 0));
        });
        connect(colorCheck, &QCheckBox::toggled, this, [this](bool checked) {
            status_->setText(checked
                ? QStringLiteral("Color processing option is enabled.")
                : QStringLiteral("Color processing option is disabled."));
        });
        connect(autoExposure, &fluent::basicinput::ToggleSwitch::toggled,
                this, [this](bool enabled) {
                    status_->setText(enabled
                        ? QStringLiteral("Automatic exposure selected.")
                        : QStringLiteral("Manual exposure selected."));
                });
        connect(gainSlider, &QSlider::valueChanged, this, [gainValue](int value) {
            gainValue->setText(QString::number(value));
        });

        setCentralWidget(page);
        applyRawWidgetPalette(application_);
    }

private:
    QApplication& application_;
    fluent::basicinput::ComboBox* themeSelector_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
    fluent::prepareHighDpiApplication();
    QApplication application(argc, argv);
    fluent::initializeResources();
    fluent::ThemeRegistry::instance().resetToDefaults();
    fluent::FluentElement::setTheme(fluent::FluentElement::Light);

    FluentGalleryWindow window(application);
    window.show();
    return application.exec();
}
