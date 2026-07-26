#include "strategy/strategy_lab_widget.hpp"

#include "data/historical_data_store.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace tvchart {
namespace {

[[nodiscard]] QString number(const double value, const int decimals = 2) {
    return QLocale::system().toString(value, 'f', decimals);
}

[[nodiscard]] QString utcDateTime(const std::int64_t timestamp) {
    return timestamp > 0
               ? QDateTime::fromSecsSinceEpoch(timestamp, QTimeZone::UTC)
                     .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
               : QStringLiteral("—");
}

[[nodiscard]] bool periodField(const StrategyField field) {
    return field == StrategyField::SimpleMovingAverage ||
           field == StrategyField::ExponentialMovingAverage ||
           field == StrategyField::RelativeStrengthIndex ||
           field == StrategyField::VolumeRatio;
}

[[nodiscard]] StrategyField comboField(const QComboBox& combo) {
    return static_cast<StrategyField>(combo.currentData().toInt());
}

void setComboData(QComboBox& combo, const int value) {
    const auto index = combo.findData(value);
    if (index >= 0) {
        combo.setCurrentIndex(index);
    }
}

[[nodiscard]] QString scanStatusText(const ScanStatus status) {
    switch (status) {
    case ScanStatus::Match:
        return QStringLiteral("MATCH");
    case ScanStatus::NoMatch:
        return QStringLiteral("No match");
    case ScanStatus::Unavailable:
        return QStringLiteral("Unavailable");
    }
    return QStringLiteral("Unavailable");
}

} // namespace

StrategyLabWidget::StrategyLabWidget(
    HistoricalDataStore* historyStore,
    QWidget* parent)
    : QWidget(parent),
      historyStore_(historyStore) {
    buildUi();
}

void StrategyLabWidget::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs);

    auto* backtest = new QWidget(tabs);
    auto* backtestLayout = new QVBoxLayout(backtest);
    backtestLayout->addWidget(buildRuleEditor(
        tr("Entry rule"),
        entryControls_,
        StrategyComparison::CrossesAbove));
    backtestLayout->addWidget(buildRuleEditor(
        tr("Exit rule"),
        exitControls_,
        StrategyComparison::CrossesBelow));

    auto* parameters = new QGroupBox(tr("Execution assumptions"), backtest);
    auto* parameterLayout = new QFormLayout(parameters);
    initialCapital_ = new QDoubleSpinBox(parameters);
    initialCapital_->setRange(1.0, 1.0e12);
    initialCapital_->setDecimals(2);
    initialCapital_->setValue(100'000.0);
    initialCapital_->setPrefix(QStringLiteral("$"));
    parameterLayout->addRow(tr("Initial capital"), initialCapital_);
    allocationPercent_ = new QDoubleSpinBox(parameters);
    allocationPercent_->setRange(0.01, 100.0);
    allocationPercent_->setDecimals(2);
    allocationPercent_->setValue(100.0);
    allocationPercent_->setSuffix(QStringLiteral("%"));
    parameterLayout->addRow(tr("Capital per trade"), allocationPercent_);
    commission_ = new QDoubleSpinBox(parameters);
    commission_->setRange(0.0, 1.0e6);
    commission_->setDecimals(2);
    commission_->setPrefix(QStringLiteral("$"));
    parameterLayout->addRow(tr("Commission / side"), commission_);
    slippage_ = new QDoubleSpinBox(parameters);
    slippage_->setRange(0.0, 1'000.0);
    slippage_->setDecimals(2);
    slippage_->setSuffix(tr(" bps"));
    parameterLayout->addRow(tr("Slippage"), slippage_);
    fractionalShares_ = new QCheckBox(tr("Allow fractional shares"), parameters);
    fractionalShares_->setChecked(true);
    parameterLayout->addRow({}, fractionalShares_);
    backtestLayout->addWidget(parameters);

    auto* run = new QPushButton(tr("Run backtest"), backtest);
    run->setObjectName(QStringLiteral("strategyRunBacktest"));
    connect(run, &QPushButton::clicked, this, &StrategyLabWidget::runCurrentBacktest);
    backtestLayout->addWidget(run);
    metricsLabel_ = new QLabel(
        tr("Load a series, then run the strategy. Signals use the completed "
           "bar and execute at the next bar open."),
        backtest);
    metricsLabel_->setWordWrap(true);
    metricsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    backtestLayout->addWidget(metricsLabel_);
    tradesTable_ = new QTableWidget(0, 7, backtest);
    tradesTable_->setObjectName(QStringLiteral("strategyTradesTable"));
    tradesTable_->setHorizontalHeaderLabels({
        tr("Entry"),
        tr("Exit"),
        tr("Quantity"),
        tr("Entry px"),
        tr("Exit px"),
        tr("P/L"),
        tr("Exit type"),
    });
    tradesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tradesTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    tradesTable_->horizontalHeader()->setStretchLastSection(true);
    backtestLayout->addWidget(tradesTable_, 1);
    auto* disclaimer = new QLabel(
        tr("Educational local analysis only. Long-only, one position at a "
           "time; no dividends, taxes, shorting, or broker execution."),
        backtest);
    disclaimer->setWordWrap(true);
    disclaimer->setStyleSheet(QStringLiteral("color: #8c8c8c;"));
    backtestLayout->addWidget(disclaimer);
    tabs->addTab(backtest, tr("Backtest"));

    auto* replayTab = new QWidget(tabs);
    auto* replayLayout = new QVBoxLayout(replayTab);
    auto* replayForm = new QFormLayout;
    replayWarmup_ = new QSpinBox(replayTab);
    replayWarmup_->setRange(1, 1'000'000);
    replayWarmup_->setValue(100);
    replayForm->addRow(tr("Initially visible bars"), replayWarmup_);
    replaySpeed_ = new QComboBox(replayTab);
    replaySpeed_->addItem(tr("Slow · 1 second"), 1'000);
    replaySpeed_->addItem(tr("Normal · 400 ms"), 400);
    replaySpeed_->addItem(tr("Fast · 100 ms"), 100);
    replaySpeed_->setCurrentIndex(1);
    replayForm->addRow(tr("Playback speed"), replaySpeed_);
    replayLayout->addLayout(replayForm);
    auto* replayActions = new QHBoxLayout;
    auto* reset = new QPushButton(tr("Start / reset"), replayTab);
    auto* step = new QPushButton(tr("Step"), replayTab);
    replayPlay_ = new QPushButton(tr("Play"), replayTab);
    auto* restore = new QPushButton(tr("Restore live view"), replayTab);
    replayActions->addWidget(reset);
    replayActions->addWidget(step);
    replayActions->addWidget(replayPlay_);
    replayActions->addWidget(restore);
    replayLayout->addLayout(replayActions);
    replayStatus_ = new QLabel(tr("Replay is inactive."), replayTab);
    replayStatus_->setWordWrap(true);
    replayLayout->addWidget(replayStatus_);
    replayLayout->addStretch();
    replayTimer_ = new QTimer(this);
    connect(reset, &QPushButton::clicked, this, &StrategyLabWidget::resetReplay);
    connect(step, &QPushButton::clicked, this, [this] { stepReplay(); });
    connect(
        replayPlay_,
        &QPushButton::clicked,
        this,
        [this] {
            if (!replayActive_) {
                resetReplay();
            }
            if (!replayActive_) {
                return;
            }
            if (replayTimer_->isActive()) {
                replayTimer_->stop();
                replayPlay_->setText(tr("Play"));
            } else {
                replayTimer_->start(replaySpeed_->currentData().toInt());
                replayPlay_->setText(tr("Pause"));
            }
        });
    connect(
        replayTimer_,
        &QTimer::timeout,
        this,
        [this] { stepReplay(); });
    connect(
        restore,
        &QPushButton::clicked,
        this,
        [this] { stopReplay(true); });
    connect(
        replaySpeed_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            if (replayTimer_->isActive()) {
                replayTimer_->start(replaySpeed_->currentData().toInt());
            }
        });
    tabs->addTab(replayTab, tr("Replay"));

    auto* scanner = new QWidget(tabs);
    auto* scannerLayout = new QVBoxLayout(scanner);
    scannerStatus_ = new QLabel(
        tr("Scans the latest completed cached bar for the active watchlist "
           "using the entry rule."),
        scanner);
    scannerStatus_->setWordWrap(true);
    scannerLayout->addWidget(scannerStatus_);
    auto* scan = new QPushButton(tr("Scan cached watchlist"), scanner);
    scan->setObjectName(QStringLiteral("strategyScanWatchlist"));
    connect(scan, &QPushButton::clicked, this, &StrategyLabWidget::scanWatchlist);
    scannerLayout->addWidget(scan);
    scannerTable_ = new QTableWidget(0, 6, scanner);
    scannerTable_->setHorizontalHeaderLabels({
        tr("Symbol"),
        tr("Provider"),
        tr("Status"),
        tr("Last bar"),
        tr("Close"),
        tr("Detail"),
    });
    scannerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scannerTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    scannerTable_->horizontalHeader()->setStretchLastSection(true);
    scannerLayout->addWidget(scannerTable_, 1);
    tabs->addTab(scanner, tr("Scanner"));

    auto* alerts = new QWidget(tabs);
    auto* alertLayout = new QVBoxLayout(alerts);
    auto* alertExplanation = new QLabel(
        tr("Foreground alerts evaluate the entry rule whenever this app "
           "finishes loading a new series. They do not run while the app is "
           "closed."),
        alerts);
    alertExplanation->setWordWrap(true);
    alertLayout->addWidget(alertExplanation);
    alertEnabled_ =
        new QCheckBox(tr("Enable foreground alert for the active symbol"), alerts);
    alertLayout->addWidget(alertEnabled_);
    auto* evaluate = new QPushButton(tr("Evaluate now"), alerts);
    connect(
        evaluate,
        &QPushButton::clicked,
        this,
        &StrategyLabWidget::evaluateForegroundAlert);
    alertLayout->addWidget(evaluate);
    alertTable_ = new QTableWidget(0, 3, alerts);
    alertTable_->setHorizontalHeaderLabels({
        tr("Time"),
        tr("Symbol"),
        tr("Message"),
    });
    alertTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alertTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    alertTable_->horizontalHeader()->setStretchLastSection(true);
    alertLayout->addWidget(alertTable_, 1);
    trayIcon_ = new QSystemTrayIcon(
        QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation),
        this);
    trayIcon_->setToolTip(tr("TradingView Chart foreground alerts"));
    tabs->addTab(alerts, tr("Alerts"));
}

QWidget* StrategyLabWidget::buildRuleEditor(
    const QString& title,
    RuleControls& controls,
    const StrategyComparison defaultComparison) {
    auto* group = new QGroupBox(title, this);
    auto* layout = new QFormLayout(group);
    controls.leftField = new QComboBox(group);
    populateFieldCombo(*controls.leftField, false);
    controls.leftPeriod = new QSpinBox(group);
    controls.leftPeriod->setRange(1, 500);
    controls.leftPeriod->setValue(20);
    controls.comparison = new QComboBox(group);
    for (const auto comparison : {
             StrategyComparison::GreaterThan,
             StrategyComparison::LessThan,
             StrategyComparison::CrossesAbove,
             StrategyComparison::CrossesBelow,
         }) {
        controls.comparison->addItem(
            strategyComparisonLabel(comparison),
            static_cast<int>(comparison));
    }
    setComboData(*controls.comparison, static_cast<int>(defaultComparison));
    controls.rightField = new QComboBox(group);
    populateFieldCombo(*controls.rightField, true);
    controls.rightPeriod = new QSpinBox(group);
    controls.rightPeriod->setRange(1, 500);
    controls.rightPeriod->setValue(20);
    controls.constant = new QDoubleSpinBox(group);
    controls.constant->setRange(-1.0e12, 1.0e12);
    controls.constant->setDecimals(6);
    controls.constant->setValue(0.0);

    auto* left = new QWidget(group);
    auto* leftLayout = new QHBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(controls.leftField);
    leftLayout->addWidget(controls.leftPeriod);
    layout->addRow(tr("Left"), left);
    layout->addRow(tr("Comparison"), controls.comparison);
    auto* right = new QWidget(group);
    auto* rightLayout = new QHBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(controls.rightField);
    rightLayout->addWidget(controls.rightPeriod);
    rightLayout->addWidget(controls.constant);
    layout->addRow(tr("Right"), right);

    setComboData(
        *controls.leftField,
        static_cast<int>(StrategyField::Close));
    setComboData(
        *controls.rightField,
        static_cast<int>(StrategyField::SimpleMovingAverage));
    connect(
        controls.leftField,
        &QComboBox::currentIndexChanged,
        this,
        [this, &controls](int) { refreshRuleControls(controls); });
    connect(
        controls.rightField,
        &QComboBox::currentIndexChanged,
        this,
        [this, &controls](int) { refreshRuleControls(controls); });
    refreshRuleControls(controls);
    return group;
}

void StrategyLabWidget::populateFieldCombo(
    QComboBox& combo,
    const bool includeConstant) {
    if (includeConstant) {
        combo.addItem(tr("Constant"), -1);
    }
    for (const auto field : {
             StrategyField::Open,
             StrategyField::High,
             StrategyField::Low,
             StrategyField::Close,
             StrategyField::Volume,
             StrategyField::SimpleMovingAverage,
             StrategyField::ExponentialMovingAverage,
             StrategyField::RelativeStrengthIndex,
             StrategyField::VolumeRatio,
         }) {
        combo.addItem(strategyFieldLabel(field), static_cast<int>(field));
    }
}

void StrategyLabWidget::refreshRuleControls(RuleControls& controls) {
    controls.leftPeriod->setEnabled(periodField(comboField(*controls.leftField)));
    const auto constant = controls.rightField->currentData().toInt() < 0;
    controls.constant->setEnabled(constant);
    controls.rightPeriod->setEnabled(
        !constant && periodField(comboField(*controls.rightField)));
}

StrategyCondition StrategyLabWidget::condition(
    const RuleControls& controls) const {
    StrategyCondition result{
        .left = {
            .field = comboField(*controls.leftField),
            .period =
                static_cast<std::uint32_t>(controls.leftPeriod->value()),
        },
        .comparison = static_cast<StrategyComparison>(
            controls.comparison->currentData().toInt()),
        .constant = controls.constant->value(),
    };
    if (controls.rightField->currentData().toInt() >= 0) {
        result.right = StrategyOperand{
            .field = comboField(*controls.rightField),
            .period =
                static_cast<std::uint32_t>(controls.rightPeriod->value()),
        };
    }
    return result;
}

StrategyDefinition StrategyLabWidget::currentStrategy() const {
    return {
        .id = QStringLiteral("strategy-lab"),
        .name = QStringLiteral("Strategy Lab"),
        .entry = {.conditions = {condition(entryControls_)}},
        .exit = {.conditions = {condition(exitControls_)}},
    };
}

void StrategyLabWidget::applyStrategy(const StrategyDefinition& strategy) {
    const auto apply =
        [](RuleControls& controls, const StrategyCondition& condition) {
            setComboData(
                *controls.leftField,
                static_cast<int>(condition.left.field));
            controls.leftPeriod->setValue(
                static_cast<int>(condition.left.period));
            setComboData(
                *controls.comparison,
                static_cast<int>(condition.comparison));
            if (condition.right) {
                setComboData(
                    *controls.rightField,
                    static_cast<int>(condition.right->field));
                controls.rightPeriod->setValue(
                    static_cast<int>(condition.right->period));
            } else {
                setComboData(*controls.rightField, -1);
                controls.constant->setValue(condition.constant);
            }
        };
    if (!strategy.entry.conditions.empty()) {
        apply(entryControls_, strategy.entry.conditions.front());
    }
    if (!strategy.exit.conditions.empty()) {
        apply(exitControls_, strategy.exit.conditions.front());
    }
    refreshRuleControls(entryControls_);
    refreshRuleControls(exitControls_);
}

void StrategyLabWidget::setCurrentSeries(
    QString symbol,
    const Timeframe timeframe,
    QString provider,
    Bars bars,
    MarketDataMetadata metadata) {
    stopReplay(false);
    symbol_ = std::move(symbol);
    timeframe_ = timeframe;
    provider_ = std::move(provider);
    bars_ = std::move(bars);
    metadata_ = std::move(metadata);
    replayWarmup_->setRange(
        1,
        std::max(1, static_cast<int>(bars_.size())));
    replayWarmup_->setValue(
        std::min(
            static_cast<int>(bars_.size()),
            std::max(1, std::min(100, replayWarmup_->value()))));

    if (historyStore_ && historyStore_->isOpen() &&
        metadata_.deliveryMode == DataDeliveryMode::Polled &&
        !bars_.empty()) {
        const auto error = historyStore_->upsertSeries(
            provider_,
            symbol_,
            timeframe_,
            bars_,
            metadata_);
        if (!error.isEmpty()) {
            emit statusMessage(
                tr("History cache did not update: %1").arg(error));
        }
    }
    evaluateForegroundAlert();
}

void StrategyLabWidget::setWatchlistSymbols(QStringList symbols) {
    for (auto& symbol : symbols) {
        symbol = normalizeWatchlistSymbol(std::move(symbol));
    }
    symbols.removeAll({});
    symbols.removeDuplicates();
    watchlistSymbols_ = std::move(symbols);
}

void StrategyLabWidget::restoreSettings(QSettings& settings) {
    const auto strategy =
        deserializeStrategy(
            settings.value(QStringLiteral("strategyLab/strategy")).toByteArray());
    if (strategy.ok()) {
        applyStrategy(strategy.strategy);
    }
    initialCapital_->setValue(
        settings.value(QStringLiteral("strategyLab/initialCapital"), 100'000.0)
            .toDouble());
    allocationPercent_->setValue(
        settings.value(QStringLiteral("strategyLab/allocationPercent"), 100.0)
            .toDouble());
    commission_->setValue(
        settings.value(QStringLiteral("strategyLab/commission"), 0.0).toDouble());
    slippage_->setValue(
        settings.value(QStringLiteral("strategyLab/slippageBps"), 0.0).toDouble());
    fractionalShares_->setChecked(
        settings.value(QStringLiteral("strategyLab/fractional"), true).toBool());
    replayWarmup_->setValue(
        settings.value(QStringLiteral("strategyLab/replayWarmup"), 100).toInt());
    alertEnabled_->setChecked(
        settings.value(QStringLiteral("strategyLab/alertEnabled"), false).toBool());
}

void StrategyLabWidget::saveSettings(QSettings& settings) const {
    settings.setValue(
        QStringLiteral("strategyLab/strategy"),
        serializeStrategy(currentStrategy()));
    settings.setValue(
        QStringLiteral("strategyLab/initialCapital"),
        initialCapital_->value());
    settings.setValue(
        QStringLiteral("strategyLab/allocationPercent"),
        allocationPercent_->value());
    settings.setValue(
        QStringLiteral("strategyLab/commission"),
        commission_->value());
    settings.setValue(
        QStringLiteral("strategyLab/slippageBps"),
        slippage_->value());
    settings.setValue(
        QStringLiteral("strategyLab/fractional"),
        fractionalShares_->isChecked());
    settings.setValue(
        QStringLiteral("strategyLab/replayWarmup"),
        replayWarmup_->value());
    settings.setValue(
        QStringLiteral("strategyLab/alertEnabled"),
        alertEnabled_->isChecked());
}

void StrategyLabWidget::restoreChartIfReplaying() {
    stopReplay(true);
}

void StrategyLabWidget::runCurrentBacktest() {
    if (bars_.empty()) {
        metricsLabel_->setText(tr("No valid series is loaded."));
        return;
    }
    const auto result = runBacktest(
        bars_,
        currentStrategy(),
        {
            .initialCapital = initialCapital_->value(),
            .allocationPercent = allocationPercent_->value(),
            .commissionPerSide = commission_->value(),
            .slippageBasisPoints = slippage_->value(),
            .allowFractionalShares = fractionalShares_->isChecked(),
        });
    showBacktest(result);
}

void StrategyLabWidget::showBacktest(const BacktestResult& result) {
    tradesTable_->setRowCount(0);
    if (!result.ok()) {
        metricsLabel_->setText(tr("Backtest failed: %1").arg(result.error));
        return;
    }
    const auto profitFactor =
        result.profitFactor
            ? (std::isinf(*result.profitFactor)
                   ? QStringLiteral("∞")
                   : number(*result.profitFactor))
            : QStringLiteral("—");
    metricsLabel_->setText(
        tr("Final equity %1 · Net %2 (%3%) · Max drawdown %4% · "
           "%5 trades · Win rate %6% · Profit factor %7 · Exposure %8%")
            .arg(number(result.finalEquity, 2))
            .arg(number(result.netProfit, 2))
            .arg(number(result.totalReturnPercent, 2))
            .arg(number(result.maximumDrawdownPercent, 2))
            .arg(result.trades.size())
            .arg(number(result.winRatePercent, 1))
            .arg(profitFactor)
            .arg(number(result.exposurePercent, 1)));
    tradesTable_->setRowCount(static_cast<int>(result.trades.size()));
    for (std::size_t index = 0; index < result.trades.size(); ++index) {
        const auto& trade = result.trades[index];
        const auto row = static_cast<int>(index);
        const QStringList values{
            utcDateTime(trade.entryTimestamp),
            utcDateTime(trade.exitTimestamp),
            number(trade.quantity, 4),
            number(trade.entryPrice, 4),
            number(trade.exitPrice, 4),
            number(trade.profitLoss, 2),
            trade.forcedExit ? tr("Forced final close") : tr("Rule"),
        };
        for (int column = 0; column < values.size(); ++column) {
            tradesTable_->setItem(
                row,
                column,
                new QTableWidgetItem(values[column]));
        }
    }
}

void StrategyLabWidget::resetReplay() {
    if (bars_.empty()) {
        replayStatus_->setText(tr("No valid series is loaded."));
        return;
    }
    replayTimer_->stop();
    replayPlay_->setText(tr("Play"));
    const auto error = replay_.reset(
        bars_,
        static_cast<std::size_t>(replayWarmup_->value()));
    if (!error.isEmpty()) {
        replayStatus_->setText(error);
        return;
    }
    replayActive_ = true;
    emit replayBarsRequested(replay_.visibleBars());
    refreshReplayLabel();
}

void StrategyLabWidget::stepReplay(const std::size_t count) {
    if (!replayActive_) {
        resetReplay();
        if (!replayActive_) {
            return;
        }
    } else {
        static_cast<void>(replay_.step(count));
        emit replayBarsRequested(replay_.visibleBars());
    }
    if (replay_.finished()) {
        replayTimer_->stop();
        replayPlay_->setText(tr("Play"));
    }
    refreshReplayLabel();
}

void StrategyLabWidget::stopReplay(const bool restoreChart) {
    replayTimer_->stop();
    replayPlay_->setText(tr("Play"));
    replay_.clear();
    const auto wasActive = replayActive_;
    replayActive_ = false;
    replayStatus_->setText(tr("Replay is inactive."));
    if (restoreChart && wasActive) {
        emit restoreFullSeriesRequested();
    }
}

void StrategyLabWidget::refreshReplayLabel() {
    replayStatus_->setText(
        tr("%1 / %2 bars · %3% · current bar %4 UTC")
            .arg(replay_.visibleCount())
            .arg(replay_.totalCount())
            .arg(number(replay_.progressPercent(), 1))
            .arg(utcDateTime(replay_.currentTimestamp())));
}

void StrategyLabWidget::scanWatchlist() {
    scannerTable_->setRowCount(0);
    if (!historyStore_ || !historyStore_->isOpen()) {
        scannerStatus_->setText(tr("Historical cache is unavailable."));
        return;
    }
    if (watchlistSymbols_.isEmpty()) {
        scannerStatus_->setText(tr("The active watchlist is empty."));
        return;
    }

    std::vector<ScanSeries> candidates;
    QStringList loadErrors;
    candidates.reserve(static_cast<std::size_t>(watchlistSymbols_.size()));
    for (const auto& symbol : watchlistSymbols_) {
        auto cached = historyStore_->loadLatestSeries(symbol, timeframe_);
        loadErrors.push_back(cached.error);
        candidates.push_back({
            .symbol = symbol,
            .provider =
                cached.ok() ? cached.key.provider : QStringLiteral("Cache"),
            .bars = std::move(cached.bars),
        });
    }
    auto results = scanLatest(candidates, currentStrategy().entry);
    scannerTable_->setRowCount(static_cast<int>(results.size()));
    auto matches = std::size_t{};
    for (std::size_t index = 0; index < results.size(); ++index) {
        auto& result = results[index];
        if (!loadErrors[static_cast<int>(index)].isEmpty()) {
            result.detail = loadErrors[static_cast<int>(index)];
        }
        if (result.status == ScanStatus::Match) {
            ++matches;
        }
        const QStringList values{
            result.symbol,
            result.provider,
            scanStatusText(result.status),
            utcDateTime(result.timestamp),
            result.timestamp > 0 ? number(result.latestClose, 4)
                                 : QStringLiteral("—"),
            result.detail,
        };
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            if (column == 2 && result.status == ScanStatus::Match) {
                item->setForeground(QColor(QStringLiteral("#26a69a")));
            }
            scannerTable_->setItem(
                static_cast<int>(index),
                column,
                item);
        }
    }
    scannerStatus_->setText(
        tr("%1 of %2 cached symbols match. Unavailable means no compatible "
           "cached bars or incomplete indicator warm-up.")
            .arg(matches)
            .arg(results.size()));
}

void StrategyLabWidget::evaluateForegroundAlert() {
    if (!alertEnabled_ || !alertEnabled_->isChecked() ||
        symbol_.isEmpty() || bars_.empty()) {
        return;
    }
    const auto strategy = currentStrategy();
    const auto hash = QCryptographicHash::hash(
                          serializeStrategy(strategy),
                          QCryptographicHash::Sha256)
                          .toHex()
                          .left(16);
    const StrategyAlert alert{
        .id = QStringLiteral("strategy-lab-%1").arg(QString::fromLatin1(hash)),
        .symbol = symbol_,
        .condition = strategy.entry,
    };
    const auto evaluation = alertEngine_.evaluate(alert, bars_);
    if (!evaluation.error.isEmpty()) {
        emit statusMessage(tr("Foreground alert failed: %1").arg(evaluation.error));
        return;
    }
    if (evaluation.trigger) {
        appendAlert(*evaluation.trigger);
    }
}

void StrategyLabWidget::appendAlert(const AlertTrigger& trigger) {
    const auto row = alertTable_->rowCount();
    alertTable_->insertRow(row);
    alertTable_->setItem(row, 0, new QTableWidgetItem(utcDateTime(trigger.timestamp)));
    alertTable_->setItem(row, 1, new QTableWidgetItem(trigger.symbol));
    alertTable_->setItem(row, 2, new QTableWidgetItem(trigger.message));
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        if (!trayIcon_->isVisible()) {
            trayIcon_->show();
        }
        trayIcon_->showMessage(
            tr("Strategy Lab alert · %1").arg(trigger.symbol),
            trigger.message,
            QSystemTrayIcon::Information,
            8'000);
    }
    emit statusMessage(trigger.message);
}

} // namespace tvchart
