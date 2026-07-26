#pragma once

#include "data/market_data_parser.hpp"
#include "domain/bar.hpp"
#include "strategy/replay_session.hpp"
#include "strategy/strategy_engine.hpp"

#include <QStringList>
#include <QWidget>

#include <cstddef>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
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
        MarketDataMetadata metadata);
    void setWatchlistSymbols(QStringList symbols);
    void restoreSettings(QSettings& settings);
    void saveSettings(QSettings& settings) const;
    void restoreChartIfReplaying();

signals:
    void replayBarsRequested(tvchart::Bars bars);
    void restoreFullSeriesRequested();
    void statusMessage(QString message);

private:
    struct RuleControls {
        QComboBox* leftField{};
        QSpinBox* leftPeriod{};
        QComboBox* comparison{};
        QComboBox* rightField{};
        QSpinBox* rightPeriod{};
        QDoubleSpinBox* constant{};
    };

    void buildUi();
    [[nodiscard]] QWidget* buildRuleEditor(
        const QString& title,
        RuleControls& controls,
        StrategyComparison defaultComparison);
    void populateFieldCombo(QComboBox& combo, bool includeConstant);
    void refreshRuleControls(RuleControls& controls);
    [[nodiscard]] StrategyCondition condition(
        const RuleControls& controls) const;
    [[nodiscard]] StrategyDefinition currentStrategy() const;
    void applyStrategy(const StrategyDefinition& strategy);

    void runCurrentBacktest();
    void showBacktest(const BacktestResult& result);
    void resetReplay();
    void stepReplay(std::size_t count = 1);
    void stopReplay(bool restoreChart);
    void refreshReplayLabel();
    void scanWatchlist();
    void evaluateForegroundAlert();
    void appendAlert(const AlertTrigger& trigger);

    HistoricalDataStore* historyStore_{};
    RuleControls entryControls_;
    RuleControls exitControls_;
    QDoubleSpinBox* initialCapital_{};
    QDoubleSpinBox* allocationPercent_{};
    QDoubleSpinBox* commission_{};
    QDoubleSpinBox* slippage_{};
    QCheckBox* fractionalShares_{};
    QLabel* metricsLabel_{};
    QTableWidget* tradesTable_{};

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
    QTableWidget* alertTable_{};
    QSystemTrayIcon* trayIcon_{};
    StrategyAlertEngine alertEngine_;

    QString symbol_;
    QString provider_;
    Timeframe timeframe_{Timeframe::FiveMinutes};
    Bars bars_;
    MarketDataMetadata metadata_;
    QStringList watchlistSymbols_;
};

} // namespace tvchart
