#include "fundamentals/fundamental_workbench_widget.hpp"

#include "data/historical_data_store.hpp"
#include "fundamentals/event_impact.hpp"
#include "fundamentals/fundamental_store.hpp"
#include "fundamentals/sec_fundamentals_client.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateEdit>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTime>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <ranges>
#include <set>
#include <utility>

namespace tvchart {
namespace {

constexpr std::array kFundamentalMetrics{
    FundamentalMetric::Revenue,
    FundamentalMetric::GrossProfit,
    FundamentalMetric::OperatingIncome,
    FundamentalMetric::NetIncome,
    FundamentalMetric::DilutedEps,
    FundamentalMetric::OperatingCashFlow,
    FundamentalMetric::CapitalExpenditure,
    FundamentalMetric::Cash,
    FundamentalMetric::TotalDebt,
    FundamentalMetric::Assets,
    FundamentalMetric::Liabilities,
    FundamentalMetric::Equity,
    FundamentalMetric::DilutedShares,
};

constexpr std::array kScreenFields{
    FundamentalScreenField::Price,
    FundamentalScreenField::MarketCapitalization,
    FundamentalScreenField::RevenueTtm,
    FundamentalScreenField::RevenueGrowthYoY,
    FundamentalScreenField::EpsTtm,
    FundamentalScreenField::EpsGrowthYoY,
    FundamentalScreenField::FreeCashFlowTtm,
    FundamentalScreenField::GrossMargin,
    FundamentalScreenField::OperatingMargin,
    FundamentalScreenField::NetMargin,
    FundamentalScreenField::DebtToEquity,
    FundamentalScreenField::ReturnOnEquity,
    FundamentalScreenField::PriceToEarnings,
    FundamentalScreenField::PriceToSales,
    FundamentalScreenField::PriceToBook,
    FundamentalScreenField::PriceToFreeCashFlow,
    FundamentalScreenField::Rsi14,
    FundamentalScreenField::DistanceFromSma50,
    FundamentalScreenField::Volatility20,
    FundamentalScreenField::DaysToEarnings,
    FundamentalScreenField::AnalystTargetUpside,
};

[[nodiscard]] QString displayNumber(
    const std::optional<double> value,
    const bool percent = false) {
    if (!value || !std::isfinite(*value)) {
        return QStringLiteral("—");
    }
    const auto magnitude = std::abs(*value);
    QString result;
    if (!percent && magnitude >= 1.0e12) {
        result = QStringLiteral("%1T").arg(*value / 1.0e12, 0, 'f', 2);
    } else if (!percent && magnitude >= 1.0e9) {
        result = QStringLiteral("%1B").arg(*value / 1.0e9, 0, 'f', 2);
    } else if (!percent && magnitude >= 1.0e6) {
        result = QStringLiteral("%1M").arg(*value / 1.0e6, 0, 'f', 2);
    } else {
        result = QString::number(*value, 'f', magnitude < 10.0 ? 3 : 2);
    }
    return percent ? result + QStringLiteral("%") : result;
}

[[nodiscard]] QString displayPoint(
    const std::map<FundamentalMetric, FundamentalSeriesPoint>& values,
    const FundamentalMetric metric) {
    const auto found = values.find(metric);
    if (found == values.end()) {
        return QStringLiteral("—");
    }
    return QStringLiteral("%1 %2%3")
        .arg(
            displayNumber(found->second.value),
            found->second.unit,
            found->second.derived
                ? QStringLiteral(" *")
                : QString{});
}

[[nodiscard]] QTableWidgetItem* item(
    const QString& text,
    const QString& tooltip = {}) {
    auto* result = new QTableWidgetItem(text);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    if (!tooltip.isEmpty()) {
        result->setToolTip(tooltip);
    }
    return result;
}

[[nodiscard]] std::optional<double> screenValue(
    const FundamentalScreenRow& row,
    const FundamentalScreenField field) {
    const auto found = row.values.find(field);
    return found == row.values.end() ? std::nullopt : found->second;
}

[[nodiscard]] FundamentalPeriodMode selectedPeriod(
    const QComboBox* selector) {
    return static_cast<FundamentalPeriodMode>(
        selector->currentData().toInt());
}

} // namespace

FundamentalGraphWidget::FundamentalGraphWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(220);
}

void FundamentalGraphWidget::setSeries(
    QString title,
    std::vector<FundamentalSeriesPoint> series) {
    title_ = std::move(title);
    series_ = std::move(series);
    update();
}

void FundamentalGraphWidget::paintEvent(QPaintEvent* event) {
    static_cast<void>(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    painter.setPen(palette().text().color());
    painter.drawText(
        QRect(10, 6, width() - 20, 24),
        Qt::AlignLeft | Qt::AlignVCenter,
        title_);
    if (series_.empty()) {
        painter.drawText(
            rect().adjusted(10, 35, -10, -10),
            Qt::AlignCenter,
            tr("No compatible point-in-time series"));
        return;
    }
    const QRectF plot(
        62.0,
        38.0,
        std::max(10.0, width() - 78.0),
        std::max(10.0, height() - 76.0));
    auto minimum = series_.front().value;
    auto maximum = series_.front().value;
    for (const auto& point : series_) {
        minimum = std::min(minimum, point.value);
        maximum = std::max(maximum, point.value);
    }
    if (std::abs(maximum - minimum) < 1.0e-12) {
        minimum -= std::max(1.0, std::abs(minimum) * 0.05);
        maximum += std::max(1.0, std::abs(maximum) * 0.05);
    }
    const auto range = maximum - minimum;
    painter.setPen(QPen(palette().mid().color(), 1));
    painter.drawRect(plot);
    painter.drawText(
        QRectF(2, plot.top() - 8, 56, 18),
        Qt::AlignRight | Qt::AlignVCenter,
        displayNumber(maximum));
    painter.drawText(
        QRectF(2, plot.bottom() - 9, 56, 18),
        Qt::AlignRight | Qt::AlignVCenter,
        displayNumber(minimum));
    painter.drawText(
        QRectF(plot.left(), plot.bottom() + 5, 110, 20),
        Qt::AlignLeft,
        series_.front().periodEnd.toString(QStringLiteral("yyyy-MM")));
    painter.drawText(
        QRectF(plot.right() - 110, plot.bottom() + 5, 110, 20),
        Qt::AlignRight,
        series_.back().periodEnd.toString(QStringLiteral("yyyy-MM")));
    QPainterPath path;
    for (std::size_t index = 0; index < series_.size(); ++index) {
        const auto x =
            plot.left() +
            (series_.size() == 1
                 ? plot.width() / 2.0
                 : plot.width() *
                       static_cast<double>(index) /
                       static_cast<double>(series_.size() - 1));
        const auto y =
            plot.bottom() -
            (series_[index].value - minimum) /
                range * plot.height();
        if (index == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    painter.setPen(QPen(QColor(QStringLiteral("#2962ff")), 2));
    painter.drawPath(path);
    painter.setBrush(QColor(QStringLiteral("#2962ff")));
    for (std::size_t index = 0; index < series_.size(); ++index) {
        const auto x =
            plot.left() +
            (series_.size() == 1
                 ? plot.width() / 2.0
                 : plot.width() *
                       static_cast<double>(index) /
                       static_cast<double>(series_.size() - 1));
        const auto y =
            plot.bottom() -
            (series_[index].value - minimum) /
                range * plot.height();
        painter.drawEllipse(QPointF(x, y), 3.0, 3.0);
    }
}

FundamentalWorkbenchWidget::FundamentalWorkbenchWidget(
    FundamentalStore* store,
    HistoricalDataStore* historyStore,
    const bool onlineDataEnabled,
    QWidget* parent)
    : QWidget(parent),
      store_(store),
      historyStore_(historyStore),
      client_(new SecFundamentalsClient(this)),
      onlineDataEnabled_(onlineDataEnabled) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs);

    auto* summary = new QWidget(tabs);
    auto* summaryLayout = new QVBoxLayout(summary);
    auto* summaryControls = new QGridLayout;
    identityLabel_ = new QLabel(tr("No symbol selected"), summary);
    identityLabel_->setWordWrap(true);
    pointInTimeLabel_ = new QLabel(summary);
    pointInTimeLabel_->setWordWrap(true);
    asOfDateInput_ = new QDateEdit(QDate::currentDate(), summary);
    asOfDateInput_->setCalendarPopup(true);
    asOfDateInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    asOfDateInput_->setMaximumDate(QDate::currentDate());
    refreshButton_ =
        new QPushButton(tr("Refresh SEC CompanyFacts"), summary);
    refreshButton_->setEnabled(onlineDataEnabled_);
    refreshButton_->setToolTip(
        tr("Uses the official SEC CompanyFacts API and the contact-bearing "
           "SEC_USER_AGENT environment value."));
    summaryControls->addWidget(identityLabel_, 0, 0, 1, 3);
    summaryControls->addWidget(new QLabel(tr("As-of date"), summary), 1, 0);
    summaryControls->addWidget(asOfDateInput_, 1, 1);
    summaryControls->addWidget(refreshButton_, 1, 2);
    summaryControls->addWidget(pointInTimeLabel_, 2, 0, 1, 3);
    summaryLayout->addLayout(summaryControls);
    summaryWarningLabel_ = new QLabel(summary);
    summaryWarningLabel_->setWordWrap(true);
    summaryLayout->addWidget(summaryWarningLabel_);
    summaryTable_ = new QTableWidget(0, 5, summary);
    summaryTable_->setHorizontalHeaderLabels({
        tr("Metric"),
        tr("Annual"),
        tr("Latest quarter"),
        tr("TTM"),
        tr("Latest public filing"),
    });
    summaryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    summaryTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    summaryTable_->horizontalHeader()->setStretchLastSection(true);
    summaryLayout->addWidget(summaryTable_, 1);
    derivedTable_ = new QTableWidget(0, 2, summary);
    derivedTable_->setHorizontalHeaderLabels({
        tr("Derived metric"),
        tr("Value"),
    });
    derivedTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    derivedTable_->horizontalHeader()->setStretchLastSection(true);
    derivedTable_->setMaximumHeight(250);
    summaryLayout->addWidget(derivedTable_);
    tabs->addTab(summary, tr("Fundamentals"));

    auto* graphPanel = new QWidget(tabs);
    auto* graphLayout = new QVBoxLayout(graphPanel);
    auto* graphControls = new QGridLayout;
    graphMetricSelector_ = new QComboBox(graphPanel);
    for (const auto metric : kFundamentalMetrics) {
        graphMetricSelector_->addItem(
            fundamentalMetricLabel(metric),
            static_cast<int>(metric));
    }
    graphPeriodSelector_ = new QComboBox(graphPanel);
    for (const auto mode : {
             FundamentalPeriodMode::Annual,
             FundamentalPeriodMode::Quarterly,
             FundamentalPeriodMode::TrailingTwelveMonths,
         }) {
        graphPeriodSelector_->addItem(
            fundamentalPeriodModeLabel(mode),
            static_cast<int>(mode));
    }
    graphControls->addWidget(new QLabel(tr("Metric"), graphPanel), 0, 0);
    graphControls->addWidget(graphMetricSelector_, 0, 1);
    graphControls->addWidget(new QLabel(tr("Period"), graphPanel), 0, 2);
    graphControls->addWidget(graphPeriodSelector_, 0, 3);
    graphLayout->addLayout(graphControls);
    graph_ = new FundamentalGraphWidget(graphPanel);
    graphLayout->addWidget(graph_);
    graphDataTable_ = new QTableWidget(0, 5, graphPanel);
    graphDataTable_->setHorizontalHeaderLabels({
        tr("Period end"),
        tr("Filed"),
        tr("Value"),
        tr("Unit"),
        tr("Source"),
    });
    graphDataTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    graphDataTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    graphDataTable_->horizontalHeader()->setStretchLastSection(true);
    graphLayout->addWidget(graphDataTable_);
    graphLayout->addWidget(new QLabel(
        tr("Cached-company peer comparison at the same as-of date"),
        graphPanel));
    peerTable_ = new QTableWidget(0, 4, graphPanel);
    peerTable_->setHorizontalHeaderLabels({
        tr("Symbol"),
        tr("Latest value"),
        tr("Period end"),
        tr("Filed"),
    });
    peerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    peerTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    peerTable_->horizontalHeader()->setStretchLastSection(true);
    graphLayout->addWidget(peerTable_);
    tabs->addTab(graphPanel, tr("Graph & peers"));

    auto* valuation = new QWidget(tabs);
    auto* valuationLayout = new QVBoxLayout(valuation);
    auto* valuationInputs = new QFormLayout;
    growthInput_ = new QDoubleSpinBox(valuation);
    growthInput_->setRange(-50.0, 100.0);
    growthInput_->setSuffix(QStringLiteral(" %"));
    growthInput_->setValue(5.0);
    discountInput_ = new QDoubleSpinBox(valuation);
    discountInput_->setRange(0.1, 100.0);
    discountInput_->setSuffix(QStringLiteral(" %"));
    discountInput_->setValue(10.0);
    terminalInput_ = new QDoubleSpinBox(valuation);
    terminalInput_->setRange(-5.0, 9.0);
    terminalInput_->setSuffix(QStringLiteral(" %"));
    terminalInput_->setValue(2.5);
    forecastYearsInput_ = new QSpinBox(valuation);
    forecastYearsInput_->setRange(1, 20);
    forecastYearsInput_->setValue(5);
    valuationInputs->addRow(tr("FCF growth"), growthInput_);
    valuationInputs->addRow(tr("Discount rate"), discountInput_);
    valuationInputs->addRow(tr("Terminal growth"), terminalInput_);
    valuationInputs->addRow(tr("Forecast years"), forecastYearsInput_);
    valuationLayout->addLayout(valuationInputs);
    auto* valuationExplanation = new QLabel(
        tr("Scenario valuation uses reported TTM free cash flow, mapped "
           "long-term debt, cash, and diluted shares. Inputs are yours; this "
           "is not an analyst target or recommendation."),
        valuation);
    valuationExplanation->setWordWrap(true);
    valuationLayout->addWidget(valuationExplanation);
    valuationSummaryLabel_ = new QLabel(valuation);
    valuationSummaryLabel_->setWordWrap(true);
    valuationLayout->addWidget(valuationSummaryLabel_);
    sensitivityTable_ = new QTableWidget(3, 3, valuation);
    sensitivityTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sensitivityTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    sensitivityTable_->verticalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    valuationLayout->addWidget(sensitivityTable_);
    valuationLayout->addStretch();
    tabs->addTab(valuation, tr("Valuation"));

    auto* screener = new QWidget(tabs);
    auto* screenerLayout = new QVBoxLayout(screener);
    auto* conditionGrid = new QGridLayout;
    conditionGrid->addWidget(new QLabel(tr("Use"), screener), 0, 0);
    conditionGrid->addWidget(new QLabel(tr("Field"), screener), 0, 1);
    conditionGrid->addWidget(new QLabel(tr("Rule"), screener), 0, 2);
    conditionGrid->addWidget(new QLabel(tr("Threshold"), screener), 0, 3);
    for (auto row = 0; row < 4; ++row) {
        ScreenConditionControls controls;
        controls.enabled = new QCheckBox(screener);
        controls.field = new QComboBox(screener);
        for (const auto field : kScreenFields) {
            controls.field->addItem(
                fundamentalScreenFieldLabel(field),
                static_cast<int>(field));
        }
        controls.comparison = new QComboBox(screener);
        controls.comparison->addItem(
            fundamentalScreenOperatorLabel(
                FundamentalScreenOperator::GreaterThanOrEqual),
            static_cast<int>(
                FundamentalScreenOperator::GreaterThanOrEqual));
        controls.comparison->addItem(
            fundamentalScreenOperatorLabel(
                FundamentalScreenOperator::LessThanOrEqual),
            static_cast<int>(
                FundamentalScreenOperator::LessThanOrEqual));
        controls.threshold = new QDoubleSpinBox(screener);
        controls.threshold->setDecimals(3);
        controls.threshold->setRange(-1.0e12, 1.0e12);
        conditionGrid->addWidget(controls.enabled, row + 1, 0);
        conditionGrid->addWidget(controls.field, row + 1, 1);
        conditionGrid->addWidget(controls.comparison, row + 1, 2);
        conditionGrid->addWidget(controls.threshold, row + 1, 3);
        screenConditions_.push_back(controls);
    }
    screenConditions_[0].enabled->setChecked(true);
    screenConditions_[0].field->setCurrentIndex(
        screenConditions_[0].field->findData(
            static_cast<int>(
                FundamentalScreenField::RevenueGrowthYoY)));
    screenConditions_[0].threshold->setValue(0.0);
    screenerLayout->addLayout(conditionGrid);
    auto* screenControls = new QGridLayout;
    screenSortSelector_ = new QComboBox(screener);
    for (const auto field : kScreenFields) {
        screenSortSelector_->addItem(
            fundamentalScreenFieldLabel(field),
            static_cast<int>(field));
    }
    screenSortSelector_->setCurrentIndex(
        screenSortSelector_->findData(
            static_cast<int>(
                FundamentalScreenField::RevenueGrowthYoY)));
    screenDescendingInput_ =
        new QCheckBox(tr("Descending"), screener);
    screenDescendingInput_->setChecked(true);
    screenAlertEnabled_ = new QCheckBox(
        tr("Notify once per matching symbol/day while app is running"),
        screener);
    filingAlertEnabled_ = new QCheckBox(
        tr("Notify when SEC refresh finds a newer filing accession"),
        screener);
    auto* runScreenButton =
        new QPushButton(tr("Run local screen"), screener);
    screenControls->addWidget(new QLabel(tr("Sort"), screener), 0, 0);
    screenControls->addWidget(screenSortSelector_, 0, 1);
    screenControls->addWidget(screenDescendingInput_, 0, 2);
    screenControls->addWidget(runScreenButton, 0, 3);
    screenControls->addWidget(screenAlertEnabled_, 1, 0, 1, 4);
    screenControls->addWidget(filingAlertEnabled_, 2, 0, 1, 4);
    screenerLayout->addLayout(screenControls);
    screenCoverageLabel_ = new QLabel(screener);
    screenCoverageLabel_->setWordWrap(true);
    screenerLayout->addWidget(screenCoverageLabel_);
    screenResultsTable_ = new QTableWidget(0, 10, screener);
    screenResultsTable_->setHorizontalHeaderLabels({
        tr("Symbol"),
        tr("Match"),
        tr("Sort value"),
        tr("Price"),
        tr("Revenue growth"),
        tr("P/E"),
        tr("Operating margin"),
        tr("RSI"),
        tr("Days to earnings"),
        tr("Filed / status"),
    });
    screenResultsTable_->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    screenResultsTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    screenResultsTable_->horizontalHeader()->setStretchLastSection(true);
    screenerLayout->addWidget(screenResultsTable_);
    tabs->addTab(screener, tr("Screener"));

    auto* eventPanel = new QWidget(tabs);
    auto* eventLayout = new QVBoxLayout(eventPanel);
    auto* eventControls = new QGridLayout;
    eventSelector_ = new QComboBox(eventPanel);
    eventAfterCloseInput_ = new QCheckBox(
        tr("After market close: align to the next trading session"),
        eventPanel);
    eventBenchmarkInput_ = new QLineEdit(
        QStringLiteral("SPY"),
        eventPanel);
    eventBenchmarkInput_->setMaximumWidth(120);
    auto* runEventButton =
        new QPushButton(tr("Analyze event"), eventPanel);
    eventControls->addWidget(new QLabel(tr("Event"), eventPanel), 0, 0);
    eventControls->addWidget(eventSelector_, 0, 1);
    eventControls->addWidget(new QLabel(tr("Benchmark"), eventPanel), 0, 2);
    eventControls->addWidget(eventBenchmarkInput_, 0, 3);
    eventControls->addWidget(runEventButton, 0, 4);
    eventControls->addWidget(eventAfterCloseInput_, 1, 0, 1, 5);
    eventLayout->addLayout(eventControls);
    eventSummaryLabel_ = new QLabel(eventPanel);
    eventSummaryLabel_->setWordWrap(true);
    eventLayout->addWidget(eventSummaryLabel_);
    eventResultTable_ = new QTableWidget(0, 4, eventPanel);
    eventResultTable_->setHorizontalHeaderLabels({
        tr("Window"),
        tr("Security return"),
        tr("Benchmark return"),
        tr("Abnormal return"),
    });
    eventResultTable_->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    eventResultTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    eventLayout->addWidget(eventResultTable_);
    tabs->addTab(eventPanel, tr("Event impact"));

    connect(
        refreshButton_,
        &QPushButton::clicked,
        this,
        &FundamentalWorkbenchWidget::refreshSecFacts);
    connect(
        asOfDateInput_,
        &QDateEdit::dateChanged,
        this,
        [this](const QDate&) {
            refreshAllViews();
            emit settingsChanged();
        });
    connect(
        graphMetricSelector_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            refreshGraph();
            refreshPeerTable();
            emit settingsChanged();
        });
    connect(
        graphPeriodSelector_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            refreshGraph();
            refreshPeerTable();
            emit settingsChanged();
        });
    for (auto* input : {
             growthInput_,
             discountInput_,
             terminalInput_,
         }) {
        connect(
            input,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this](double) {
                refreshValuation();
                emit settingsChanged();
            });
    }
    connect(
        forecastYearsInput_,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        [this](int) {
            refreshValuation();
            emit settingsChanged();
        });
    connect(
        runScreenButton,
        &QPushButton::clicked,
        this,
        &FundamentalWorkbenchWidget::runScreen);
    connect(
        runEventButton,
        &QPushButton::clicked,
        this,
        &FundamentalWorkbenchWidget::runEventStudy);
    for (const auto& controls : screenConditions_) {
        connect(
            controls.enabled,
            &QCheckBox::toggled,
            this,
            &FundamentalWorkbenchWidget::settingsChanged);
        connect(
            controls.field,
            &QComboBox::currentIndexChanged,
            this,
            &FundamentalWorkbenchWidget::settingsChanged);
        connect(
            controls.comparison,
            &QComboBox::currentIndexChanged,
            this,
            &FundamentalWorkbenchWidget::settingsChanged);
        connect(
            controls.threshold,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            &FundamentalWorkbenchWidget::settingsChanged);
    }
    connect(
        screenSortSelector_,
        &QComboBox::currentIndexChanged,
        this,
        &FundamentalWorkbenchWidget::settingsChanged);
    connect(
        screenDescendingInput_,
        &QCheckBox::toggled,
        this,
        &FundamentalWorkbenchWidget::settingsChanged);
    connect(
        screenAlertEnabled_,
        &QCheckBox::toggled,
        this,
        &FundamentalWorkbenchWidget::settingsChanged);
    connect(
        filingAlertEnabled_,
        &QCheckBox::toggled,
        this,
        &FundamentalWorkbenchWidget::settingsChanged);
    connect(
        eventBenchmarkInput_,
        &QLineEdit::editingFinished,
        this,
        &FundamentalWorkbenchWidget::settingsChanged);
    connect(
        eventAfterCloseInput_,
        &QCheckBox::toggled,
        this,
        &FundamentalWorkbenchWidget::settingsChanged);
    refreshAllViews();
}

FundamentalWorkbenchWidget::~FundamentalWorkbenchWidget() = default;

void FundamentalWorkbenchWidget::setCurrentContext(
    QString symbol,
    Bars bars) {
    symbol_ = normalizeWatchlistSymbol(std::move(symbol));
    currentBars_ = std::move(bars);
    refreshAllViews();
    evaluateCurrentScreenAlert();
}

void FundamentalWorkbenchWidget::setResearchWorkspace(
    ResearchWorkspace workspace) {
    researchWorkspace_ = std::move(workspace);
    refreshEventChoices();
}

void FundamentalWorkbenchWidget::setUniverseSymbols(QStringList symbols) {
    for (auto& symbol : symbols) {
        symbol = normalizeWatchlistSymbol(std::move(symbol));
    }
    symbols.removeAll(QString{});
    symbols.removeDuplicates();
    std::ranges::sort(symbols);
    universeSymbols_ = std::move(symbols);
}

void FundamentalWorkbenchWidget::restoreSettings(QSettings& settings) {
    asOfDateInput_->setDate(
        settings.value(
                    QStringLiteral("fundamentals/asOfDate"),
                    QDate::currentDate())
            .toDate());
    const auto graphMetric = settings.value(
        QStringLiteral("fundamentals/graphMetric"),
        static_cast<int>(FundamentalMetric::Revenue)).toInt();
    if (const auto index =
            graphMetricSelector_->findData(graphMetric);
        index >= 0) {
        graphMetricSelector_->setCurrentIndex(index);
    }
    const auto graphPeriod = settings.value(
        QStringLiteral("fundamentals/graphPeriod"),
        static_cast<int>(FundamentalPeriodMode::Annual)).toInt();
    if (const auto index =
            graphPeriodSelector_->findData(graphPeriod);
        index >= 0) {
        graphPeriodSelector_->setCurrentIndex(index);
    }
    growthInput_->setValue(
        settings.value(
                    QStringLiteral("fundamentals/dcfGrowth"),
                    5.0)
            .toDouble());
    discountInput_->setValue(
        settings.value(
                    QStringLiteral("fundamentals/dcfDiscount"),
                    10.0)
            .toDouble());
    terminalInput_->setValue(
        settings.value(
                    QStringLiteral("fundamentals/dcfTerminal"),
                    2.5)
            .toDouble());
    forecastYearsInput_->setValue(
        settings.value(
                    QStringLiteral("fundamentals/dcfYears"),
                    5)
            .toInt());
    eventBenchmarkInput_->setText(
        normalizeWatchlistSymbol(
            settings.value(
                        QStringLiteral(
                            "fundamentals/eventBenchmark"),
                        QStringLiteral("SPY"))
                .toString()));
    eventAfterCloseInput_->setChecked(
        settings.value(
                    QStringLiteral(
                        "fundamentals/eventAfterClose"),
                    false)
            .toBool());
    const auto screenPayload =
        settings.value(
                    QStringLiteral("fundamentals/screen"))
            .toByteArray();
    if (!screenPayload.isEmpty()) {
        const auto loaded =
            deserializeFundamentalScreen(screenPayload);
        if (loaded.ok()) {
            applyScreenDefinition(loaded.definition);
        } else {
            emit statusMessage(
                tr("Saved fundamental screen was invalid: %1")
                    .arg(loaded.error));
        }
    }
    screenAlertEnabled_->setChecked(
        settings.value(
                    QStringLiteral(
                        "fundamentals/screenAlertEnabled"),
                    false)
            .toBool());
    filingAlertEnabled_->setChecked(
        settings.value(
                    QStringLiteral(
                        "fundamentals/filingAlertEnabled"),
                    false)
            .toBool());
    refreshAllViews();
}

void FundamentalWorkbenchWidget::saveSettings(
    QSettings& settings) const {
    settings.setValue(
        QStringLiteral("fundamentals/asOfDate"),
        asOfDateInput_->date());
    settings.setValue(
        QStringLiteral("fundamentals/graphMetric"),
        graphMetricSelector_->currentData().toInt());
    settings.setValue(
        QStringLiteral("fundamentals/graphPeriod"),
        graphPeriodSelector_->currentData().toInt());
    settings.setValue(
        QStringLiteral("fundamentals/dcfGrowth"),
        growthInput_->value());
    settings.setValue(
        QStringLiteral("fundamentals/dcfDiscount"),
        discountInput_->value());
    settings.setValue(
        QStringLiteral("fundamentals/dcfTerminal"),
        terminalInput_->value());
    settings.setValue(
        QStringLiteral("fundamentals/dcfYears"),
        forecastYearsInput_->value());
    settings.setValue(
        QStringLiteral("fundamentals/eventBenchmark"),
        normalizeWatchlistSymbol(eventBenchmarkInput_->text()));
    settings.setValue(
        QStringLiteral("fundamentals/eventAfterClose"),
        eventAfterCloseInput_->isChecked());
    settings.setValue(
        QStringLiteral("fundamentals/screenAlertEnabled"),
        screenAlertEnabled_->isChecked());
    settings.setValue(
        QStringLiteral("fundamentals/filingAlertEnabled"),
        filingAlertEnabled_->isChecked());
    const auto payload =
        serializeFundamentalScreen(screenDefinition());
    if (!payload.isEmpty()) {
        settings.setValue(
            QStringLiteral("fundamentals/screen"),
            payload);
    }
}

void FundamentalWorkbenchWidget::refreshSecFacts() {
    if (!onlineDataEnabled_ || symbol_.isEmpty()) {
        emit statusMessage(
            tr("Select a valid symbol before refreshing SEC fundamentals."));
        return;
    }
    refreshButton_->setEnabled(false);
    refreshButton_->setText(tr("Refreshing SEC…"));
    auto previousLatestFiledDate = QDate{};
    auto previousAccessions = std::set<QString>{};
    if (const auto cached = currentCompany(); cached.ok()) {
        for (const auto& fact : cached.company.facts) {
            previousLatestFiledDate =
                std::max(
                    previousLatestFiledDate,
                    fact.filedDate);
            previousAccessions.insert(fact.accession);
        }
    }
    client_->fetch(
        symbol_,
        [this,
         previousLatestFiledDate,
         previousAccessions = std::move(previousAccessions)](
            SecFundamentalsResult result) {
            refreshButton_->setEnabled(onlineDataEnabled_);
            refreshButton_->setText(
                tr("Refresh SEC CompanyFacts"));
            if (!result.ok()) {
                emit statusMessage(
                    tr("SEC fundamentals refresh failed: %1")
                        .arg(result.error));
                return;
            }
            const auto companyName = result.company.name;
            const auto factCount = result.company.facts.size();
            auto newFilingDate = QDate{};
            auto newFilingAccession = QString{};
            for (const auto& fact : result.company.facts) {
                if (previousAccessions.contains(fact.accession) ||
                    (previousLatestFiledDate.isValid() &&
                     fact.filedDate < previousLatestFiledDate)) {
                    continue;
                }
                if (fact.filedDate > newFilingDate ||
                    (fact.filedDate == newFilingDate &&
                     fact.accession > newFilingAccession)) {
                    newFilingDate = fact.filedDate;
                    newFilingAccession = fact.accession;
                }
            }
            const auto storeError =
                store_ ? store_->upsertCompany(result.company)
                       : QStringLiteral(
                             "Fundamental cache is unavailable.");
            if (!storeError.isEmpty()) {
                emit statusMessage(
                    tr("SEC facts were received but not cached: %1")
                        .arg(storeError));
                return;
            }
            refreshAllViews();
            evaluateCurrentScreenAlert();
            if (filingAlertEnabled_->isChecked() &&
                previousLatestFiledDate.isValid() &&
                newFilingDate.isValid()) {
                emit alertTriggered({
                    .alertId =
                        QStringLiteral(
                            "fundamental-filing-%1")
                            .arg(result.company.symbol),
                    .symbol = result.company.symbol,
                    .timestamp =
                        QDateTime(
                            newFilingDate,
                            QTime(0, 0),
                            QTimeZone::UTC)
                            .toSecsSinceEpoch(),
                    .triggeredAtUtc =
                        QDateTime::currentSecsSinceEpoch(),
                    .message =
                        tr("New SEC filing cached · %1 · filed %2 · "
                           "accession %3")
                            .arg(
                                result.company.symbol,
                                newFilingDate.toString(Qt::ISODate),
                                newFilingAccession),
                });
            }
            emit statusMessage(
                tr("Cached %1 point-in-time SEC facts for %2; "
                   "%3 unsupported or malformed fact(s) were skipped.")
                    .arg(
                        static_cast<qulonglong>(factCount))
                    .arg(companyName)
                    .arg(
                        static_cast<qulonglong>(
                            result.rejectedFacts)));
        });
}

StoredFundamentalCompany
FundamentalWorkbenchWidget::currentCompany() const {
    if (!store_ || !store_->isOpen() || symbol_.isEmpty()) {
        return {.error = QStringLiteral(
                    "Fundamental cache or symbol is unavailable.")};
    }
    return store_->loadCompany(symbol_);
}

std::optional<double>
FundamentalWorkbenchWidget::currentPrice() const {
    if (!historyStore_ || !historyStore_->isOpen() ||
        symbol_.isEmpty()) {
        return std::nullopt;
    }
    const auto history = historyStore_->loadLatestSeries(
        symbol_,
        Timeframe::OneDay);
    if (!history.ok()) {
        return std::nullopt;
    }
    const auto stored = currentCompany();
    if (!stored.ok()) {
        return std::nullopt;
    }
    QString reportingCurrency;
    for (const auto mode : {
             FundamentalPeriodMode::TrailingTwelveMonths,
             FundamentalPeriodMode::Annual,
             FundamentalPeriodMode::Quarterly,
         }) {
        const auto revenue = fundamentalSeries(
            stored.company.facts,
            FundamentalMetric::Revenue,
            mode,
            asOfDateInput_->date());
        if (!revenue.empty() &&
            revenue.back().unit.size() == 3) {
            reportingCurrency =
                revenue.back().unit.toUpper();
            break;
        }
    }
    const auto priceCurrency =
        history.metadata.currency.trimmed().toUpper();
    if (reportingCurrency.isEmpty() ||
        priceCurrency.isEmpty() ||
        reportingCurrency != priceCurrency) {
        return std::nullopt;
    }
    for (auto bar = history.bars.rbegin();
         bar != history.bars.rend();
         ++bar) {
        if (QDateTime::fromSecsSinceEpoch(
                bar->timestamp,
                QTimeZone::UTC)
                .date() <= asOfDateInput_->date()) {
            return bar->close;
        }
    }
    return std::nullopt;
}

void FundamentalWorkbenchWidget::refreshAllViews() {
    refreshSummary();
    refreshGraph();
    refreshPeerTable();
    refreshValuation();
    refreshEventChoices();
}

void FundamentalWorkbenchWidget::refreshSummary() {
    summaryTable_->setRowCount(0);
    derivedTable_->setRowCount(0);
    const auto stored = currentCompany();
    if (!stored.ok()) {
        identityLabel_->setText(
            symbol_.isEmpty()
                ? tr("No symbol selected")
                : tr("%1 · no cached SEC CompanyFacts")
                      .arg(symbol_));
        pointInTimeLabel_->setText(
            tr("Refresh requires SEC_USER_AGENT. No value is inferred "
               "from price data."));
        summaryWarningLabel_->setText(stored.error);
        return;
    }
    const auto snapshot = buildFundamentalSnapshot(
        stored.company,
        asOfDateInput_->date(),
        currentPrice());
    identityLabel_->setText(
        tr("%1 · %2 · CIK %3 · %4 cached facts")
            .arg(
                stored.company.symbol,
                stored.company.name,
                stored.company.cik)
            .arg(
                static_cast<qulonglong>(
                    stored.company.facts.size())));
    pointInTimeLabel_->setText(
        tr("Point-in-time boundary: only facts filed on or before %1. "
           "Latest included filing: %2.")
            .arg(
                asOfDateInput_->date().toString(Qt::ISODate),
                snapshot.latestFiledDate.isValid()
                    ? snapshot.latestFiledDate.toString(Qt::ISODate)
                    : QStringLiteral("—")));
    if (!snapshot.ok()) {
        summaryWarningLabel_->setText(snapshot.error);
        return;
    }
    summaryWarningLabel_->setText(
        snapshot.warning.isEmpty()
            ? tr("* denotes a reconstructed value. Select a cell for "
                 "its filing/accession provenance.")
            : snapshot.warning);
    summaryTable_->setRowCount(
        static_cast<int>(kFundamentalMetrics.size()));
    for (std::size_t row = 0;
         row < kFundamentalMetrics.size();
         ++row) {
        const auto metric = kFundamentalMetrics[row];
        summaryTable_->setItem(
            static_cast<int>(row),
            0,
            item(fundamentalMetricLabel(metric)));
        const auto setPoint =
            [&](const int column,
                const std::map<
                    FundamentalMetric,
                    FundamentalSeriesPoint>& values) {
                const auto found = values.find(metric);
                summaryTable_->setItem(
                    static_cast<int>(row),
                    column,
                    item(
                        displayPoint(values, metric),
                        found == values.end()
                            ? QString{}
                            : found->second.provenance));
            };
        setPoint(1, snapshot.latestAnnual);
        setPoint(2, snapshot.latestQuarter);
        setPoint(3, snapshot.trailingTwelveMonths);
        auto latestFiled = QDate{};
        for (const auto* values : {
                 &snapshot.latestAnnual,
                 &snapshot.latestQuarter,
                 &snapshot.trailingTwelveMonths,
             }) {
            const auto found = values->find(metric);
            if (found != values->end()) {
                latestFiled = std::max(
                    latestFiled,
                    found->second.filedDate);
            }
        }
        summaryTable_->setItem(
            static_cast<int>(row),
            4,
            item(
                latestFiled.isValid()
                    ? latestFiled.toString(Qt::ISODate)
                    : QStringLiteral("—")));
    }
    const std::vector<std::pair<QString, std::optional<double>>> derived{
        {tr("Revenue growth YoY"),
         snapshot.derived.revenueGrowthYoYPercent},
        {tr("EPS growth YoY"),
         snapshot.derived.epsGrowthYoYPercent},
        {tr("Gross margin"),
         snapshot.derived.grossMarginPercent},
        {tr("Operating margin"),
         snapshot.derived.operatingMarginPercent},
        {tr("Net margin"),
         snapshot.derived.netMarginPercent},
        {tr("Free cash flow"),
         snapshot.derived.freeCashFlow},
        {tr("FCF margin"),
         snapshot.derived.freeCashFlowMarginPercent},
        {tr("Long-term debt / equity"),
         snapshot.derived.debtToEquity},
        {tr("Return on equity"),
         snapshot.derived.returnOnEquityPercent},
        {tr("Market capitalization"),
         snapshot.derived.marketCapitalization},
        {tr("P/E"), snapshot.derived.priceToEarnings},
        {tr("P/S"), snapshot.derived.priceToSales},
        {tr("P/B"), snapshot.derived.priceToBook},
        {tr("P/FCF"), snapshot.derived.priceToFreeCashFlow},
    };
    derivedTable_->setRowCount(
        static_cast<int>(derived.size()));
    for (std::size_t row = 0; row < derived.size(); ++row) {
        const auto percent =
            derived[row].first.contains(QStringLiteral("growth"), Qt::CaseInsensitive) ||
            derived[row].first.contains(QStringLiteral("margin"), Qt::CaseInsensitive) ||
            derived[row].first.contains(QStringLiteral("return on"), Qt::CaseInsensitive);
        derivedTable_->setItem(
            static_cast<int>(row),
            0,
            item(derived[row].first));
        derivedTable_->setItem(
            static_cast<int>(row),
            1,
            item(displayNumber(derived[row].second, percent)));
    }
}

void FundamentalWorkbenchWidget::refreshGraph() {
    graphDataTable_->setRowCount(0);
    const auto stored = currentCompany();
    if (!stored.ok()) {
        graph_->setSeries(
            symbol_.isEmpty()
                ? tr("Fundamental graph")
                : tr("%1 fundamentals").arg(symbol_),
            {});
        return;
    }
    const auto metric = static_cast<FundamentalMetric>(
        graphMetricSelector_->currentData().toInt());
    const auto period = selectedPeriod(graphPeriodSelector_);
    const auto series = fundamentalSeries(
        stored.company.facts,
        metric,
        period,
        asOfDateInput_->date());
    graph_->setSeries(
        tr("%1 · %2 · %3")
            .arg(
                stored.company.symbol,
                fundamentalMetricLabel(metric),
                fundamentalPeriodModeLabel(period)),
        series);
    graphDataTable_->setRowCount(
        static_cast<int>(series.size()));
    for (std::size_t row = 0; row < series.size(); ++row) {
        graphDataTable_->setItem(
            static_cast<int>(row),
            0,
            item(series[row].periodEnd.toString(Qt::ISODate)));
        graphDataTable_->setItem(
            static_cast<int>(row),
            1,
            item(series[row].filedDate.toString(Qt::ISODate)));
        graphDataTable_->setItem(
            static_cast<int>(row),
            2,
            item(
                displayNumber(series[row].value) +
                (series[row].derived
                     ? QStringLiteral(" *")
                     : QString{})));
        graphDataTable_->setItem(
            static_cast<int>(row),
            3,
            item(series[row].unit));
        graphDataTable_->setItem(
            static_cast<int>(row),
            4,
            item(series[row].provenance, series[row].provenance));
    }
}

void FundamentalWorkbenchWidget::refreshPeerTable() {
    peerTable_->setRowCount(0);
    if (!store_ || !store_->isOpen()) {
        return;
    }
    const auto metric = static_cast<FundamentalMetric>(
        graphMetricSelector_->currentData().toInt());
    const auto period = selectedPeriod(graphPeriodSelector_);
    QString comparisonUnit;
    const auto selectedCompany = currentCompany();
    if (selectedCompany.ok()) {
        const auto selectedSeries = fundamentalSeries(
            selectedCompany.company.facts,
            metric,
            period,
            asOfDateInput_->date());
        if (!selectedSeries.empty()) {
            comparisonUnit = selectedSeries.back().unit;
        }
    }
    if (comparisonUnit.isEmpty()) {
        peerTable_->setToolTip(
            tr("Select a company with a comparable cached metric first."));
        return;
    }
    peerTable_->setToolTip(
        tr("Only companies reporting this metric in %1 are compared. "
           "No foreign-exchange conversion is inferred.")
            .arg(comparisonUnit));
    struct Peer {
        QString symbol;
        FundamentalSeriesPoint point;
    };
    std::vector<Peer> peers;
    for (const auto& summary : store_->availableCompanies()) {
        const auto company = store_->loadCompany(summary.symbol);
        if (!company.ok()) {
            continue;
        }
        const auto series = fundamentalSeries(
            company.company.facts,
            metric,
            period,
            asOfDateInput_->date());
        if (!series.empty() &&
            series.back().unit.compare(
                comparisonUnit,
                Qt::CaseInsensitive) == 0) {
            peers.push_back({
                .symbol = summary.symbol,
                .point = series.back(),
            });
        }
    }
    std::ranges::sort(
        peers,
        [](const Peer& left, const Peer& right) {
            if (left.point.value != right.point.value) {
                return left.point.value > right.point.value;
            }
            return left.symbol < right.symbol;
        });
    peerTable_->setRowCount(static_cast<int>(peers.size()));
    for (std::size_t row = 0; row < peers.size(); ++row) {
        peerTable_->setItem(
            static_cast<int>(row),
            0,
            item(peers[row].symbol));
        peerTable_->setItem(
            static_cast<int>(row),
            1,
            item(
                QStringLiteral("%1 %2")
                    .arg(
                        displayNumber(peers[row].point.value),
                        peers[row].point.unit),
                peers[row].point.provenance));
        peerTable_->setItem(
            static_cast<int>(row),
            2,
            item(peers[row].point.periodEnd.toString(Qt::ISODate)));
        peerTable_->setItem(
            static_cast<int>(row),
            3,
            item(peers[row].point.filedDate.toString(Qt::ISODate)));
    }
}

void FundamentalWorkbenchWidget::refreshValuation() {
    sensitivityTable_->clearContents();
    const auto stored = currentCompany();
    if (!stored.ok()) {
        valuationSummaryLabel_->setText(stored.error);
        return;
    }
    const auto snapshot = buildFundamentalSnapshot(
        stored.company,
        asOfDateInput_->date(),
        currentPrice());
    const DcfAssumptions assumptions{
        .annualGrowthPercent = growthInput_->value(),
        .discountRatePercent = discountInput_->value(),
        .terminalGrowthPercent = terminalInput_->value(),
        .forecastYears = forecastYearsInput_->value(),
    };
    const auto report = calculateDcf(
        snapshot,
        assumptions,
        currentPrice());
    if (!report.ok()) {
        valuationSummaryLabel_->setText(report.error);
        return;
    }
    valuationSummaryLabel_->setText(
        tr("Scenario value %1 %2/share · enterprise value %3 · "
           "mapped net debt %4 · starting FCF %5%6")
            .arg(
                displayNumber(report.valuePerShare),
                report.currency.isEmpty()
                    ? QStringLiteral("reported currency")
                    : report.currency,
                displayNumber(report.enterpriseValue),
                displayNumber(report.netDebt),
                displayNumber(report.startingFreeCashFlow),
                report.impliedAnnualGrowthPercent
                    ? tr(" · market-implied annual FCF growth %1")
                          .arg(
                              displayNumber(
                                  report.impliedAnnualGrowthPercent,
                                  true))
                    : QString{}));
    const std::array changes{-2.0, 0.0, 2.0};
    QStringList horizontal;
    QStringList vertical;
    for (const auto growthChange : changes) {
        horizontal.push_back(
            tr("Growth %1%")
                .arg(
                    assumptions.annualGrowthPercent +
                    growthChange,
                    0,
                    'f',
                    1));
    }
    for (const auto discountChange : changes) {
        vertical.push_back(
            tr("Discount %1%")
                .arg(
                    assumptions.discountRatePercent +
                    discountChange,
                    0,
                    'f',
                    1));
    }
    sensitivityTable_->setHorizontalHeaderLabels(horizontal);
    sensitivityTable_->setVerticalHeaderLabels(vertical);
    for (auto row = 0; row < 3; ++row) {
        for (auto column = 0; column < 3; ++column) {
            auto candidate = assumptions;
            candidate.discountRatePercent += changes[row];
            candidate.annualGrowthPercent += changes[column];
            const auto value = calculateDcf(
                snapshot,
                candidate,
                currentPrice());
            sensitivityTable_->setItem(
                row,
                column,
                item(
                    value.ok()
                        ? displayNumber(value.valuePerShare)
                        : QStringLiteral("—"),
                    value.error));
        }
    }
}

FundamentalScreenDefinition
FundamentalWorkbenchWidget::screenDefinition() const {
    FundamentalScreenDefinition definition{
        .name = QStringLiteral("Local fundamental screen"),
        .sortField =
            static_cast<FundamentalScreenField>(
                screenSortSelector_->currentData().toInt()),
        .sortDescending =
            screenDescendingInput_->isChecked(),
    };
    for (const auto& controls : screenConditions_) {
        if (!controls.enabled->isChecked()) {
            continue;
        }
        definition.conditions.push_back({
            .field =
                static_cast<FundamentalScreenField>(
                    controls.field->currentData().toInt()),
            .comparison =
                static_cast<FundamentalScreenOperator>(
                    controls.comparison->currentData().toInt()),
            .threshold = controls.threshold->value(),
        });
    }
    return definition;
}

void FundamentalWorkbenchWidget::applyScreenDefinition(
    const FundamentalScreenDefinition& definition) {
    for (auto& controls : screenConditions_) {
        controls.enabled->setChecked(false);
    }
    const auto count = std::min(
        screenConditions_.size(),
        definition.conditions.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto& controls = screenConditions_[index];
        const auto& condition = definition.conditions[index];
        controls.enabled->setChecked(true);
        controls.field->setCurrentIndex(
            controls.field->findData(
                static_cast<int>(condition.field)));
        controls.comparison->setCurrentIndex(
            controls.comparison->findData(
                static_cast<int>(condition.comparison)));
        controls.threshold->setValue(condition.threshold);
    }
    screenSortSelector_->setCurrentIndex(
        screenSortSelector_->findData(
            static_cast<int>(definition.sortField)));
    screenDescendingInput_->setChecked(
        definition.sortDescending);
}

void FundamentalWorkbenchWidget::runScreen() {
    screenResultsTable_->setRowCount(0);
    if (!store_ || !store_->isOpen() ||
        !historyStore_ || !historyStore_->isOpen()) {
        screenCoverageLabel_->setText(
            tr("Fundamental or daily price cache is unavailable."));
        return;
    }
    std::set<QString> requested;
    for (const auto& symbol : universeSymbols_) {
        requested.insert(symbol);
    }
    if (!symbol_.isEmpty()) {
        requested.insert(symbol_);
    }
    std::vector<FundamentalScreenInput> inputs;
    auto missingPrice = std::size_t{};
    auto cachedCompanies = std::size_t{};
    for (const auto& summary : store_->availableCompanies()) {
        if (!requested.empty() &&
            !requested.contains(summary.symbol)) {
            continue;
        }
        ++cachedCompanies;
        const auto company = store_->loadCompany(summary.symbol);
        const auto history = historyStore_->loadLatestSeries(
            summary.symbol,
            Timeframe::OneDay);
        if (!company.ok() || !history.ok()) {
            ++missingPrice;
        }
        inputs.push_back({
            .company = company.company,
            .dailyBars = history.bars,
            .priceCurrency = history.metadata.currency,
            .events = researchWorkspace_.events,
            .targetEstimates =
                researchWorkspace_.targetEstimates,
        });
    }
    const auto definition = screenDefinition();
    const auto report = runFundamentalScreen(
        definition,
        inputs,
        asOfDateInput_->date());
    if (!report.ok()) {
        screenCoverageLabel_->setText(report.error);
        return;
    }
    const auto matched =
        std::ranges::count(
            report.rows,
            true,
            &FundamentalScreenRow::matched);
    screenCoverageLabel_->setText(
        tr("%1 matched · %2 cached companies in the current universe · "
           "%3 without compatible raw daily price history. "
           "This is a current-universe screen, not a survivorship-bias-free "
           "historical universe.")
            .arg(
                static_cast<qulonglong>(matched))
            .arg(static_cast<qulonglong>(cachedCompanies))
            .arg(static_cast<qulonglong>(missingPrice)));
    screenResultsTable_->setRowCount(
        static_cast<int>(report.rows.size()));
    for (std::size_t row = 0;
         row < report.rows.size();
         ++row) {
        const auto& value = report.rows[row];
        const auto set = [&](const int column, const QString& text) {
            screenResultsTable_->setItem(
                static_cast<int>(row),
                column,
                item(text, value.unavailableReason));
        };
        set(0, value.symbol);
        set(1, value.matched ? tr("Yes") : tr("No"));
        set(
            2,
            displayNumber(screenValue(
                value,
                definition.sortField)));
        set(
            3,
            displayNumber(screenValue(
                value,
                FundamentalScreenField::Price)));
        set(
            4,
            displayNumber(
                screenValue(
                    value,
                    FundamentalScreenField::RevenueGrowthYoY),
                true));
        set(
            5,
            displayNumber(screenValue(
                value,
                FundamentalScreenField::PriceToEarnings)));
        set(
            6,
            displayNumber(
                screenValue(
                    value,
                    FundamentalScreenField::OperatingMargin),
                true));
        set(
            7,
            displayNumber(screenValue(
                value,
                FundamentalScreenField::Rsi14)));
        set(
            8,
            displayNumber(screenValue(
                value,
                FundamentalScreenField::DaysToEarnings)));
        set(
            9,
            value.unavailableReason.isEmpty()
                ? value.latestFiledDate.toString(Qt::ISODate)
                : value.unavailableReason);
        if (value.matched &&
            asOfDateInput_->date() == QDate::currentDate()) {
            emitScreenAlert(definition, value);
        }
    }
    emit settingsChanged();
}

void FundamentalWorkbenchWidget::evaluateCurrentScreenAlert() {
    if (!screenAlertEnabled_ ||
        !screenAlertEnabled_->isChecked() ||
        asOfDateInput_->date() != QDate::currentDate() ||
        symbol_.isEmpty() ||
        !store_ || !store_->isOpen() ||
        !historyStore_ || !historyStore_->isOpen()) {
        return;
    }
    const auto company = currentCompany();
    const auto history = historyStore_->loadLatestSeries(
        symbol_,
        Timeframe::OneDay);
    if (!company.ok() || !history.ok()) {
        return;
    }
    const auto definition = screenDefinition();
    const auto report = runFundamentalScreen(
        definition,
        {{
            .company = company.company,
            .dailyBars = history.bars,
            .priceCurrency = history.metadata.currency,
            .events = researchWorkspace_.events,
            .targetEstimates =
                researchWorkspace_.targetEstimates,
        }},
        asOfDateInput_->date());
    if (report.ok() &&
        report.rows.size() == 1 &&
        report.rows.front().matched) {
        emitScreenAlert(definition, report.rows.front());
    }
}

void FundamentalWorkbenchWidget::emitScreenAlert(
    const FundamentalScreenDefinition& definition,
    const FundamentalScreenRow& row) {
    if (!screenAlertEnabled_ ||
        !screenAlertEnabled_->isChecked() ||
        !row.matched ||
        !row.priceDate.isValid()) {
        return;
    }
    const auto digest = QCryptographicHash::hash(
                            serializeFundamentalScreen(definition) +
                                row.symbol.toUtf8(),
                            QCryptographicHash::Sha256)
                            .toHex();
    emit alertTriggered({
        .alertId =
            QStringLiteral("fundamental-screen-%1")
                .arg(QString::fromLatin1(digest)),
        .symbol = row.symbol,
        .timestamp =
            QDateTime(
                row.priceDate,
                QTime(0, 0),
                QTimeZone::UTC)
                .toSecsSinceEpoch(),
        .triggeredAtUtc =
            QDateTime::currentSecsSinceEpoch(),
        .message =
            tr("Fundamental screen match · %1 · as-of price %2 · "
               "latest filing %3")
                .arg(
                    row.symbol,
                    row.priceDate.toString(Qt::ISODate),
                    row.latestFiledDate.isValid()
                        ? row.latestFiledDate.toString(Qt::ISODate)
                        : QStringLiteral("—")),
    });
}

void FundamentalWorkbenchWidget::refreshEventChoices() {
    const auto previous =
        eventSelector_->currentData().toString();
    eventSelector_->clear();
    for (const auto& event : researchWorkspace_.events) {
        if (normalizeWatchlistSymbol(event.symbol) != symbol_ ||
            (event.type != ResearchEventType::Earnings &&
             event.type != ResearchEventType::Filing) ||
            !event.scheduledDate.isValid()) {
            continue;
        }
        eventSelector_->addItem(
            QStringLiteral("%1 · %2 · %3")
                .arg(
                    event.scheduledDate.toString(Qt::ISODate),
                    researchEventTypeLabel(event.type),
                    event.title),
            event.id);
    }
    if (const auto index = eventSelector_->findData(previous);
        index >= 0) {
        eventSelector_->setCurrentIndex(index);
    }
}

void FundamentalWorkbenchWidget::runEventStudy() {
    eventResultTable_->setRowCount(0);
    if (!historyStore_ || !historyStore_->isOpen()) {
        eventSummaryLabel_->setText(
            tr("Raw daily price cache is unavailable."));
        return;
    }
    const auto eventId =
        eventSelector_->currentData().toString();
    const auto event = std::ranges::find(
        researchWorkspace_.events,
        eventId,
        &ResearchEvent::id);
    if (event == researchWorkspace_.events.end()) {
        eventSummaryLabel_->setText(
            tr("Select a cached earnings or SEC filing event."));
        return;
    }
    const auto security = historyStore_->loadLatestSeries(
        symbol_,
        Timeframe::OneDay);
    const auto benchmarkSymbol = normalizeWatchlistSymbol(
        eventBenchmarkInput_->text());
    const auto benchmark = historyStore_->loadLatestSeries(
        benchmarkSymbol,
        Timeframe::OneDay);
    if (!security.ok()) {
        eventSummaryLabel_->setText(
            tr("Load %1 on the daily timeframe to seed event history.")
                .arg(symbol_));
        return;
    }
    const auto report = calculateEventImpact(
        security.bars,
        benchmark.ok() ? benchmark.bars : Bars{},
        event->scheduledDate,
        eventAfterCloseInput_->isChecked());
    if (!report.ok()) {
        eventSummaryLabel_->setText(report.error);
        return;
    }
    eventSummaryLabel_->setText(
        tr("Requested %1 · aligned trading date %2 · opening gap %3 · "
           "event-day return %4 · volume / prior 20-day average %5. %6")
            .arg(
                report.requestedEventDate.toString(Qt::ISODate),
                report.alignedTradingDate.toString(Qt::ISODate),
                displayNumber(report.openingGapPercent, true),
                displayNumber(report.eventDayReturnPercent, true),
                displayNumber(report.eventVolumeRatio20),
                report.assumption));
    eventResultTable_->setRowCount(
        static_cast<int>(report.windows.size()));
    for (std::size_t row = 0;
         row < report.windows.size();
         ++row) {
        const auto& window = report.windows[row];
        eventResultTable_->setItem(
            static_cast<int>(row),
            0,
            item(tr("+%1 trading day(s)")
                     .arg(window.tradingDaysAfter)));
        eventResultTable_->setItem(
            static_cast<int>(row),
            1,
            item(displayNumber(
                window.securityReturnPercent,
                true)));
        eventResultTable_->setItem(
            static_cast<int>(row),
            2,
            item(displayNumber(
                window.benchmarkReturnPercent,
                true)));
        eventResultTable_->setItem(
            static_cast<int>(row),
            3,
            item(displayNumber(
                window.abnormalReturnPercent,
                true)));
    }
    if (!benchmark.ok()) {
        emit statusMessage(
            tr("Event returns were calculated without benchmark adjustment. "
               "Load %1 on the daily timeframe to enable abnormal returns.")
                .arg(benchmarkSymbol));
    }
}

} // namespace tvchart
