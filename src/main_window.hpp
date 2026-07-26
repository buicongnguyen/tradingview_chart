#pragma once

#include "analysis/technical_indicators.hpp"
#include "chart/chart_view.hpp"
#include "data/market_data_parser.hpp"
#include "domain/bar.hpp"
#include "research/research_models.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QMainWindow>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <vector>

class QAction;
class QActionGroup;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSettings;
class QSpinBox;
class QSplitter;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

namespace tvchart {

class MarketDataClient;
class HistoricalDataStore;
class AlphaVantageResearchClient;
class EventIntelligenceClient;
class FundamentalStore;
class FundamentalWorkbenchWidget;
class MarketStructureWidget;
class PortfolioWidget;
class RiskContextWidget;
class StrategyLabWidget;
struct AlphaVantageResearchResult;
struct EventIntelligenceResult;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        QWidget* parent = nullptr,
        bool onlineDataEnabled = true,
        bool settingsEnabled = true);
    ~MainWindow() override;

signals:
    void chartReady();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class WorkspaceLevel : int {
        Basic,
        Intermediate,
        Advanced,
        Custom,
    };

    struct IndicatorControl {
        IndicatorKind kind{IndicatorKind::None};
        QTableWidgetItem* enabledItem{};
        QSpinBox* periodOrFast{};
        QSpinBox* slow{};
        QSpinBox* signal{};
    };

    struct DataStatusSnapshot {
        QString lifecycle;
        QString source;
        QString detail;
        MarketDataMetadata metadata;
        std::size_t barCount{};
        std::optional<std::int64_t> lastBarTimestamp;
    };

    void buildUi();
    void buildWatchlistDock();
    void buildIndicatorDock();
    void buildAnalysisDock();
    void buildDataStatusDock();
    void buildResearchDock();
    void buildFundamentalDock();
    void buildMarginRiskDock();
    void buildPortfolioDock();
    void buildRiskContextDock();
    void buildMarketStructureDock();
    void buildStrategyLabDock();
    void applyWorkspaceLevel(WorkspaceLevel level, bool resetLayout = true);
    void applyToolbarLevel(WorkspaceLevel level);
    void selectWorkspaceLevelAction(WorkspaceLevel level);
    void markWorkspaceCustom();
    void resetDockLayout(bool revealAll);
    void restoreSettings();
    void restoreIndicatorSettings(QSettings& settings);
    void saveSettings() const;
    void saveSettingsNow() const;

    void reloadActiveSource();
    void loadMarketData();
    void loadDemo();
    void showDemo(const QString& source, const QString& detail);
    bool applySeries(
        const QString& symbol,
        const QString& timeframe,
        const QString& source,
        Bars bars,
        MarketDataMetadata metadata,
        const QString& lifecycle,
        const QString& detail = {},
        Bars rawCacheBars = {});
    void updateTechnicalAnalysis();
    void handleIndicatorConfigurationChanged();
    [[nodiscard]] std::vector<IndicatorSpec> activeIndicatorSpecs() const;
    void scheduleRefresh(Timeframe timeframe);
    void openCsv();

    [[nodiscard]] NamedWatchlist* activeWatchlist() noexcept;
    [[nodiscard]] const NamedWatchlist* activeWatchlist() const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    activeWatchlistEntryIndex() const noexcept;
    void refreshWatchlistSelector();
    void refreshWatchlistEntries(const QString& preferredSymbol = {});
    void selectNamedWatchlist(int index);
    void createNamedWatchlist();
    void renameNamedWatchlist();
    void deleteNamedWatchlist();
    void addWatchlistSymbol();
    void removeWatchlistSymbol();
    void moveWatchlistSymbol(int direction);
    void updateWatchlistNote();
    void updateWatchlistSort(int index);
    void importWatchlist();
    void exportWatchlist();

    void refreshResearchFromProvider();
    void refreshSecFilings();
    void refreshFredCalendar();
    void refreshResearchDisplay();
    void mergeResearchResult(AlphaVantageResearchResult result);
    void mergeEventIntelligenceResult(EventIntelligenceResult result);
    void addTargetEstimate();
    void removeTargetEstimate();
    void importTargetEstimates();
    void exportTargetEstimates();
    void addResearchEvent();
    void removeResearchEvent();
    void recalculateMarginRisk();
    void setChartLayout(const QString& layout);
    void loadComparisonData();
    void applyComparisonSeries(
        const QString& symbol,
        const QString& source,
        Bars bars,
        const MarketDataMetadata& metadata);
    void refreshComparisonAnalysis();
    void addPriceLevel();
    void clearPriceLevels();
    void showReplaySeries(Bars bars);
    void restoreFullSeries();
    void refreshStrategyWatchlist();
    void refreshFundamentalUniverse();

    void showLoadingDataStatus(const QString& symbol, const QString& timeframe);
    void updateDataStatus(
        const QString& source,
        const MarketDataMetadata& metadata,
        const Bars& bars,
        const QString& lifecycle,
        const QString& detail);
    void refreshDataStatusDisplay();
    void applyTheme(bool dark);
    void showAbout();
    [[nodiscard]] QString activeSymbol() const;
    [[nodiscard]] Timeframe activeTimeframe() const;
    [[nodiscard]] QString activeTimeframeLabel() const;
    [[nodiscard]] PriceAdjustmentMode activeAdjustmentMode() const noexcept;
    void setStatus(
        const QString& source,
        std::size_t barCount,
        const QString& symbol,
        const QString& timeframe);

    ChartView* chartView_{};
    ChartView* comparisonChartView_{};
    QSplitter* chartSplitter_{};
    MarketDataClient* marketDataClient_{};
    MarketDataClient* comparisonDataClient_{};
    AlphaVantageResearchClient* researchClient_{};
    EventIntelligenceClient* eventIntelligenceClient_{};
    FundamentalWorkbenchWidget* fundamentalWidget_{};
    MarketStructureWidget* marketStructureWidget_{};
    PortfolioWidget* portfolioWidget_{};
    RiskContextWidget* riskContextWidget_{};
    StrategyLabWidget* strategyLab_{};
    std::unique_ptr<HistoricalDataStore> historyStore_;
    std::unique_ptr<FundamentalStore> fundamentalStore_;
    QTimer* refreshTimer_{};
    QTimer* statusAgeTimer_{};

    QComboBox* namedWatchlistSelector_{};
    QComboBox* watchlistSortSelector_{};
    QListWidget* watchlist_{};
    QLineEdit* watchlistSymbolInput_{};
    QLineEdit* watchlistNoteInput_{};

    QComboBox* timeframeSelector_{};
    QComboBox* priceBasisSelector_{};
    QComboBox* styleSelector_{};
    QComboBox* scaleSelector_{};
    QComboBox* chartLayoutSelector_{};
    QLineEdit* comparisonSymbolInput_{};
    QLabel* comparisonStatusLabel_{};
    QTableWidget* indicatorTable_{};
    std::vector<IndicatorControl> indicatorControls_;

    QLabel* sourceLabel_{};
    QLabel* latestValueLabel_{};
    QLabel* changeValueLabel_{};
    QLabel* rangeValueLabel_{};
    QLabel* averageVolumeValueLabel_{};
    QLabel* indicatorValueLabel_{};
    QLabel* dataLifecycleLabel_{};
    QLabel* dataDeliveryLabel_{};
    QLabel* dataLastBarLabel_{};
    QLabel* dataRetrievedLabel_{};
    QLabel* dataMarketLabel_{};
    QLabel* dataBarCountLabel_{};
    QLabel* dataPriceBasisLabel_{};
    QLabel* dataQualityLabel_{};
    QLabel* dataDetailLabel_{};

    QLabel* researchProviderLabel_{};
    QLabel* researchCompanyLabel_{};
    QLabel* researchFundamentalsLabel_{};
    QLabel* researchProviderTargetLabel_{};
    QLabel* researchRatingsLabel_{};
    QLabel* researchManualConsensusLabel_{};
    QLabel* researchNextEventLabel_{};
    QPushButton* researchRefreshButton_{};
    QPushButton* secRefreshButton_{};
    QPushButton* fredRefreshButton_{};
    QTableWidget* targetEstimateTable_{};
    QTableWidget* researchEventTable_{};

    QDoubleSpinBox* marginLongValueInput_{};
    QDoubleSpinBox* marginDebitInput_{};
    QDoubleSpinBox* marginOtherEquityInput_{};
    QDoubleSpinBox* marginMaintenanceInput_{};
    QDoubleSpinBox* marginStressInput_{};
    QLabel* marginInstrumentLabel_{};
    QLabel* marginCurrentLabel_{};
    QLabel* marginStressResultLabel_{};
    QLabel* marginThresholdLabel_{};

    QAction* darkThemeAction_{};
    QActionGroup* workspaceLevelActions_{};
    QAction* marketStructureOverlayAction_{};
    Bars currentBars_;
    Bars comparisonBars_;
    std::vector<ChartPriceLevel> priceLevels_;
    QString currentSeriesSymbol_;
    QString comparisonSeriesSymbol_;
    WatchlistCollection watchlists_{defaultWatchlists()};
    ResearchWorkspace researchWorkspace_;
    DataStatusSnapshot dataStatus_;
    MarketDataMetadata comparisonMetadata_;
    bool onlineDataEnabled_{true};
    bool settingsEnabled_{true};
    bool restoringSettings_{};
    bool applyingWorkspaceProfile_{};
    WorkspaceLevel workspaceLevel_{WorkspaceLevel::Intermediate};
    WorkspaceLevel workspaceBaseLevel_{WorkspaceLevel::Intermediate};
};

} // namespace tvchart
