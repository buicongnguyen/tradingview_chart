#pragma once

#include "analysis/technical_indicators.hpp"
#include "chart/chart_view.hpp"
#include "domain/bar.hpp"

#include <QMainWindow>

#include <cstddef>

class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QListWidget;
class QTimer;

namespace tvchart {

class MarketDataClient;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        QWidget* parent = nullptr,
        bool onlineDataEnabled = true,
        bool settingsEnabled = true);

signals:
    void chartReady();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void restoreSettings();
    void saveSettings() const;
    void reloadActiveSource();
    void loadMarketData();
    void loadDemo();
    void showDemo(const QString& source, const QString& detail);
    bool applySeries(
        const QString& symbol,
        const QString& timeframe,
        const QString& source,
        Bars bars);
    void updateTechnicalAnalysis();
    void scheduleRefresh(Timeframe timeframe);
    void openCsv();
    void applyTheme(bool dark);
    void showAbout();
    [[nodiscard]] QString activeSymbol() const;
    [[nodiscard]] Timeframe activeTimeframe() const;
    [[nodiscard]] QString activeTimeframeLabel() const;
    [[nodiscard]] IndicatorKind activeIndicator() const;
    void setStatus(
        const QString& source,
        std::size_t barCount,
        const QString& symbol,
        const QString& timeframe);

    ChartView* chartView_{};
    MarketDataClient* marketDataClient_{};
    QTimer* refreshTimer_{};
    QListWidget* watchlist_{};
    QComboBox* timeframeSelector_{};
    QComboBox* styleSelector_{};
    QComboBox* indicatorSelector_{};
    QLabel* sourceLabel_{};
    QLabel* latestValueLabel_{};
    QLabel* changeValueLabel_{};
    QLabel* rangeValueLabel_{};
    QLabel* averageVolumeValueLabel_{};
    QLabel* indicatorValueLabel_{};
    QAction* darkThemeAction_{};
    Bars currentBars_;
    bool onlineDataEnabled_{true};
    bool settingsEnabled_{true};
    bool restoringSettings_{};
};

} // namespace tvchart
