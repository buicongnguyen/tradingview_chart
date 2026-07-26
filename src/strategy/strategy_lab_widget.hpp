#pragma once

#include "data/market_data_parser.hpp"
#include "domain/bar.hpp"
#include "strategy/replay_session.hpp"
#include "strategy/strategy_engine.hpp"
#include "research/research_models.hpp"

#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QSystemTrayIcon;
class QTableWidget;
class QTimer;

namespace tvchart {

class HistoricalDataStore;

class StrategyLabWidget final : public QWidget {
    Q_OBJECT

public:
    explicit StrategyLabWidget(
        HistoricalDataStore* historyStore,
        QWidget* parent = nullptr);

    void setCurrentSeries(
        QString symbol,
        Timeframe timeframe,
        QString provider,
        Bars bars,
        MarketDataMetadata metadata,
        Bars rawCacheBars = {});
    void setWatchlistSymbols(QStringList symbols);
    void setResearchEvents(std::vector<ResearchEvent> events);
    bool addPriceLevelAlert(
        QString symbol,
        double price,
        bool crossesAbove,
        QString label);
    void recordExternalAlert(AlertTrigger trigger);
    void restoreSettings(QSettings& settings);
    void saveSettings(QSettings& settings) const;
    void restoreChartIfReplaying();

signals:
    void replayBarsRequested(tvchart::Bars bars);
    void restoreFullSeriesRequested();
    void statusMessage(QString message);
    void workspaceChanged();

private:
    struct RuleControls {
        QComboBox* leftField{};
        QComboBox* leftTimeframe{};
        QSpinBox* leftPeriod{};
        QComboBox* comparison{};
        QComboBox* rightField{};
        QComboBox* rightTimeframe{};
        QSpinBox* rightPeriod{};
        QDoubleSpinBox* constant{};
    };

    struct RuleGroupControls {
        QComboBox* match{};
        QTableWidget* table{};
        QPushButton* remove{};
    };

    void buildUi();
    [[nodiscard]] QWidget* buildRuleEditor(
        const QString& title,
        RuleGroupControls& controls,
        StrategyComparison defaultComparison);
    void addRule(
        RuleGroupControls& controls,
        const StrategyCondition& initial);
    void removeSelectedRule(RuleGroupControls& controls);
    void populateFieldCombo(QComboBox& combo, bool includeConstant);
    void populateTimeframeCombo(QComboBox& combo);
    void refreshRuleControls(RuleControls& controls);
    [[nodiscard]] StrategyCondition condition(
        const RuleGroupControls& controls,
        int row) const;
    [[nodiscard]] ConditionGroup conditionGroup(
        const RuleGroupControls& controls) const;
    [[nodiscard]] StrategyDefinition currentStrategy() const;
    [[nodiscard]] TimeframeSeries loadAdditionalSeries(
        const StrategyDefinition& strategy,
        const QString& symbol,
        QStringList& missing) const;
    void applyStrategy(const StrategyDefinition& strategy);
    void saveNamedStrategy();
    void loadNamedStrategy();
    void deleteNamedStrategy();
    void refreshStrategyLibrary();

    void runCurrentBacktest();
    void runRobustness();
    void showBacktest(const BacktestResult& result);
    void resetReplay();
    void stepReplay(std::size_t count = 1);
    void stopReplay(bool restoreChart);
    void refreshReplayLabel();
    void scanWatchlist();
    void evaluateForegroundAlert();
    void evaluateManagedAlerts(bool automatic);
    void addManagedAlert();
    void removeManagedAlert();
    void toggleManagedAlert();
    void refreshManagedAlerts();
    void evaluateEventReminders();
    void appendAlert(const AlertTrigger& trigger, bool notify = true);

    HistoricalDataStore* historyStore_{};
    RuleGroupControls entryControls_;
    RuleGroupControls exitControls_;
    QLineEdit* strategyName_{};
    QComboBox* strategyLibrary_{};
    std::vector<StrategyDefinition> savedStrategies_;
    QDoubleSpinBox* initialCapital_{};
    QDoubleSpinBox* allocationPercent_{};
    QDoubleSpinBox* commission_{};
    QDoubleSpinBox* slippage_{};
    QCheckBox* fractionalShares_{};
    QCheckBox* holdoutEnabled_{};
    QSpinBox* holdoutPercent_{};
    QLabel* metricsLabel_{};
    QLabel* validationLabel_{};
    QTableWidget* tradesTable_{};
    QSpinBox* robustnessFolds_{};
    QSpinBox* robustnessSimulations_{};
    QLabel* robustnessSummary_{};
    QTableWidget* walkForwardTable_{};
    QTableWidget* parameterTable_{};
    QTableWidget* regimeTable_{};
    QTableWidget* batchTable_{};

    QSpinBox* replayWarmup_{};
    QComboBox* replaySpeed_{};
    QPushButton* replayPlay_{};
    QLabel* replayStatus_{};
    QTimer* replayTimer_{};
    ReplaySession replay_;
    bool replayActive_{};

    QTableWidget* scannerTable_{};
    QLabel* scannerStatus_{};
    QCheckBox* alertEnabled_{};
    QLineEdit* alertName_{};
    QComboBox* alertFrequency_{};
    QSpinBox* alertCooldownMinutes_{};
    QCheckBox* alertExpiryEnabled_{};
    QDateTimeEdit* alertExpiry_{};
    QTableWidget* managedAlertsTable_{};
    QCheckBox* eventReminderEnabled_{};
    QSpinBox* eventReminderLeadDays_{};
    QTableWidget* alertTable_{};
    QSystemTrayIcon* trayIcon_{};
    StrategyAlertEngine alertEngine_;
    std::vector<StrategyAlert> managedAlerts_;
    std::vector<ResearchEvent> researchEvents_;

    QString symbol_;
    QString provider_;
    Timeframe timeframe_{Timeframe::FiveMinutes};
    Bars bars_;
    MarketDataMetadata metadata_;
    QStringList watchlistSymbols_;
};

} // namespace tvchart
