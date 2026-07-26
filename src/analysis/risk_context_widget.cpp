#include "analysis/risk_context_widget.hpp"

#include "analysis/risk_context_analyzer.hpp"
#include "data/historical_data_store.hpp"
#include "data/market_data_quality.hpp"
#include "fundamentals/fundamental_store.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QAbstractItemView>
#include <QDate>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <ranges>
#include <utility>

namespace tvchart {
namespace {

[[nodiscard]] QString optionalPercent(
    const std::optional<double>& value) {
    return value
               ? QStringLiteral("%1%")
                     .arg(QLocale::system().toString(*value, 'f', 2))
               : QStringLiteral("—");
}

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

void populateEvidence(
    QTableWidget& table,
    const std::vector<RiskEvidence>& evidence,
    const RiskEvidenceKind kind) {
    const auto count = std::ranges::count_if(
        evidence,
        [kind](const RiskEvidence& item) { return item.kind == kind; });
    table.setRowCount(static_cast<int>(count));
    auto row = 0;
    for (const auto& item : evidence) {
        if (item.kind != kind) {
            continue;
        }
        const QStringList values{
            riskCategoryLabel(item.category),
            kind == RiskEvidenceKind::Adverse
                ? QString::number(item.points)
                : QStringLiteral("—"),
            item.title,
            item.observed,
            item.source,
            item.asOfDate.toString(Qt::ISODate),
            item.explanation,
        };
        for (auto column = 0; column < values.size(); ++column) {
            auto* cell = new QTableWidgetItem(values[column]);
            cell->setToolTip(item.explanation);
            table.setItem(row, column, cell);
        }
        ++row;
    }
    table.resizeRowsToContents();
}

} // namespace

RiskContextWidget::RiskContextWidget(
    HistoricalDataStore* historyStore,
    FundamentalStore* fundamentalStore,
    QWidget* parent)
    : QWidget(parent),
      historyStore_(historyStore),
      fundamentalStore_(fundamentalStore) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto* explanation = new QLabel(
        tr("Deterministic 5–20 session observed-risk context. It explains "
           "dated evidence and comparable history; it is not investment "
           "advice, a loss probability, or a buy/sell signal."),
        this);
    explanation->setWordWrap(true);
    root->addWidget(explanation);

    auto* controls = new QHBoxLayout;
    symbolLabel_ = new QLabel(tr("Symbol: —"), this);
    controls->addWidget(symbolLabel_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Benchmark"), this));
    benchmarkInput_ = new QLineEdit(QStringLiteral("SPY"), this);
    benchmarkInput_->setObjectName(QStringLiteral("riskBenchmarkInput"));
    benchmarkInput_->setMaxLength(32);
    benchmarkInput_->setMaximumWidth(120);
    controls->addWidget(benchmarkInput_);
    auto* run = new QPushButton(tr("Analyze cached daily data"), this);
    run->setObjectName(QStringLiteral("riskAnalyzeButton"));
    controls->addWidget(run);
    controls->addStretch();
    root->addLayout(controls);

    headlineLabel_ = new QLabel(tr("Load provider daily history, then analyze."), this);
    headlineLabel_->setObjectName(QStringLiteral("riskHeadlineLabel"));
    headlineLabel_->setWordWrap(true);
    auto headlineFont = headlineLabel_->font();
    headlineFont.setBold(true);
    headlineFont.setPointSize(headlineFont.pointSize() + 2);
    headlineLabel_->setFont(headlineFont);
    root->addWidget(headlineLabel_);

    coverageLabel_ = new QLabel(this);
    coverageLabel_->setWordWrap(true);
    datesLabel_ = new QLabel(this);
    datesLabel_->setWordWrap(true);
    historicalLabel_ = new QLabel(this);
    historicalLabel_->setWordWrap(true);
    warningLabel_ = new QLabel(this);
    warningLabel_->setWordWrap(true);
    warningLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(coverageLabel_);
    root->addWidget(datesLabel_);
    root->addWidget(historicalLabel_);
    root->addWidget(warningLabel_);

    auto* adverseTitle = new QLabel(tr("Adverse evidence"), this);
    auto titleFont = adverseTitle->font();
    titleFont.setBold(true);
    adverseTitle->setFont(titleFont);
    root->addWidget(adverseTitle);
    evidenceTable_ = new QTableWidget(0, 7, this);
    evidenceTable_->setObjectName(QStringLiteral("riskEvidenceTable"));
    evidenceTable_->setHorizontalHeaderLabels(
        {tr("Category"),
         tr("Points"),
         tr("Evidence"),
         tr("Observed"),
         tr("Source"),
         tr("As of"),
         tr("Why it matters")});
    configureTable(*evidenceTable_);
    root->addWidget(evidenceTable_, 1);

    auto* constructiveTitle =
        new QLabel(tr("Constructive counter-evidence"), this);
    constructiveTitle->setFont(titleFont);
    root->addWidget(constructiveTitle);
    constructiveTable_ = new QTableWidget(0, 7, this);
    constructiveTable_->setObjectName(
        QStringLiteral("riskConstructiveTable"));
    constructiveTable_->setHorizontalHeaderLabels(
        {tr("Category"),
         tr("Points"),
         tr("Evidence"),
         tr("Observed"),
         tr("Source"),
         tr("As of"),
         tr("Why it matters")});
    configureTable(*constructiveTable_);
    root->addWidget(constructiveTable_, 1);

    connect(run, &QPushButton::clicked, this, &RiskContextWidget::analyze);
    connect(
        benchmarkInput_,
        &QLineEdit::editingFinished,
        this,
        [this] {
            benchmarkInput_->setText(
                normalizeWatchlistSymbol(benchmarkInput_->text()));
            emit settingsChanged();
        });
}

void RiskContextWidget::setCurrentContext(QString symbol) {
    symbol_ = normalizeWatchlistSymbol(std::move(symbol));
    symbolLabel_->setText(
        symbol_.isEmpty() ? tr("Symbol: —") : tr("Symbol: %1").arg(symbol_));
    clearReport(
        symbol_.isEmpty()
            ? tr("Select a symbol.")
            : tr("Ready to analyze %1 from cached completed daily bars.")
                  .arg(symbol_));
}

void RiskContextWidget::setResearchWorkspace(
    ResearchWorkspace workspace) {
    researchWorkspace_ = std::move(workspace);
}

void RiskContextWidget::restoreSettings(QSettings& settings) {
    auto benchmark = normalizeWatchlistSymbol(
        settings.value(
                    QStringLiteral("riskContext/benchmark"),
                    QStringLiteral("SPY"))
            .toString());
    if (benchmark.isEmpty()) {
        benchmark = QStringLiteral("SPY");
    }
    benchmarkInput_->setText(benchmark);
}

void RiskContextWidget::saveSettings(QSettings& settings) const {
    auto benchmark =
        normalizeWatchlistSymbol(benchmarkInput_->text());
    if (benchmark.isEmpty()) {
        benchmark = QStringLiteral("SPY");
    }
    settings.setValue(
        QStringLiteral("riskContext/benchmark"),
        benchmark);
}

void RiskContextWidget::clearReport(const QString& message) {
    headlineLabel_->setText(message);
    coverageLabel_->clear();
    datesLabel_->clear();
    historicalLabel_->clear();
    warningLabel_->clear();
    evidenceTable_->setRowCount(0);
    constructiveTable_->setRowCount(0);
}

void RiskContextWidget::analyze() {
    if (!historyStore_ || !historyStore_->isOpen() || symbol_.isEmpty()) {
        clearReport(tr("Historical cache is unavailable or no symbol is selected."));
        return;
    }
    auto benchmark =
        normalizeWatchlistSymbol(benchmarkInput_->text());
    if (benchmark.isEmpty()) {
        benchmark = QStringLiteral("SPY");
        benchmarkInput_->setText(benchmark);
    }
    const auto security =
        historyStore_->loadLatestSeries(symbol_, Timeframe::OneDay);
    if (!security.ok()) {
        clearReport(
            tr("No provider daily history for %1. Select 1D, load the symbol "
               "from Yahoo/Twelve Data, then run the analyzer again.")
                .arg(symbol_));
        emit statusMessage(
            tr("Risk analysis needs cached provider daily history for %1.")
                .arg(symbol_));
        return;
    }
    auto benchmarkSeries =
        historyStore_->loadLatestSeries(benchmark, Timeframe::OneDay);

    const auto securityQuality = analyzeMarketDataQuality(
        security.bars,
        Timeframe::OneDay,
        security.bars.size(),
        0,
        0,
        {});
    auto benchmarkQuality = MarketDataQualityReport{};
    Bars benchmarkBars;
    if (benchmarkSeries.ok()) {
        benchmarkBars = benchmarkSeries.bars;
        benchmarkQuality = analyzeMarketDataQuality(
            benchmarkBars,
            Timeframe::OneDay,
            benchmarkBars.size(),
            0,
            0,
            {});
    }

    std::optional<FundamentalCompany> fundamentals;
    if (fundamentalStore_ && fundamentalStore_->isOpen()) {
        const auto stored = fundamentalStore_->loadCompany(symbol_);
        if (stored.ok()) {
            fundamentals = stored.company;
        }
    }

    RiskContextInput input{
        .symbol = symbol_,
        .benchmarkSymbol = benchmark,
        .securityBars = security.bars,
        .benchmarkBars = std::move(benchmarkBars),
        .securityQuality = securityQuality,
        .benchmarkQuality = benchmarkQuality,
        .fundamentals = std::move(fundamentals),
        .events = researchWorkspace_.events,
        .eventCalendarCoverageKnown = false,
        .observationDate = QDate::currentDate(),
        .includeHistoricalValidation = true,
    };
    const auto report = analyzeRiskContext(input);
    if (!report.ok()) {
        clearReport(report.error);
        emit statusMessage(report.error);
        return;
    }

    headlineLabel_->setText(
        tr("%1 · %2/100").arg(riskLevelLabel(report.level)).arg(report.score));
    coverageLabel_->setText(
        tr("Evidence coverage: %1% (%2/100 category weight) · confidence: %3 · "
           "adverse points: %4. Counter-evidence does not cancel adverse points.")
            .arg(
                QLocale::system().toString(
                    report.coveragePercent,
                    'f',
                    0))
            .arg(report.availableWeight)
            .arg(report.confidence)
            .arg(report.adversePoints));
    datesLabel_->setText(
        tr("Security daily bar: %1 · benchmark %2: %3 · security source: %4")
            .arg(
                report.securityAsOfDate.toString(Qt::ISODate),
                report.benchmarkSymbol,
                report.benchmarkAsOfDate.isValid()
                    ? report.benchmarkAsOfDate.toString(Qt::ISODate)
                    : tr("unavailable"),
                security.key.provider));
    const auto& historical = report.historical;
    historicalLabel_->setText(
        tr("Comparable historical setups: %1 · median 5-day: %2 · median "
           "20-day: %3 · negative 20-day outcomes: %4 · median/worst 20-day "
           "drawdown: %5 / %6 · median benchmark-relative 20-day: %7. %8")
            .arg(historical.comparableSetups)
            .arg(optionalPercent(historical.medianForwardReturn5Percent))
            .arg(optionalPercent(historical.medianForwardReturn20Percent))
            .arg(optionalPercent(historical.negativeForwardReturn20Percent))
            .arg(optionalPercent(historical.medianMaximumDrawdown20Percent))
            .arg(optionalPercent(historical.worstMaximumDrawdown20Percent))
            .arg(optionalPercent(
                historical.medianBenchmarkRelativeReturn20Percent))
            .arg(historical.note));

    QStringList notices;
    for (const auto& missing : report.missingInputs) {
        notices.push_back(tr("Missing: %1").arg(missing));
    }
    for (const auto& warning : report.warnings) {
        notices.push_back(tr("Warning: %1").arg(warning));
    }
    if (!benchmarkSeries.ok()) {
        notices.push_back(
            tr("Missing: load %1 on 1D once to add benchmark-regime context.")
                .arg(benchmark));
    }
    warningLabel_->setText(notices.join(QStringLiteral("\n")));
    populateEvidence(
        *evidenceTable_,
        report.evidence,
        RiskEvidenceKind::Adverse);
    populateEvidence(
        *constructiveTable_,
        report.evidence,
        RiskEvidenceKind::Constructive);
    emit statusMessage(
        tr("Risk context updated for %1 through %2.")
            .arg(symbol_, report.securityAsOfDate.toString(Qt::ISODate)));
}

} // namespace tvchart
