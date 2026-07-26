#include "analysis/market_structure_widget.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <utility>

namespace tvchart {
namespace {

void configureTable(QTableWidget& table) {
    table.setEditTriggers(QAbstractItemView::NoEditTriggers);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setSelectionMode(QAbstractItemView::SingleSelection);
    table.setWordWrap(true);
    table.verticalHeader()->setVisible(false);
    table.horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    table.horizontalHeader()->setStretchLastSection(true);
}

[[nodiscard]] QString price(const double value) {
    const auto decimals = std::abs(value) < 1.0 ? 4 : 2;
    return QLocale::system().toString(value, 'f', decimals);
}

[[nodiscard]] QString optionalPrice(
    const std::optional<double>& value) {
    return value ? price(*value) : QStringLiteral("—");
}

[[nodiscard]] QString optionalPercent(
    const std::optional<double>& value) {
    return value
               ? QStringLiteral("%1%")
                     .arg(QLocale::system().toString(*value, 'f', 2))
               : QStringLiteral("—");
}

[[nodiscard]] QString dateTime(const std::int64_t timestamp) {
    return timestamp > 0
               ? QDateTime::fromSecsSinceEpoch(
                     timestamp,
                     QTimeZone::UTC)
                     .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
               : QStringLiteral("—");
}

void setRow(
    QTableWidget& table,
    const int row,
    const QStringList& values,
    const QString& tooltip = {}) {
    for (auto column = 0; column < values.size(); ++column) {
        auto* item = new QTableWidgetItem(values[column]);
        if (!tooltip.isEmpty()) {
            item->setToolTip(tooltip);
        }
        table.setItem(row, column, item);
    }
}

} // namespace

MarketStructureWidget::MarketStructureWidget(QWidget* parent)
    : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto* explanation = new QLabel(
        tr("Confirmed-pivot market structure with ATR-normalized zones, "
           "patterns, completed higher-timeframe confluence, and descriptive "
           "historical outcomes. It does not predict prices or provide "
           "investment advice."),
        this);
    explanation->setWordWrap(true);
    root->addWidget(explanation);

    auto* form = new QFormLayout;
    symbolLabel_ = new QLabel(tr("—"), this);
    form->addRow(tr("Symbol"), symbolLabel_);

    pivotStrengthInput_ = new QSpinBox(this);
    pivotStrengthInput_->setObjectName(
        QStringLiteral("marketStructurePivotStrength"));
    pivotStrengthInput_->setRange(2, 10);
    pivotStrengthInput_->setValue(3);
    pivotStrengthInput_->setKeyboardTracking(false);
    form->addRow(tr("Pivot strength"), pivotStrengthInput_);

    zoneAtrInput_ = new QDoubleSpinBox(this);
    zoneAtrInput_->setObjectName(
        QStringLiteral("marketStructureZoneAtr"));
    zoneAtrInput_->setRange(0.25, 3.0);
    zoneAtrInput_->setDecimals(2);
    zoneAtrInput_->setSingleStep(0.05);
    zoneAtrInput_->setValue(0.75);
    zoneAtrInput_->setKeyboardTracking(false);
    form->addRow(tr("Zone width (ATR)"), zoneAtrInput_);

    minimumTouchesInput_ = new QSpinBox(this);
    minimumTouchesInput_->setObjectName(
        QStringLiteral("marketStructureMinimumTouches"));
    minimumTouchesInput_->setRange(2, 6);
    minimumTouchesInput_->setValue(2);
    minimumTouchesInput_->setKeyboardTracking(false);
    form->addRow(tr("Minimum zone touches"), minimumTouchesInput_);

    historicalValidationInput_ = new QCheckBox(
        tr("Calculate 5/20-bar historical outcomes"),
        this);
    historicalValidationInput_->setObjectName(
        QStringLiteral("marketStructureHistoricalValidation"));
    historicalValidationInput_->setChecked(true);
    form->addRow(QString{}, historicalValidationInput_);
    root->addLayout(form);

    auto* analyzeButton =
        new QPushButton(tr("Analyze current chart"), this);
    analyzeButton->setObjectName(
        QStringLiteral("marketStructureAnalyzeButton"));
    root->addWidget(analyzeButton);

    headlineLabel_ = new QLabel(
        tr("Load at least 30 completed bars."),
        this);
    headlineLabel_->setObjectName(
        QStringLiteral("marketStructureHeadline"));
    headlineLabel_->setWordWrap(true);
    auto headlineFont = headlineLabel_->font();
    headlineFont.setBold(true);
    headlineFont.setPointSize(headlineFont.pointSize() + 1);
    headlineLabel_->setFont(headlineFont);
    root->addWidget(headlineLabel_);

    confluenceLabel_ = new QLabel(this);
    confluenceLabel_->setWordWrap(true);
    root->addWidget(confluenceLabel_);
    warningLabel_ = new QLabel(this);
    warningLabel_->setWordWrap(true);
    warningLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(warningLabel_);

    auto* tabs = new QTabWidget(this);
    zoneTable_ = new QTableWidget(0, 6, tabs);
    zoneTable_->setObjectName(
        QStringLiteral("marketStructureZoneTable"));
    zoneTable_->setHorizontalHeaderLabels(
        {tr("Type"),
         tr("Range"),
         tr("Touches"),
         tr("Distance"),
         tr("State"),
         tr("Detected UTC")});
    configureTable(*zoneTable_);
    tabs->addTab(zoneTable_, tr("Zones"));

    patternTable_ = new QTableWidget(0, 7, tabs);
    patternTable_->setObjectName(
        QStringLiteral("marketStructurePatternTable"));
    patternTable_->setHorizontalHeaderLabels(
        {tr("Pattern"),
         tr("Direction"),
         tr("Status"),
         tr("Target"),
         tr("Invalidation"),
         tr("Detected UTC"),
         tr("Evidence")});
    configureTable(*patternTable_);
    tabs->addTab(patternTable_, tr("Patterns"));

    outcomeTable_ = new QTableWidget(0, 9, tabs);
    outcomeTable_->setObjectName(
        QStringLiteral("marketStructureOutcomeTable"));
    outcomeTable_->setHorizontalHeaderLabels(
        {tr("Pattern"),
         tr("Direction"),
         tr("Samples"),
         tr("Median 5"),
         tr("Median 20"),
         tr("Positive 20"),
         tr("Median MAE 20"),
         tr("Target hits"),
         tr("Invalidations")});
    configureTable(*outcomeTable_);
    tabs->addTab(outcomeTable_, tr("History"));
    root->addWidget(tabs, 1);

    connect(
        analyzeButton,
        &QPushButton::clicked,
        this,
        &MarketStructureWidget::analyze);
    const auto changed = [this] {
        emit settingsChanged();
        analyze();
    };
    connect(
        pivotStrengthInput_,
        &QSpinBox::valueChanged,
        this,
        changed);
    connect(
        zoneAtrInput_,
        &QDoubleSpinBox::valueChanged,
        this,
        changed);
    connect(
        minimumTouchesInput_,
        &QSpinBox::valueChanged,
        this,
        changed);
    connect(
        historicalValidationInput_,
        &QCheckBox::toggled,
        this,
        changed);
}

void MarketStructureWidget::setCurrentContext(
    QString symbol,
    const Timeframe timeframe,
    const Bars& bars) {
    symbol_ = normalizeWatchlistSymbol(std::move(symbol));
    timeframe_ = timeframe;
    bars_ = bars;
    symbolLabel_->setText(
        symbol_.isEmpty() ? tr("—") : symbol_);
    analyze();
}

void MarketStructureWidget::restoreSettings(QSettings& settingsStore) {
    pivotStrengthInput_->setValue(
        settingsStore
            .value(QStringLiteral("marketStructure/pivotStrength"), 3)
            .toInt());
    zoneAtrInput_->setValue(
        settingsStore
            .value(QStringLiteral("marketStructure/zoneAtr"), 0.75)
            .toDouble());
    minimumTouchesInput_->setValue(
        settingsStore
            .value(QStringLiteral("marketStructure/minimumTouches"), 2)
            .toInt());
    historicalValidationInput_->setChecked(
        settingsStore
            .value(
                QStringLiteral("marketStructure/historicalValidation"),
                true)
            .toBool());
}

void MarketStructureWidget::saveSettings(
    QSettings& settingsStore) const {
    settingsStore.setValue(
        QStringLiteral("marketStructure/pivotStrength"),
        pivotStrengthInput_->value());
    settingsStore.setValue(
        QStringLiteral("marketStructure/zoneAtr"),
        zoneAtrInput_->value());
    settingsStore.setValue(
        QStringLiteral("marketStructure/minimumTouches"),
        minimumTouchesInput_->value());
    settingsStore.setValue(
        QStringLiteral("marketStructure/historicalValidation"),
        historicalValidationInput_->isChecked());
}

const MarketStructureReport&
MarketStructureWidget::report() const noexcept {
    return report_;
}

MarketStructureSettings MarketStructureWidget::settings() const {
    return {
        .pivotStrength = pivotStrengthInput_->value(),
        .zoneAtrMultiplier = zoneAtrInput_->value(),
        .minimumZoneTouches = minimumTouchesInput_->value(),
        .maximumLookbackBars = 1'500,
        .maximumPivots = 64,
        .maximumZones = 12,
        .maximumPatterns = 24,
    };
}

void MarketStructureWidget::clearReport(const QString& message) {
    report_ = {
        .symbol = symbol_,
        .timeframe = timeframe_,
        .error = message,
    };
    headlineLabel_->setText(message);
    confluenceLabel_->clear();
    warningLabel_->clear();
    zoneTable_->setRowCount(0);
    patternTable_->setRowCount(0);
    outcomeTable_->setRowCount(0);
    emit reportChanged();
}

void MarketStructureWidget::analyze() {
    if (symbol_.isEmpty()) {
        clearReport(tr("Select a valid symbol."));
        return;
    }
    if (bars_.size() < 30) {
        clearReport(
            tr("At least 30 completed bars are required for %1.")
                .arg(symbol_));
        return;
    }
    report_ = analyzeMarketStructure({
        .symbol = symbol_,
        .bars = bars_,
        .timeframe = timeframe_,
        .settings = settings(),
        .includeHistoricalValidation =
            historicalValidationInput_->isChecked(),
    });
    if (!report_.ok()) {
        clearReport(report_.error);
        return;
    }

    headlineLabel_->setText(
        tr("%1 · %2 pivots · %3 zones · %4 patterns · through %5 UTC")
            .arg(
                structureBiasLabel(report_.bias),
                QString::number(report_.pivots.size()),
                QString::number(report_.zones.size()),
                QString::number(report_.patterns.size()),
                dateTime(report_.asOfTimestamp)));
    confluenceLabel_->setText(
        tr("%1: %2. %3")
            .arg(
                report_.confluence.higherTimeframe,
                report_.confluence.state,
                report_.confluence.explanation));
    warningLabel_->setText(
        QStringList(
            report_.warnings.begin(),
            report_.warnings.end())
            .join(QStringLiteral("\n")));

    zoneTable_->setRowCount(
        static_cast<int>(report_.zones.size()));
    for (std::size_t index = 0;
         index < report_.zones.size();
         ++index) {
        const auto& zone = report_.zones[index];
        setRow(
            *zoneTable_,
            static_cast<int>(index),
            {
                structureZoneTypeLabel(zone.type),
                QStringLiteral("%1 – %2")
                    .arg(price(zone.low), price(zone.high)),
                QString::number(zone.touches),
                QStringLiteral("%1%")
                    .arg(
                        QLocale::system().toString(
                            zone.distanceFromClosePercent,
                            'f',
                            2)),
                zone.broken
                    ? tr("Broken %1")
                          .arg(dateTime(
                              zone.brokenTimestamp.value_or(0)))
                    : tr("Active"),
                dateTime(zone.detectionTimestamp),
            },
            zone.explanation);
    }
    zoneTable_->resizeRowsToContents();

    patternTable_->setRowCount(
        static_cast<int>(report_.patterns.size()));
    for (std::size_t index = 0;
         index < report_.patterns.size();
         ++index) {
        const auto& pattern = report_.patterns[index];
        setRow(
            *patternTable_,
            static_cast<int>(index),
            {
                patternKindLabel(pattern.kind),
                patternDirectionLabel(pattern.direction),
                patternStatusLabel(pattern.status),
                optionalPrice(pattern.targetPrice),
                optionalPrice(pattern.invalidationPrice),
                dateTime(pattern.detectionTimestamp),
                pattern.explanation,
            },
            pattern.explanation);
    }
    patternTable_->resizeRowsToContents();

    outcomeTable_->setRowCount(
        static_cast<int>(report_.historicalOutcomes.size()));
    for (std::size_t index = 0;
         index < report_.historicalOutcomes.size();
         ++index) {
        const auto& outcome = report_.historicalOutcomes[index];
        setRow(
            *outcomeTable_,
            static_cast<int>(index),
            {
                patternKindLabel(outcome.kind),
                patternDirectionLabel(outcome.direction),
                QString::number(outcome.samples),
                optionalPercent(
                    outcome.medianSignedReturn5Percent),
                optionalPercent(
                    outcome.medianSignedReturn20Percent),
                optionalPercent(
                    outcome.positiveSignedReturn20Percent),
                optionalPercent(
                    outcome
                        .medianMaximumAdverseExcursion20Percent),
                QString::number(outcome.targetHits),
                QString::number(outcome.invalidations),
            },
            outcome.note);
    }
    outcomeTable_->resizeRowsToContents();
    emit reportChanged();
    emit statusMessage(
        tr("Market structure updated for %1 through %2 UTC.")
            .arg(symbol_, dateTime(report_.asOfTimestamp)));
}

} // namespace tvchart
