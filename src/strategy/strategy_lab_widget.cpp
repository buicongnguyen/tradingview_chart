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
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTime>
#include <QTimeZone>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <ranges>
#include <set>
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

[[nodiscard]] QString timeframeText(const Timeframe timeframe) {
    switch (timeframe) {
    case Timeframe::OneMinute:
        return QStringLiteral("1m");
    case Timeframe::FiveMinutes:
        return QStringLiteral("5m");
    case Timeframe::FifteenMinutes:
        return QStringLiteral("15m");
    case Timeframe::OneHour:
        return QStringLiteral("1h");
    case Timeframe::OneDay:
        return QStringLiteral("1D");
    }
    return QStringLiteral("?");
}

[[nodiscard]] std::optional<std::uint32_t> firstStrategyPeriod(
    const StrategyDefinition& strategy) {
    const auto findIn =
        [](const ConditionGroup& group)
        -> std::optional<std::uint32_t> {
        for (const auto& condition : group.conditions) {
            if (periodField(condition.left.field)) {
                return condition.left.period;
            }
            if (condition.right &&
                periodField(condition.right->field)) {
                return condition.right->period;
            }
        }
        return std::nullopt;
    };
    if (const auto entry = findIn(strategy.entry)) {
        return entry;
    }
    return findIn(strategy.exit);
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

[[nodiscard]] Bars analysisBars(
    const Bars& bars,
    const Timeframe timeframe,
    const MarketDataMetadata& metadata) {
    if (metadata.deliveryMode != DataDeliveryMode::Polled) {
        return bars;
    }
    const auto count =
        completedBarCount(bars, timeframe, metadata.retrievedAtUtc);
    return {
        bars.begin(),
        std::next(
            bars.begin(),
            static_cast<std::ptrdiff_t>(count)),
    };
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

    auto* library = new QGroupBox(tr("Named strategy"), backtest);
    auto* libraryLayout = new QFormLayout(library);
    strategyName_ = new QLineEdit(tr("Strategy Lab"), library);
    strategyName_->setMaxLength(120);
    connect(
        strategyName_,
        &QLineEdit::textEdited,
        this,
        &StrategyLabWidget::workspaceChanged);
    strategyLibrary_ = new QComboBox(library);
    strategyLibrary_->setPlaceholderText(tr("No saved strategies"));
    auto* libraryActions = new QWidget(library);
    auto* libraryActionLayout = new QHBoxLayout(libraryActions);
    libraryActionLayout->setContentsMargins(0, 0, 0, 0);
    auto* newStrategy = new QPushButton(tr("Save as new"), libraryActions);
    auto* saveStrategy = new QPushButton(tr("Save / update"), libraryActions);
    auto* loadStrategy = new QPushButton(tr("Load"), libraryActions);
    auto* deleteStrategy = new QPushButton(tr("Delete"), libraryActions);
    libraryActionLayout->addWidget(newStrategy);
    libraryActionLayout->addWidget(saveStrategy);
    libraryActionLayout->addWidget(loadStrategy);
    libraryActionLayout->addWidget(deleteStrategy);
    libraryLayout->addRow(tr("Name"), strategyName_);
    libraryLayout->addRow(tr("Saved"), strategyLibrary_);
    libraryLayout->addRow({}, libraryActions);
    connect(
        newStrategy,
        &QPushButton::clicked,
        this,
        [this] {
            strategyLibrary_->setCurrentIndex(-1);
            strategyName_->selectAll();
            strategyName_->setFocus();
            emit statusMessage(
                tr("Enter a new name, then choose Save / update."));
        });
    connect(
        saveStrategy,
        &QPushButton::clicked,
        this,
        &StrategyLabWidget::saveNamedStrategy);
    connect(
        loadStrategy,
        &QPushButton::clicked,
        this,
        &StrategyLabWidget::loadNamedStrategy);
    connect(
        deleteStrategy,
        &QPushButton::clicked,
        this,
        &StrategyLabWidget::deleteNamedStrategy);
    backtestLayout->addWidget(library);

    backtestLayout->addWidget(buildRuleEditor(
        tr("Entry conditions"),
        entryControls_,
        StrategyComparison::CrossesAbove));
    backtestLayout->addWidget(buildRuleEditor(
        tr("Exit conditions"),
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
    holdoutEnabled_ = new QCheckBox(
        tr("Run chronological holdout validation"),
        parameters);
    holdoutEnabled_->setChecked(true);
    parameterLayout->addRow({}, holdoutEnabled_);
    holdoutPercent_ = new QSpinBox(parameters);
    holdoutPercent_->setRange(10, 50);
    holdoutPercent_->setValue(30);
    holdoutPercent_->setSuffix(QStringLiteral("%"));
    parameterLayout->addRow(tr("Holdout tail"), holdoutPercent_);
    connect(
        holdoutEnabled_,
        &QCheckBox::toggled,
        holdoutPercent_,
        &QSpinBox::setEnabled);
    const auto persistBacktestSettings = [this] {
        emit workspaceChanged();
    };
    connect(
        initialCapital_,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        persistBacktestSettings);
    connect(
        allocationPercent_,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        persistBacktestSettings);
    connect(
        commission_,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        persistBacktestSettings);
    connect(
        slippage_,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        persistBacktestSettings);
    connect(
        fractionalShares_,
        &QCheckBox::toggled,
        this,
        persistBacktestSettings);
    connect(
        holdoutEnabled_,
        &QCheckBox::toggled,
        this,
        persistBacktestSettings);
    connect(
        holdoutPercent_,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        persistBacktestSettings);
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
    validationLabel_ = new QLabel(
        tr("Holdout validation uses earlier bars only for indicator warm-up; "
           "no position is carried across the split."),
        backtest);
    validationLabel_->setWordWrap(true);
    validationLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    backtestLayout->addWidget(validationLabel_);
    tradesTable_ = new QTableWidget(0, 11, backtest);
    tradesTable_->setObjectName(QStringLiteral("strategyTradesTable"));
    tradesTable_->setHorizontalHeaderLabels({
        tr("Entry"),
        tr("Exit"),
        tr("Quantity"),
        tr("Entry px"),
        tr("Exit px"),
        tr("P/L"),
        tr("Return"),
        tr("MAE"),
        tr("MFE"),
        tr("Bars"),
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

    auto* robustness = new QWidget(tabs);
    auto* robustnessLayout = new QVBoxLayout(robustness);
    auto* robustnessControls = new QHBoxLayout;
    robustnessFolds_ = new QSpinBox(robustness);
    robustnessFolds_->setRange(2, 10);
    robustnessFolds_->setValue(4);
    robustnessFolds_->setPrefix(tr("Folds "));
    robustnessSimulations_ = new QSpinBox(robustness);
    robustnessSimulations_->setRange(100, 100'000);
    robustnessSimulations_->setSingleStep(100);
    robustnessSimulations_->setValue(1'000);
    robustnessSimulations_->setPrefix(tr("Monte Carlo "));
    auto* runRobustness =
        new QPushButton(tr("Run professional validation"), robustness);
    robustnessControls->addWidget(robustnessFolds_);
    robustnessControls->addWidget(robustnessSimulations_);
    robustnessControls->addWidget(runRobustness);
    robustnessControls->addStretch();
    robustnessLayout->addLayout(robustnessControls);
    connect(
        runRobustness,
        &QPushButton::clicked,
        this,
        &StrategyLabWidget::runRobustness);

    robustnessSummary_ = new QLabel(
        tr("Runs walk-forward folds, deterministic trade resampling, "
           "diagnostic period stability, entry-time regime attribution, and "
           "the unchanged strategy over cached watchlist history."),
        robustness);
    robustnessSummary_->setWordWrap(true);
    robustnessSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    robustnessLayout->addWidget(robustnessSummary_);

    auto* robustnessTables = new QTabWidget(robustness);
    walkForwardTable_ = new QTableWidget(0, 7, robustnessTables);
    walkForwardTable_->setHorizontalHeaderLabels({
        tr("Fold"),
        tr("Start UTC"),
        tr("End UTC"),
        tr("Return"),
        tr("Max DD"),
        tr("Trades"),
        tr("Sharpe"),
    });
    parameterTable_ = new QTableWidget(0, 6, robustnessTables);
    parameterTable_->setHorizontalHeaderLabels({
        tr("Period"),
        tr("Return"),
        tr("Max DD"),
        tr("Trades"),
        tr("Win rate"),
        tr("Profit factor"),
    });
    regimeTable_ = new QTableWidget(0, 5, robustnessTables);
    regimeTable_->setHorizontalHeaderLabels({
        tr("Regime"),
        tr("Trades"),
        tr("Win rate"),
        tr("Average return"),
        tr("Net P/L"),
    });
    batchTable_ = new QTableWidget(0, 7, robustnessTables);
    batchTable_->setHorizontalHeaderLabels({
        tr("Symbol"),
        tr("Provider"),
        tr("Status"),
        tr("Return"),
        tr("Max DD"),
        tr("Trades"),
        tr("Sharpe"),
    });
    for (auto* table : {
             walkForwardTable_,
             parameterTable_,
             regimeTable_,
             batchTable_,
         }) {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setSectionResizeMode(
            QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
    }
    robustnessTables->addTab(walkForwardTable_, tr("Walk-forward"));
    robustnessTables->addTab(parameterTable_, tr("Parameter stability"));
    robustnessTables->addTab(regimeTable_, tr("Regimes"));
    robustnessTables->addTab(batchTable_, tr("Cached symbols"));
    robustnessLayout->addWidget(robustnessTables, 1);
    auto* robustnessDisclaimer = new QLabel(
        tr("Diagnostic only. No best parameter is selected automatically. "
           "Monte Carlo resamples observed trade returns and cannot model "
           "unseen market structure."),
        robustness);
    robustnessDisclaimer->setWordWrap(true);
    robustnessDisclaimer->setStyleSheet(QStringLiteral("color: #8c8c8c;"));
    robustnessLayout->addWidget(robustnessDisclaimer);
    tabs->addTab(robustness, tr("Robustness"));

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
            emit workspaceChanged();
        });
    connect(
        replayWarmup_,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        [this](int) { emit workspaceChanged(); });
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
        tr("Saved local alerts use the current multi-condition entry rule. "
           "Automatic evaluation runs when this app loads a completed series; "
           "it does not run while the app is closed."),
        alerts);
    alertExplanation->setWordWrap(true);
    alertLayout->addWidget(alertExplanation);
    alertEnabled_ =
        new QCheckBox(tr("Automatically evaluate enabled saved alerts"), alerts);
    alertLayout->addWidget(alertEnabled_);
    auto* eventReminderRow = new QHBoxLayout;
    eventReminderEnabled_ = new QCheckBox(
        tr("Notify for sourced events within"),
        alerts);
    eventReminderLeadDays_ = new QSpinBox(alerts);
    eventReminderLeadDays_->setRange(0, 90);
    eventReminderLeadDays_->setValue(7);
    eventReminderLeadDays_->setSuffix(tr(" days"));
    eventReminderRow->addWidget(eventReminderEnabled_);
    eventReminderRow->addWidget(eventReminderLeadDays_);
    eventReminderRow->addStretch();
    alertLayout->addLayout(eventReminderRow);
    connect(
        eventReminderEnabled_,
        &QCheckBox::toggled,
        this,
        [this](bool) {
            evaluateEventReminders();
            emit workspaceChanged();
        });
    connect(
        eventReminderLeadDays_,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        [this](int) {
            evaluateEventReminders();
            emit workspaceChanged();
        });
    connect(
        alertEnabled_,
        &QCheckBox::toggled,
        this,
        &StrategyLabWidget::workspaceChanged);

    auto* alertForm = new QFormLayout;
    alertName_ = new QLineEdit(tr("Entry rule alert"), alerts);
    alertName_->setMaxLength(120);
    alertForm->addRow(tr("New alert name"), alertName_);
    alertFrequency_ = new QComboBox(alerts);
    for (const auto frequency : {
             AlertFrequency::Once,
             AlertFrequency::OncePerBar,
             AlertFrequency::OnTransition,
             AlertFrequency::Cooldown,
         }) {
        alertFrequency_->addItem(
            alertFrequencyLabel(frequency),
            static_cast<int>(frequency));
    }
    setComboData(
        *alertFrequency_,
        static_cast<int>(AlertFrequency::OncePerBar));
    alertForm->addRow(tr("Frequency"), alertFrequency_);
    alertCooldownMinutes_ = new QSpinBox(alerts);
    alertCooldownMinutes_->setRange(1, 31 * 24 * 60);
    alertCooldownMinutes_->setValue(60);
    alertCooldownMinutes_->setSuffix(tr(" minutes"));
    alertForm->addRow(tr("Cooldown"), alertCooldownMinutes_);
    alertExpiryEnabled_ = new QCheckBox(tr("Expire at"), alerts);
    alertExpiry_ = new QDateTimeEdit(
        QDateTime::currentDateTimeUtc().addDays(30),
        alerts);
    alertExpiry_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    alertExpiry_->setTimeZone(QTimeZone::UTC);
    auto* expiryRow = new QWidget(alerts);
    auto* expiryLayout = new QHBoxLayout(expiryRow);
    expiryLayout->setContentsMargins(0, 0, 0, 0);
    expiryLayout->addWidget(alertExpiryEnabled_);
    expiryLayout->addWidget(alertExpiry_, 1);
    alertForm->addRow(tr("Expiry"), expiryRow);
    alertLayout->addLayout(alertForm);
    const auto refreshAlertControls = [this](int) {
        alertCooldownMinutes_->setEnabled(
            static_cast<AlertFrequency>(
                alertFrequency_->currentData().toInt()) ==
            AlertFrequency::Cooldown);
        alertExpiry_->setEnabled(alertExpiryEnabled_->isChecked());
    };
    connect(
        alertFrequency_,
        &QComboBox::currentIndexChanged,
        this,
        refreshAlertControls);
    connect(
        alertExpiryEnabled_,
        &QCheckBox::toggled,
        this,
        [refreshAlertControls](bool) { refreshAlertControls(0); });
    refreshAlertControls(0);

    auto* alertActions = new QHBoxLayout;
    auto* addAlert = new QPushButton(tr("Save current entry rule"), alerts);
    auto* evaluate = new QPushButton(tr("Evaluate all now"), alerts);
    auto* toggle = new QPushButton(tr("Enable / disable"), alerts);
    auto* removeAlert = new QPushButton(tr("Remove"), alerts);
    alertActions->addWidget(addAlert);
    alertActions->addWidget(evaluate);
    alertActions->addWidget(toggle);
    alertActions->addWidget(removeAlert);
    alertLayout->addLayout(alertActions);
    connect(
        addAlert,
        &QPushButton::clicked,
        this,
        &StrategyLabWidget::addManagedAlert);
    connect(
        evaluate,
        &QPushButton::clicked,
        this,
        [this] { evaluateManagedAlerts(false); });
    connect(
        toggle,
        &QPushButton::clicked,
        this,
        &StrategyLabWidget::toggleManagedAlert);
    connect(
        removeAlert,
        &QPushButton::clicked,
        this,
        &StrategyLabWidget::removeManagedAlert);
    managedAlertsTable_ = new QTableWidget(0, 5, alerts);
    managedAlertsTable_->setHorizontalHeaderLabels({
        tr("Name"),
        tr("Symbol"),
        tr("Frequency"),
        tr("Expires"),
        tr("Status"),
    });
    managedAlertsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    managedAlertsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    managedAlertsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    managedAlertsTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    managedAlertsTable_->horizontalHeader()->setStretchLastSection(true);
    alertLayout->addWidget(managedAlertsTable_);
    alertTable_ = new QTableWidget(0, 4, alerts);
    alertTable_->setHorizontalHeaderLabels({
        tr("Triggered UTC"),
        tr("Bar UTC"),
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
    RuleGroupControls& controls,
    const StrategyComparison defaultComparison) {
    auto* group = new QGroupBox(title, this);
    auto* layout = new QVBoxLayout(group);
    auto* actions = new QHBoxLayout;
    controls.match = new QComboBox(group);
    controls.match->addItem(tr("Match all conditions"), static_cast<int>(ConditionMatch::All));
    controls.match->addItem(tr("Match any condition"), static_cast<int>(ConditionMatch::Any));
    auto* add = new QPushButton(tr("Add condition"), group);
    controls.remove = new QPushButton(tr("Remove selected"), group);
    actions->addWidget(controls.match, 1);
    actions->addWidget(add);
    actions->addWidget(controls.remove);
    layout->addLayout(actions);

    controls.table = new QTableWidget(0, 8, group);
    controls.table->setHorizontalHeaderLabels({
        tr("Left"),
        tr("TF"),
        tr("Period"),
        tr("Comparison"),
        tr("Right"),
        tr("TF"),
        tr("Period"),
        tr("Constant"),
    });
    controls.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    controls.table->setSelectionMode(QAbstractItemView::SingleSelection);
    controls.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    controls.table->verticalHeader()->setVisible(false);
    controls.table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    controls.table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(controls.table);

    const StrategyCondition initial{
        .left = {.field = StrategyField::Close, .period = 20},
        .comparison = defaultComparison,
        .right = StrategyOperand{
            .field = StrategyField::SimpleMovingAverage,
            .period = 20,
        },
    };
    addRule(controls, initial);
    connect(
        add,
        &QPushButton::clicked,
        this,
        [this, &controls] {
            if (controls.table->rowCount() >= 16) {
                emit statusMessage(tr("A rule group supports at most 16 conditions."));
                return;
            }
            addRule(
                controls,
                {
                    .left = {.field = StrategyField::Close, .period = 20},
                    .comparison = StrategyComparison::GreaterThan,
                    .constant = 0.0,
                });
            emit workspaceChanged();
        });
    connect(
        controls.remove,
        &QPushButton::clicked,
        this,
        [this, &controls] { removeSelectedRule(controls); });
    connect(
        controls.match,
        &QComboBox::currentIndexChanged,
        this,
        &StrategyLabWidget::workspaceChanged);
    return group;
}

void StrategyLabWidget::addRule(
    RuleGroupControls& controls,
    const StrategyCondition& initial) {
    const auto row = controls.table->rowCount();
    controls.table->insertRow(row);
    RuleControls widgets;
    widgets.leftField = new QComboBox(controls.table);
    populateFieldCombo(*widgets.leftField, false);
    widgets.leftTimeframe = new QComboBox(controls.table);
    populateTimeframeCombo(*widgets.leftTimeframe);
    widgets.leftPeriod = new QSpinBox(controls.table);
    widgets.leftPeriod->setRange(1, 500);
    widgets.comparison = new QComboBox(controls.table);
    for (const auto comparison : {
             StrategyComparison::GreaterThan,
             StrategyComparison::LessThan,
             StrategyComparison::CrossesAbove,
             StrategyComparison::CrossesBelow,
         }) {
        widgets.comparison->addItem(
            strategyComparisonLabel(comparison),
            static_cast<int>(comparison));
    }
    widgets.rightField = new QComboBox(controls.table);
    populateFieldCombo(*widgets.rightField, true);
    widgets.rightTimeframe = new QComboBox(controls.table);
    populateTimeframeCombo(*widgets.rightTimeframe);
    widgets.rightPeriod = new QSpinBox(controls.table);
    widgets.rightPeriod->setRange(1, 500);
    widgets.constant = new QDoubleSpinBox(controls.table);
    widgets.constant->setRange(-1.0e12, 1.0e12);
    widgets.constant->setDecimals(6);

    setComboData(*widgets.leftField, static_cast<int>(initial.left.field));
    setComboData(
        *widgets.leftTimeframe,
        initial.left.timeframe
            ? static_cast<int>(*initial.left.timeframe)
            : -1);
    widgets.leftPeriod->setValue(static_cast<int>(initial.left.period));
    setComboData(
        *widgets.comparison,
        static_cast<int>(initial.comparison));
    if (initial.right) {
        setComboData(
            *widgets.rightField,
            static_cast<int>(initial.right->field));
        setComboData(
            *widgets.rightTimeframe,
            initial.right->timeframe
                ? static_cast<int>(*initial.right->timeframe)
                : -1);
        widgets.rightPeriod->setValue(
            static_cast<int>(initial.right->period));
    } else {
        setComboData(*widgets.rightField, -1);
        widgets.constant->setValue(initial.constant);
    }

    controls.table->setCellWidget(row, 0, widgets.leftField);
    controls.table->setCellWidget(row, 1, widgets.leftTimeframe);
    controls.table->setCellWidget(row, 2, widgets.leftPeriod);
    controls.table->setCellWidget(row, 3, widgets.comparison);
    controls.table->setCellWidget(row, 4, widgets.rightField);
    controls.table->setCellWidget(row, 5, widgets.rightTimeframe);
    controls.table->setCellWidget(row, 6, widgets.rightPeriod);
    controls.table->setCellWidget(row, 7, widgets.constant);

    const auto refresh =
        [this,
         leftField = widgets.leftField,
         leftTimeframe = widgets.leftTimeframe,
         leftPeriod = widgets.leftPeriod,
         comparison = widgets.comparison,
         rightField = widgets.rightField,
         rightTimeframe = widgets.rightTimeframe,
         rightPeriod = widgets.rightPeriod,
         constant = widgets.constant](int) {
            RuleControls rowControls{
                leftField,
                leftTimeframe,
                leftPeriod,
                comparison,
                rightField,
                rightTimeframe,
                rightPeriod,
                constant,
            };
            refreshRuleControls(rowControls);
        };
    connect(
        widgets.leftField,
        &QComboBox::currentIndexChanged,
        this,
        refresh);
    connect(
        widgets.rightField,
        &QComboBox::currentIndexChanged,
        this,
        refresh);
    const auto persistRule = [this] { emit workspaceChanged(); };
    connect(
        widgets.leftField,
        &QComboBox::currentIndexChanged,
        this,
        persistRule);
    connect(
        widgets.leftPeriod,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        persistRule);
    connect(
        widgets.leftTimeframe,
        &QComboBox::currentIndexChanged,
        this,
        persistRule);
    connect(
        widgets.comparison,
        &QComboBox::currentIndexChanged,
        this,
        persistRule);
    connect(
        widgets.rightField,
        &QComboBox::currentIndexChanged,
        this,
        persistRule);
    connect(
        widgets.rightPeriod,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        persistRule);
    connect(
        widgets.rightTimeframe,
        &QComboBox::currentIndexChanged,
        this,
        persistRule);
    connect(
        widgets.constant,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        persistRule);
    refresh(0);
    controls.table->selectRow(row);
    controls.remove->setEnabled(controls.table->rowCount() > 1);
}

void StrategyLabWidget::removeSelectedRule(RuleGroupControls& controls) {
    if (controls.table->rowCount() <= 1) {
        emit statusMessage(tr("A rule group requires at least one condition."));
        return;
    }
    auto row = controls.table->currentRow();
    if (row < 0) {
        row = controls.table->rowCount() - 1;
    }
    controls.table->removeRow(row);
    controls.remove->setEnabled(controls.table->rowCount() > 1);
    emit workspaceChanged();
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

void StrategyLabWidget::populateTimeframeCombo(QComboBox& combo) {
    combo.addItem(tr("Chart"), -1);
    for (const auto timeframe : {
             Timeframe::OneMinute,
             Timeframe::FiveMinutes,
             Timeframe::FifteenMinutes,
             Timeframe::OneHour,
             Timeframe::OneDay,
         }) {
        combo.addItem(
            timeframe == Timeframe::OneMinute
                ? QStringLiteral("1m")
                : timeframe == Timeframe::FiveMinutes
                    ? QStringLiteral("5m")
                    : timeframe == Timeframe::FifteenMinutes
                        ? QStringLiteral("15m")
                        : timeframe == Timeframe::OneHour
                            ? QStringLiteral("1h")
                            : QStringLiteral("1D"),
            static_cast<int>(timeframe));
    }
}

void StrategyLabWidget::refreshRuleControls(RuleControls& controls) {
    controls.leftPeriod->setEnabled(periodField(comboField(*controls.leftField)));
    const auto constant = controls.rightField->currentData().toInt() < 0;
    controls.constant->setEnabled(constant);
    controls.rightTimeframe->setEnabled(!constant);
    controls.rightPeriod->setEnabled(
        !constant && periodField(comboField(*controls.rightField)));
}

StrategyCondition StrategyLabWidget::condition(
    const RuleGroupControls& controls,
    const int row) const {
    const RuleControls widgets{
        qobject_cast<QComboBox*>(controls.table->cellWidget(row, 0)),
        qobject_cast<QComboBox*>(controls.table->cellWidget(row, 1)),
        qobject_cast<QSpinBox*>(controls.table->cellWidget(row, 2)),
        qobject_cast<QComboBox*>(controls.table->cellWidget(row, 3)),
        qobject_cast<QComboBox*>(controls.table->cellWidget(row, 4)),
        qobject_cast<QComboBox*>(controls.table->cellWidget(row, 5)),
        qobject_cast<QSpinBox*>(controls.table->cellWidget(row, 6)),
        qobject_cast<QDoubleSpinBox*>(controls.table->cellWidget(row, 7)),
    };
    StrategyCondition result{
        .left = {
            .field = comboField(*widgets.leftField),
            .period =
                static_cast<std::uint32_t>(widgets.leftPeriod->value()),
            .timeframe =
                widgets.leftTimeframe->currentData().toInt() >= 0
                    ? std::optional<Timeframe>{
                          static_cast<Timeframe>(
                              widgets.leftTimeframe
                                  ->currentData()
                                  .toInt())}
                    : std::nullopt,
        },
        .comparison = static_cast<StrategyComparison>(
            widgets.comparison->currentData().toInt()),
        .constant = widgets.constant->value(),
    };
    if (widgets.rightField->currentData().toInt() >= 0) {
        result.right = StrategyOperand{
            .field = comboField(*widgets.rightField),
            .period =
                static_cast<std::uint32_t>(widgets.rightPeriod->value()),
            .timeframe =
                widgets.rightTimeframe->currentData().toInt() >= 0
                    ? std::optional<Timeframe>{
                          static_cast<Timeframe>(
                              widgets.rightTimeframe
                                  ->currentData()
                                  .toInt())}
                    : std::nullopt,
        };
    }
    return result;
}

ConditionGroup StrategyLabWidget::conditionGroup(
    const RuleGroupControls& controls) const {
    ConditionGroup group{
        .match = static_cast<ConditionMatch>(
            controls.match->currentData().toInt()),
    };
    group.conditions.reserve(
        static_cast<std::size_t>(controls.table->rowCount()));
    for (auto row = 0; row < controls.table->rowCount(); ++row) {
        group.conditions.push_back(condition(controls, row));
    }
    return group;
}

StrategyDefinition StrategyLabWidget::currentStrategy() const {
    auto id = strategyLibrary_->currentData().toString();
    if (id.isEmpty()) {
        id = QStringLiteral("strategy-lab");
    }
    auto name = strategyName_->text().trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("Strategy Lab");
    }
    return {
        .id = std::move(id),
        .name = std::move(name),
        .entry = conditionGroup(entryControls_),
        .exit = conditionGroup(exitControls_),
    };
}

TimeframeSeries StrategyLabWidget::loadAdditionalSeries(
    const StrategyDefinition& strategy,
    const QString& symbol,
    QStringList& missing) const {
    std::set<Timeframe> required;
    const auto collect =
        [&](const ConditionGroup& group) {
        for (const auto& condition : group.conditions) {
            if (condition.left.timeframe &&
                *condition.left.timeframe != timeframe_) {
                required.insert(*condition.left.timeframe);
            }
            if (condition.right &&
                condition.right->timeframe &&
                *condition.right->timeframe != timeframe_) {
                required.insert(*condition.right->timeframe);
            }
        }
    };
    collect(strategy.entry);
    collect(strategy.exit);

    TimeframeSeries result;
    if (required.empty()) {
        return result;
    }
    if (metadata_.appliedAdjustmentMode != PriceAdjustmentMode::Raw) {
        missing.push_back(
            tr("Switch the primary chart to Raw before using cached "
               "multi-timeframe conditions."));
        return result;
    }
    if (!historyStore_ || !historyStore_->isOpen()) {
        missing.push_back(tr("The historical cache is unavailable."));
        return result;
    }
    for (const auto timeframe : required) {
        auto cached = historyStore_->loadLatestSeries(symbol, timeframe);
        if (!cached.ok()) {
            missing.push_back(
                tr("%1 %2: %3")
                    .arg(
                        normalizeWatchlistSymbol(symbol),
                        timeframeText(timeframe),
                        cached.error));
            continue;
        }
        auto completed =
            analysisBars(cached.bars, timeframe, cached.metadata);
        if (completed.empty()) {
            missing.push_back(
                tr("%1 %2 has no completed cached bars.")
                    .arg(
                        normalizeWatchlistSymbol(symbol),
                        timeframeText(timeframe)));
            continue;
        }
        result.emplace(timeframe, std::move(completed));
    }
    return result;
}

void StrategyLabWidget::applyStrategy(const StrategyDefinition& strategy) {
    const auto apply =
        [this](RuleGroupControls& controls, const ConditionGroup& rule) {
            controls.table->setRowCount(0);
            setComboData(*controls.match, static_cast<int>(rule.match));
            for (const auto& condition : rule.conditions) {
                addRule(controls, condition);
            }
            controls.remove->setEnabled(controls.table->rowCount() > 1);
        };
    strategyName_->setText(strategy.name);
    apply(entryControls_, strategy.entry);
    apply(exitControls_, strategy.exit);
    const auto libraryIndex = strategyLibrary_->findData(strategy.id);
    if (libraryIndex >= 0) {
        strategyLibrary_->setCurrentIndex(libraryIndex);
    }
}

void StrategyLabWidget::saveNamedStrategy() {
    auto strategy = currentStrategy();
    strategy.name = strategyName_->text().trimmed();
    if (strategy.name.isEmpty()) {
        emit statusMessage(tr("Enter a strategy name before saving."));
        return;
    }

    const auto selectedId = strategyLibrary_->currentData().toString();
    const auto duplicateName = std::ranges::find_if(
        savedStrategies_,
        [&](const StrategyDefinition& candidate) {
            return candidate.id != selectedId &&
                   candidate.name.compare(
                       strategy.name,
                       Qt::CaseInsensitive) == 0;
        });
    if (duplicateName != savedStrategies_.end()) {
        emit statusMessage(
            tr("A saved strategy already uses the name “%1”.")
                .arg(strategy.name));
        return;
    }
    auto existing = std::ranges::find_if(
        savedStrategies_,
        [&](const StrategyDefinition& candidate) {
            return (!selectedId.isEmpty() && candidate.id == selectedId) ||
                   candidate.name.compare(
                       strategy.name,
                       Qt::CaseInsensitive) == 0;
        });
    if (existing != savedStrategies_.end()) {
        strategy.id = existing->id;
        *existing = strategy;
    } else {
        strategy.id =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        savedStrategies_.push_back(strategy);
    }
    std::ranges::sort(
        savedStrategies_,
        {},
        [](const StrategyDefinition& candidate) {
            return candidate.name.toCaseFolded();
        });
    refreshStrategyLibrary();
    const auto index = strategyLibrary_->findData(strategy.id);
    if (index >= 0) {
        strategyLibrary_->setCurrentIndex(index);
    }
    emit statusMessage(tr("Saved strategy “%1”.").arg(strategy.name));
    emit workspaceChanged();
}

void StrategyLabWidget::loadNamedStrategy() {
    const auto identity = strategyLibrary_->currentData().toString();
    const auto found = std::ranges::find(
        savedStrategies_,
        identity,
        &StrategyDefinition::id);
    if (found == savedStrategies_.end()) {
        emit statusMessage(tr("Select a saved strategy to load."));
        return;
    }
    applyStrategy(*found);
    emit statusMessage(tr("Loaded strategy “%1”.").arg(found->name));
    emit workspaceChanged();
}

void StrategyLabWidget::deleteNamedStrategy() {
    const auto identity = strategyLibrary_->currentData().toString();
    const auto found = std::ranges::find(
        savedStrategies_,
        identity,
        &StrategyDefinition::id);
    if (found == savedStrategies_.end()) {
        emit statusMessage(tr("Select a saved strategy to delete."));
        return;
    }
    if (QMessageBox::question(
            this,
            tr("Delete strategy"),
            tr("Delete the local strategy “%1”?").arg(found->name)) !=
        QMessageBox::Yes) {
        return;
    }
    savedStrategies_.erase(found);
    refreshStrategyLibrary();
    strategyLibrary_->setCurrentIndex(-1);
    strategyName_->setText(tr("Strategy Lab"));
    emit statusMessage(tr("Deleted the saved strategy."));
    emit workspaceChanged();
}

void StrategyLabWidget::refreshStrategyLibrary() {
    const auto selected = strategyLibrary_->currentData().toString();
    strategyLibrary_->clear();
    for (const auto& strategy : savedStrategies_) {
        strategyLibrary_->addItem(strategy.name, strategy.id);
    }
    const auto index = strategyLibrary_->findData(selected);
    if (index >= 0) {
        strategyLibrary_->setCurrentIndex(index);
    }
}

void StrategyLabWidget::setCurrentSeries(
    QString symbol,
    const Timeframe timeframe,
    QString provider,
    Bars bars,
    MarketDataMetadata metadata,
    Bars rawCacheBars) {
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
            std::max(1, replayWarmup_->value())));

    if (historyStore_ && historyStore_->isOpen() &&
        metadata_.deliveryMode == DataDeliveryMode::Polled &&
        !bars_.empty()) {
        const auto& cacheBars =
            rawCacheBars.empty() ? bars_ : rawCacheBars;
        auto cacheMetadata = metadata_;
        cacheMetadata.requestedAdjustmentMode = PriceAdjustmentMode::Raw;
        cacheMetadata.appliedAdjustmentMode = PriceAdjustmentMode::Raw;
        cacheMetadata.adjustmentWarning.clear();
        const auto completed =
            analysisBars(cacheBars, timeframe_, cacheMetadata);
        if (!completed.empty()) {
            const auto error = historyStore_->upsertSeries(
                provider_,
                symbol_,
                timeframe_,
                completed,
                cacheMetadata);
            if (!error.isEmpty()) {
                emit statusMessage(
                    tr("History cache did not update: %1").arg(error));
            }
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

void StrategyLabWidget::setResearchEvents(
    std::vector<ResearchEvent> events) {
    researchEvents_ = std::move(events);
    evaluateEventReminders();
}

void StrategyLabWidget::restoreSettings(QSettings& settings) {
    const auto library = deserializeStrategyLibrary(
        settings.value(QStringLiteral("strategyLab/library")).toByteArray());
    if (library.ok()) {
        savedStrategies_ = library.strategies;
        refreshStrategyLibrary();
    }
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
    holdoutEnabled_->setChecked(
        settings.value(QStringLiteral("strategyLab/holdoutEnabled"), true)
            .toBool());
    holdoutPercent_->setValue(
        settings.value(QStringLiteral("strategyLab/holdoutPercent"), 30)
            .toInt());
    replayWarmup_->setValue(
        settings.value(QStringLiteral("strategyLab/replayWarmup"), 100).toInt());
    alertEnabled_->setChecked(
        settings.value(QStringLiteral("strategyLab/alertEnabled"), false).toBool());
    eventReminderEnabled_->setChecked(
        settings.value(QStringLiteral("strategyLab/eventReminders"), false)
            .toBool());
    eventReminderLeadDays_->setValue(
        settings.value(QStringLiteral("strategyLab/eventLeadDays"), 7)
            .toInt());
    const auto alerts = deserializeAlertWorkspace(
        settings.value(QStringLiteral("strategyLab/alerts")).toByteArray());
    if (alerts.ok()) {
        managedAlerts_ = alerts.workspace.alerts;
        alertEngine_.restoreAuditLog(alerts.workspace.history);
        refreshManagedAlerts();
        alertTable_->setRowCount(0);
        for (const auto& trigger : alertEngine_.auditLog()) {
            appendAlert(trigger, false);
        }
    }
}

void StrategyLabWidget::saveSettings(QSettings& settings) const {
    settings.setValue(
        QStringLiteral("strategyLab/strategy"),
        serializeStrategy(currentStrategy()));
    settings.setValue(
        QStringLiteral("strategyLab/library"),
        serializeStrategyLibrary(savedStrategies_));
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
        QStringLiteral("strategyLab/holdoutEnabled"),
        holdoutEnabled_->isChecked());
    settings.setValue(
        QStringLiteral("strategyLab/holdoutPercent"),
        holdoutPercent_->value());
    settings.setValue(
        QStringLiteral("strategyLab/replayWarmup"),
        replayWarmup_->value());
    settings.setValue(
        QStringLiteral("strategyLab/alertEnabled"),
        alertEnabled_->isChecked());
    settings.setValue(
        QStringLiteral("strategyLab/eventReminders"),
        eventReminderEnabled_->isChecked());
    settings.setValue(
        QStringLiteral("strategyLab/eventLeadDays"),
        eventReminderLeadDays_->value());
    settings.setValue(
        QStringLiteral("strategyLab/alerts"),
        serializeAlertWorkspace({
            .alerts = managedAlerts_,
            .history = alertEngine_.auditLog(),
        }));
}

void StrategyLabWidget::restoreChartIfReplaying() {
    stopReplay(true);
}

void StrategyLabWidget::runCurrentBacktest() {
    const auto completed = analysisBars(bars_, timeframe_, metadata_);
    if (completed.empty()) {
        metricsLabel_->setText(tr("No valid series is loaded."));
        return;
    }
    const BacktestParameters parameters{
        .initialCapital = initialCapital_->value(),
        .allocationPercent = allocationPercent_->value(),
        .commissionPerSide = commission_->value(),
        .slippageBasisPoints = slippage_->value(),
        .allowFractionalShares = fractionalShares_->isChecked(),
    };
    const auto strategy = currentStrategy();
    QStringList missingSeries;
    const auto additionalSeries = loadAdditionalSeries(
        strategy,
        symbol_,
        missingSeries);
    if (!missingSeries.isEmpty()) {
        metricsLabel_->setText(
            tr("Backtest unavailable: %1")
                .arg(missingSeries.join(QStringLiteral(" "))));
        validationLabel_->setText(
            tr("Multi-timeframe inputs never fall back to the chart "
               "timeframe."));
        return;
    }
    const auto result = runBacktest(
        completed,
        timeframe_,
        additionalSeries,
        strategy,
        parameters);
    showBacktest(result);
    if (!result.ok() || !holdoutEnabled_->isChecked()) {
        validationLabel_->setText(
            holdoutEnabled_->isChecked()
                ? tr("Holdout validation was not run because the full backtest failed.")
                : tr("Chronological holdout validation is disabled."));
        return;
    }
    const auto validation = runHoldoutBacktest(
        completed,
        timeframe_,
        additionalSeries,
        strategy,
        parameters,
        static_cast<double>(holdoutPercent_->value()));
    if (!validation.ok()) {
        validationLabel_->setText(
            tr("Holdout validation unavailable: %1").arg(validation.error));
        return;
    }
    validationLabel_->setText(
        tr("Chronological split %1 UTC · Training return %2% (%3 trades) · "
           "Holdout return %4% (%5 trades) · Holdout buy-and-hold %6%. "
           "Earlier bars warm indicators only; positions do not cross the split.")
            .arg(utcDateTime(validation.splitTimestamp))
            .arg(number(validation.training.totalReturnPercent, 2))
            .arg(validation.training.trades.size())
            .arg(number(validation.holdout.totalReturnPercent, 2))
            .arg(validation.holdout.trades.size())
            .arg(number(validation.holdout.buyAndHoldReturnPercent, 2)));
}

void StrategyLabWidget::runRobustness() {
    for (auto* table : {
             walkForwardTable_,
             parameterTable_,
             regimeTable_,
             batchTable_,
         }) {
        table->setRowCount(0);
    }
    const auto completed = analysisBars(bars_, timeframe_, metadata_);
    if (completed.empty()) {
        robustnessSummary_->setText(
            tr("Professional validation unavailable: no completed series."));
        return;
    }
    if (metadata_.appliedAdjustmentMode != PriceAdjustmentMode::Raw) {
        robustnessSummary_->setText(
            tr("Professional validation uses compatible raw cached histories. "
               "Switch Price basis to Raw, then run again."));
        return;
    }
    const BacktestParameters parameters{
        .initialCapital = initialCapital_->value(),
        .allocationPercent = allocationPercent_->value(),
        .commissionPerSide = commission_->value(),
        .slippageBasisPoints = slippage_->value(),
        .allowFractionalShares = fractionalShares_->isChecked(),
    };
    const auto strategy = currentStrategy();
    QStringList missingSeries;
    const auto additionalSeries = loadAdditionalSeries(
        strategy,
        symbol_,
        missingSeries);
    if (!missingSeries.isEmpty()) {
        robustnessSummary_->setText(
            tr("Professional validation unavailable: %1")
                .arg(missingSeries.join(QStringLiteral(" "))));
        return;
    }
    const auto base = runBacktest(
        completed,
        timeframe_,
        additionalSeries,
        strategy,
        parameters);
    if (!base.ok()) {
        robustnessSummary_->setText(
            tr("Professional validation failed: %1").arg(base.error));
        return;
    }

    const auto walkForward = runWalkForwardAnalysis(
        completed,
        timeframe_,
        additionalSeries,
        strategy,
        parameters,
        static_cast<std::size_t>(robustnessFolds_->value()));
    if (walkForward.ok()) {
        walkForwardTable_->setRowCount(
            static_cast<int>(walkForward.folds.size()));
        for (std::size_t index = 0;
             index < walkForward.folds.size();
             ++index) {
            const auto& fold = walkForward.folds[index];
            const QStringList values{
                QString::number(fold.index),
                utcDateTime(fold.startTimestamp),
                utcDateTime(fold.endTimestamp),
                number(fold.result.totalReturnPercent, 2) +
                    QStringLiteral("%"),
                number(fold.result.maximumDrawdownPercent, 2) +
                    QStringLiteral("%"),
                QString::number(fold.result.trades.size()),
                fold.result.sharpeRatio
                    ? number(*fold.result.sharpeRatio, 2)
                    : QStringLiteral("—"),
            };
            for (int column = 0; column < values.size(); ++column) {
                walkForwardTable_->setItem(
                    static_cast<int>(index),
                    column,
                    new QTableWidgetItem(values[column]));
            }
        }
    }

    const auto monteCarlo = runTradeMonteCarlo(
        base,
        static_cast<std::size_t>(
            robustnessSimulations_->value()));

    ParameterStabilityAnalysis stability;
    if (const auto center = firstStrategyPeriod(strategy)) {
        std::vector<std::uint32_t> periods{
            std::max<std::uint32_t>(1, *center / 2),
            std::max<std::uint32_t>(1, *center * 3 / 4),
            *center,
            std::min<std::uint32_t>(500, *center * 5 / 4),
            std::min<std::uint32_t>(500, *center * 3 / 2),
        };
        std::ranges::sort(periods);
        periods.erase(
            std::unique(periods.begin(), periods.end()),
            periods.end());
        stability = runPrimaryPeriodStability(
            completed,
            timeframe_,
            additionalSeries,
            strategy,
            parameters,
            periods);
    } else {
        stability.error =
            tr("No period-bearing operand is available.");
    }
    if (stability.ok()) {
        parameterTable_->setRowCount(
            static_cast<int>(stability.points.size()));
        for (std::size_t index = 0;
             index < stability.points.size();
             ++index) {
            const auto& point = stability.points[index];
            const auto profitFactor =
                point.result.profitFactor
                    ? (std::isinf(*point.result.profitFactor)
                           ? QStringLiteral("∞")
                           : number(*point.result.profitFactor, 2))
                    : QStringLiteral("—");
            const QStringList values{
                QString::number(point.period),
                number(point.result.totalReturnPercent, 2) +
                    QStringLiteral("%"),
                number(point.result.maximumDrawdownPercent, 2) +
                    QStringLiteral("%"),
                QString::number(point.result.trades.size()),
                number(point.result.winRatePercent, 1) +
                    QStringLiteral("%"),
                profitFactor,
            };
            for (int column = 0; column < values.size(); ++column) {
                parameterTable_->setItem(
                    static_cast<int>(index),
                    column,
                    new QTableWidgetItem(values[column]));
            }
        }
    }

    const auto regimes = analyzeTradeRegimes(completed, base);
    if (regimes.ok()) {
        regimeTable_->setRowCount(
            static_cast<int>(regimes.regimes.size()));
        for (std::size_t index = 0;
             index < regimes.regimes.size();
             ++index) {
            const auto& result = regimes.regimes[index];
            const QStringList values{
                marketRegimeLabel(result.regime),
                QString::number(result.trades),
                number(result.winRatePercent, 1) +
                    QStringLiteral("%"),
                number(result.averageReturnPercent, 2) +
                    QStringLiteral("%"),
                number(result.netProfitLoss, 2),
            };
            for (int column = 0; column < values.size(); ++column) {
                regimeTable_->setItem(
                    static_cast<int>(index),
                    column,
                    new QTableWidgetItem(values[column]));
            }
        }
    }

    std::vector<StrategyBatchSeries> batchInputs;
    for (const auto& symbol : watchlistSymbols_) {
        auto normalized = normalizeWatchlistSymbol(symbol);
        if (normalized.isEmpty()) {
            continue;
        }
        StrategyBatchSeries input{
            .symbol = normalized,
            .timeframe = timeframe_,
        };
        if (normalized == normalizeWatchlistSymbol(symbol_)) {
            input.provider = provider_;
            input.bars = completed;
            input.additionalSeries = additionalSeries;
        } else if (historyStore_ && historyStore_->isOpen()) {
            auto cached =
                historyStore_->loadLatestSeries(normalized, timeframe_);
            if (cached.ok()) {
                input.provider = cached.key.provider;
                input.bars = analysisBars(
                    cached.bars,
                    timeframe_,
                    cached.metadata);
                QStringList missing;
                input.additionalSeries = loadAdditionalSeries(
                    strategy,
                    normalized,
                    missing);
            } else {
                input.provider = tr("Cache unavailable");
            }
        }
        batchInputs.push_back(std::move(input));
    }
    const auto batch = runStrategyBatch(
        batchInputs,
        strategy,
        parameters);
    batchTable_->setRowCount(static_cast<int>(batch.size()));
    for (std::size_t index = 0; index < batch.size(); ++index) {
        const auto& item = batch[index];
        const auto ok = item.result.ok();
        const QStringList values{
            item.symbol,
            item.provider,
            ok ? tr("Complete") : item.result.error,
            ok
                ? number(item.result.totalReturnPercent, 2) +
                      QStringLiteral("%")
                : QStringLiteral("—"),
            ok
                ? number(item.result.maximumDrawdownPercent, 2) +
                      QStringLiteral("%")
                : QStringLiteral("—"),
            ok
                ? QString::number(item.result.trades.size())
                : QStringLiteral("—"),
            ok && item.result.sharpeRatio
                ? number(*item.result.sharpeRatio, 2)
                : QStringLiteral("—"),
        };
        for (int column = 0; column < values.size(); ++column) {
            batchTable_->setItem(
                static_cast<int>(index),
                column,
                new QTableWidgetItem(values[column]));
        }
    }

    QStringList summaries;
    summaries.push_back(
        tr("Base %1 trades · return %2%")
            .arg(base.trades.size())
            .arg(number(base.totalReturnPercent, 2)));
    summaries.push_back(
        walkForward.ok()
            ? tr("Walk-forward median %1% · positive folds %2% · worst %3%")
                  .arg(number(walkForward.medianReturnPercent, 2))
                  .arg(number(walkForward.positiveFoldPercent, 1))
                  .arg(number(walkForward.worstFoldReturnPercent, 2))
            : tr("Walk-forward unavailable: %1")
                  .arg(walkForward.error));
    summaries.push_back(
        monteCarlo.ok()
            ? tr("Monte Carlo %1 paths / %2 trades · median %3% · 5th %4% · "
                 "95th drawdown %5% · loss probability %6%")
                  .arg(monteCarlo.simulationCount)
                  .arg(monteCarlo.tradeCount)
                  .arg(number(
                      monteCarlo.medianTerminalReturnPercent,
                      2))
                  .arg(number(
                      monteCarlo.percentile5TerminalReturnPercent,
                      2))
                  .arg(number(
                      monteCarlo
                          .percentile95MaximumDrawdownPercent,
                      2))
                  .arg(number(
                      monteCarlo.probabilityOfLossPercent,
                      1))
            : tr("Monte Carlo unavailable: %1")
                  .arg(monteCarlo.error));
    summaries.push_back(
        stability.ok()
            ? tr("%1 stability across %2 periods; no automatic selection.")
                  .arg(stability.operandLabel)
                  .arg(stability.points.size())
            : tr("Parameter stability unavailable: %1")
                  .arg(stability.error));
    summaries.push_back(
        tr("Regime-attributed %1/%2 trades · cached symbols %3.")
            .arg(base.trades.size() - regimes.unavailableTrades)
            .arg(base.trades.size())
            .arg(batch.size()));
    robustnessSummary_->setText(
        summaries.join(QStringLiteral(" · ")));
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
    const auto optionalNumber = [](const std::optional<double>& value) {
        return value ? number(*value, 2) : QStringLiteral("—");
    };
    metricsLabel_->setText(
        tr("Final equity %1 · Net %2 (%3%) · Max drawdown %4% · "
           "%5 trades · Win rate %6% · Profit factor %7 · Exposure %8% · "
           "Buy-and-hold %9% · CAGR %10% · Sharpe %11 · Sortino %12 · "
           "Calmar %13 · Average trade %14% · Longest underwater %15 days")
            .arg(number(result.finalEquity, 2))
            .arg(number(result.netProfit, 2))
            .arg(number(result.totalReturnPercent, 2))
            .arg(number(result.maximumDrawdownPercent, 2))
            .arg(result.trades.size())
            .arg(number(result.winRatePercent, 1))
            .arg(profitFactor)
            .arg(number(result.exposurePercent, 1))
            .arg(number(result.buyAndHoldReturnPercent, 2))
            .arg(optionalNumber(result.compoundAnnualGrowthRatePercent))
            .arg(optionalNumber(result.sharpeRatio))
            .arg(optionalNumber(result.sortinoRatio))
            .arg(optionalNumber(result.calmarRatio))
            .arg(number(result.averageTradeReturnPercent, 2))
            .arg(number(result.longestUnderwaterDays, 1)));
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
            number(trade.returnPercent, 2) + QStringLiteral("%"),
            number(trade.maximumAdverseExcursionPercent, 2) +
                QStringLiteral("%"),
            number(trade.maximumFavorableExcursionPercent, 2) +
                QStringLiteral("%"),
            QString::number(trade.barsHeld),
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
    const auto completed = analysisBars(bars_, timeframe_, metadata_);
    if (completed.empty()) {
        replayStatus_->setText(tr("No valid series is loaded."));
        return;
    }
    replayTimer_->stop();
    replayPlay_->setText(tr("Play"));
    const auto error = replay_.reset(
        completed,
        std::min(
            completed.size(),
            static_cast<std::size_t>(replayWarmup_->value())));
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
    const auto strategy = currentStrategy();
    candidates.reserve(static_cast<std::size_t>(watchlistSymbols_.size()));
    for (const auto& symbol : watchlistSymbols_) {
        auto cached = historyStore_->loadLatestSeries(symbol, timeframe_);
        auto completed =
            cached.ok()
                ? analysisBars(cached.bars, timeframe_, cached.metadata)
                : Bars{};
        QStringList missing;
        auto additional = loadAdditionalSeries(
            strategy,
            symbol,
            missing);
        loadErrors.push_back(
            !cached.error.isEmpty()
                ? cached.error
                : missing.join(QStringLiteral(" ")));
        candidates.push_back({
            .symbol = symbol,
            .provider =
                cached.ok() ? cached.key.provider : QStringLiteral("Cache"),
            .bars = std::move(completed),
            .timeframe = timeframe_,
            .additionalSeries = std::move(additional),
        });
    }
    auto results = scanLatest(candidates, strategy.entry);
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
    evaluateManagedAlerts(true);
}

void StrategyLabWidget::evaluateManagedAlerts(const bool automatic) {
    if ((automatic &&
         (!alertEnabled_ || !alertEnabled_->isChecked())) ||
        symbol_.isEmpty() || bars_.empty()) {
        return;
    }
    const auto completed = analysisBars(bars_, timeframe_, metadata_);
    if (completed.empty()) {
        return;
    }
    if (managedAlerts_.empty()) {
        if (!automatic) {
            emit statusMessage(
                tr("Save at least one alert before evaluating."));
        }
        return;
    }
    const auto normalizedSymbol = normalizeWatchlistSymbol(symbol_);
    const auto evaluatedAtUtc = QDateTime::currentSecsSinceEpoch();
    auto evaluated = std::size_t{};
    auto triggered = std::size_t{};
    auto expired = std::size_t{};
    for (const auto& alert : managedAlerts_) {
        if (normalizeWatchlistSymbol(alert.symbol) != normalizedSymbol) {
            continue;
        }
        ++evaluated;
        const StrategyDefinition dependencyStrategy{
            .id = alert.id,
            .name =
                alert.name.trimmed().isEmpty()
                    ? alert.id
                    : alert.name,
            .entry = alert.condition,
            .exit = alert.condition,
        };
        QStringList missing;
        const auto additional = loadAdditionalSeries(
            dependencyStrategy,
            normalizedSymbol,
            missing);
        if (!missing.isEmpty()) {
            if (!automatic) {
                emit statusMessage(
                    tr("Alert “%1” is unavailable: %2")
                        .arg(
                            dependencyStrategy.name,
                            missing.join(QStringLiteral(" "))));
            }
            continue;
        }
        const auto evaluation =
            alertEngine_.evaluate(
                alert,
                completed,
                timeframe_,
                additional,
                evaluatedAtUtc);
        if (!evaluation.error.isEmpty()) {
            emit statusMessage(
                tr("Alert “%1” failed: %2")
                    .arg(
                        alert.name.trimmed().isEmpty()
                            ? alert.id
                            : alert.name,
                        evaluation.error));
            continue;
        }
        if (evaluation.expired) {
            ++expired;
        }
        if (evaluation.trigger) {
            ++triggered;
            appendAlert(*evaluation.trigger);
        }
    }
    refreshManagedAlerts();
    if (!automatic) {
        emit statusMessage(
            tr("Evaluated %1 alerts for %2 · %3 triggered · %4 expired.")
                .arg(evaluated)
                .arg(normalizedSymbol)
                .arg(triggered)
                .arg(expired));
    }
}

void StrategyLabWidget::addManagedAlert() {
    if (managedAlerts_.size() >= AlertWorkspace::maximumAlerts) {
        emit statusMessage(tr("The local alert workspace already has 64 alerts."));
        return;
    }
    const auto name = alertName_->text().trimmed();
    if (name.isEmpty() || symbol_.isEmpty()) {
        emit statusMessage(tr("An alert name and loaded symbol are required."));
        return;
    }
    const auto duplicate = std::ranges::find_if(
        managedAlerts_,
        [&](const StrategyAlert& alert) {
            return normalizeWatchlistSymbol(alert.symbol) ==
                       normalizeWatchlistSymbol(symbol_) &&
                   alert.name.compare(name, Qt::CaseInsensitive) == 0;
        });
    if (duplicate != managedAlerts_.end()) {
        emit statusMessage(
            tr("An alert named “%1” already exists for %2.")
                .arg(name, normalizeWatchlistSymbol(symbol_)));
        return;
    }
    const auto frequency = static_cast<AlertFrequency>(
        alertFrequency_->currentData().toInt());
    const auto expiresAtUtc =
        alertExpiryEnabled_->isChecked()
            ? alertExpiry_->dateTime().toUTC().toSecsSinceEpoch()
            : std::int64_t{};
    if (expiresAtUtc > 0 &&
        expiresAtUtc <= QDateTime::currentSecsSinceEpoch()) {
        emit statusMessage(tr("Choose an alert expiry in the future."));
        return;
    }
    StrategyAlert alert{
        .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
        .symbol = normalizeWatchlistSymbol(symbol_),
        .condition = currentStrategy().entry,
        .enabled = true,
        .name = name,
        .frequency = frequency,
        .cooldownSeconds =
            frequency == AlertFrequency::Cooldown
                ? static_cast<std::int64_t>(
                      alertCooldownMinutes_->value()) *
                      60
                : 0,
        .expiresAtUtc = expiresAtUtc,
    };
    if (const auto error = validateStrategyAlert(alert); !error.isEmpty()) {
        emit statusMessage(tr("Alert was not saved: %1").arg(error));
        return;
    }
    managedAlerts_.push_back(std::move(alert));
    refreshManagedAlerts();
    emit statusMessage(tr("Saved local alert “%1”.").arg(name));
    emit workspaceChanged();
}

bool StrategyLabWidget::addPriceLevelAlert(
    QString symbol,
    const double price,
    const bool crossesAbove,
    QString label) {
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    label = label.trimmed();
    if (managedAlerts_.size() >= AlertWorkspace::maximumAlerts ||
        symbol.isEmpty() || !std::isfinite(price) || price <= 0.0) {
        emit statusMessage(
            tr("The price-level alert is invalid or the 64-alert limit was reached."));
        return false;
    }
    if (label.isEmpty()) {
        label =
            crossesAbove
                ? tr("Cross above %1").arg(number(price, 4))
                : tr("Cross below %1").arg(number(price, 4));
    }
    if (label.size() > 128) {
        emit statusMessage(tr("Alert names can contain at most 128 characters."));
        return false;
    }
    const auto duplicate = std::ranges::find_if(
        managedAlerts_,
        [&](const StrategyAlert& alert) {
            return normalizeWatchlistSymbol(alert.symbol) == symbol &&
                   alert.name.compare(label, Qt::CaseInsensitive) == 0;
        });
    if (duplicate != managedAlerts_.end()) {
        emit statusMessage(
            tr("An alert named “%1” already exists for %2.")
                .arg(label, symbol));
        return false;
    }

    StrategyAlert alert{
        .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
        .symbol = symbol,
        .condition = {
            .match = ConditionMatch::All,
            .conditions = {{
                .left = {
                    .field = StrategyField::Close,
                    .period = 1,
                },
                .comparison =
                    crossesAbove
                        ? StrategyComparison::CrossesAbove
                        : StrategyComparison::CrossesBelow,
                .right = std::nullopt,
                .constant = price,
            }},
        },
        .enabled = true,
        .name = label,
        .frequency = AlertFrequency::OncePerBar,
    };
    if (const auto error = validateStrategyAlert(alert); !error.isEmpty()) {
        emit statusMessage(tr("Price-level alert was not saved: %1").arg(error));
        return false;
    }
    managedAlerts_.push_back(std::move(alert));
    refreshManagedAlerts();
    emit statusMessage(
        tr("Saved price-level alert “%1” for %2.")
            .arg(label, symbol));
    emit workspaceChanged();
    return true;
}

void StrategyLabWidget::recordExternalAlert(
    AlertTrigger trigger) {
    if (alertEngine_.recordExternalTrigger(trigger)) {
        appendAlert(trigger);
    }
}

void StrategyLabWidget::removeManagedAlert() {
    const auto row = managedAlertsTable_->currentRow();
    if (row < 0 || !managedAlertsTable_->item(row, 0)) {
        emit statusMessage(tr("Select a saved alert to remove."));
        return;
    }
    const auto identity =
        managedAlertsTable_->item(row, 0)->data(Qt::UserRole).toString();
    const auto found = std::ranges::find(
        managedAlerts_,
        identity,
        &StrategyAlert::id);
    if (found == managedAlerts_.end()) {
        return;
    }
    if (QMessageBox::question(
            this,
            tr("Remove alert"),
            tr("Remove the local alert “%1”?")
                .arg(found->name)) != QMessageBox::Yes) {
        return;
    }
    managedAlerts_.erase(found);
    refreshManagedAlerts();
    emit statusMessage(tr("Removed the saved alert."));
    emit workspaceChanged();
}

void StrategyLabWidget::toggleManagedAlert() {
    const auto row = managedAlertsTable_->currentRow();
    if (row < 0 || !managedAlertsTable_->item(row, 0)) {
        emit statusMessage(tr("Select a saved alert to enable or disable."));
        return;
    }
    const auto identity =
        managedAlertsTable_->item(row, 0)->data(Qt::UserRole).toString();
    const auto found = std::ranges::find(
        managedAlerts_,
        identity,
        &StrategyAlert::id);
    if (found == managedAlerts_.end()) {
        return;
    }
    found->enabled = !found->enabled;
    refreshManagedAlerts();
    emit statusMessage(
        found->enabled
            ? tr("Enabled alert “%1”.").arg(found->name)
            : tr("Disabled alert “%1”.").arg(found->name));
    emit workspaceChanged();
}

void StrategyLabWidget::refreshManagedAlerts() {
    if (!managedAlertsTable_) {
        return;
    }
    managedAlertsTable_->setRowCount(
        static_cast<int>(managedAlerts_.size()));
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (std::size_t index = 0; index < managedAlerts_.size(); ++index) {
        const auto& alert = managedAlerts_[index];
        const auto expired =
            alert.expiresAtUtc > 0 && now > alert.expiresAtUtc;
        const std::array values{
            alert.name.trimmed().isEmpty() ? alert.id : alert.name,
            normalizeWatchlistSymbol(alert.symbol),
            alertFrequencyLabel(alert.frequency) +
                (alert.frequency == AlertFrequency::Cooldown
                     ? tr(" · %1 min")
                           .arg(alert.cooldownSeconds / 60)
                     : QString{}),
            alert.expiresAtUtc > 0
                ? utcDateTime(alert.expiresAtUtc)
                : tr("Open-ended"),
            expired
                ? tr("Expired")
                : (alert.enabled ? tr("Enabled") : tr("Disabled")),
        };
        for (int column = 0; column < static_cast<int>(values.size());
             ++column) {
            auto* item =
                new QTableWidgetItem(values[static_cast<std::size_t>(column)]);
            if (column == 0) {
                item->setData(Qt::UserRole, alert.id);
            }
            managedAlertsTable_->setItem(
                static_cast<int>(index),
                column,
                item);
        }
    }
}

void StrategyLabWidget::evaluateEventReminders() {
    if (!eventReminderEnabled_ ||
        !eventReminderEnabled_->isChecked() ||
        !eventReminderLeadDays_) {
        return;
    }
    const auto today = QDate::currentDate();
    const auto horizon =
        today.addDays(eventReminderLeadDays_->value());
    const auto triggeredAtUtc = QDateTime::currentSecsSinceEpoch();
    for (const auto& event : researchEvents_) {
        if (!validateResearchEvent(event).isEmpty() ||
            event.scheduledDate < today ||
            event.scheduledDate > horizon) {
            continue;
        }
        const auto digest = QCryptographicHash::hash(
                                event.id.toUtf8(),
                                QCryptographicHash::Sha256)
                                .toHex();
        const auto eventSymbol =
            normalizeWatchlistSymbol(event.symbol);
        AlertTrigger trigger{
            .alertId =
                QStringLiteral("event-%1")
                    .arg(QString::fromLatin1(digest)),
            .symbol =
                eventSymbol.isEmpty()
                    ? QStringLiteral("MARKET")
                    : eventSymbol,
            .timestamp =
                QDateTime(
                    event.scheduledDate,
                    QTime(0, 0),
                    QTimeZone::UTC)
                    .toSecsSinceEpoch(),
            .triggeredAtUtc = triggeredAtUtc,
            .message =
                tr("Event reminder · %1 · %2 · %3 · source %4")
                    .arg(
                        event.scheduledDate.toString(Qt::ISODate),
                        eventSymbol.isEmpty()
                            ? tr("Market")
                            : eventSymbol,
                        event.title,
                        event.source),
        };
        if (alertEngine_.recordExternalTrigger(trigger)) {
            appendAlert(trigger);
        }
    }
}

void StrategyLabWidget::appendAlert(
    const AlertTrigger& trigger,
    const bool notify) {
    constexpr auto maximumVisibleAlerts = 1'000;
    while (alertTable_->rowCount() >= maximumVisibleAlerts) {
        alertTable_->removeRow(0);
    }
    const auto row = alertTable_->rowCount();
    alertTable_->insertRow(row);
    alertTable_->setItem(
        row,
        0,
        new QTableWidgetItem(utcDateTime(trigger.triggeredAtUtc)));
    alertTable_->setItem(
        row,
        1,
        new QTableWidgetItem(utcDateTime(trigger.timestamp)));
    alertTable_->setItem(row, 2, new QTableWidgetItem(trigger.symbol));
    alertTable_->setItem(row, 3, new QTableWidgetItem(trigger.message));
    if (notify && QSystemTrayIcon::isSystemTrayAvailable()) {
        if (!trayIcon_->isVisible()) {
            trayIcon_->show();
        }
        trayIcon_->showMessage(
            tr("Strategy Lab alert · %1").arg(trigger.symbol),
            trigger.message,
            QSystemTrayIcon::Information,
            8'000);
    }
    if (notify) {
        emit statusMessage(trigger.message);
        emit workspaceChanged();
    }
}

} // namespace tvchart
