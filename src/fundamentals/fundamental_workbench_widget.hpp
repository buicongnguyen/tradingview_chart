#pragma once

#include "domain/bar.hpp"
#include "fundamentals/fundamental_analysis.hpp"
#include "fundamentals/fundamental_screener.hpp"
#include "research/research_models.hpp"
#include "strategy/strategy_engine.hpp"

#include <QStringList>
#include <QWidget>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QTableWidget;

namespace tvchart {

class FundamentalStore;
class HistoricalDataStore;
class SecFundamentalsClient;
struct StoredFundamentalCompany;

class FundamentalGraphWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FundamentalGraphWidget(QWidget* parent = nullptr);

    void setSeries(
        QString title,
        std::vector<FundamentalSeriesPoint> series);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_;
    std::vector<FundamentalSeriesPoint> series_;
};

class FundamentalWorkbenchWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FundamentalWorkbenchWidget(
        FundamentalStore* store,
        HistoricalDataStore* historyStore,
        bool onlineDataEnabled,
        QWidget* parent = nullptr);
    ~FundamentalWorkbenchWidget() override;

    void setCurrentContext(QString symbol, Bars bars);
    void setResearchWorkspace(ResearchWorkspace workspace);
    void setUniverseSymbols(QStringList symbols);
    void restoreSettings(QSettings& settings);
    void saveSettings(QSettings& settings) const;

signals:
    void statusMessage(const QString& message);
    void settingsChanged();
    void alertTriggered(tvchart::AlertTrigger trigger);

private:
    struct ScreenConditionControls {
        QCheckBox* enabled{};
        QComboBox* field{};
        QComboBox* comparison{};
        QDoubleSpinBox* threshold{};
    };

    void refreshSecFacts();
    void refreshAllViews();
    void refreshSummary();
    void refreshGraph();
    void refreshPeerTable();
    void refreshValuation();
    void runScreen();
    void evaluateCurrentScreenAlert();
    void emitScreenAlert(
        const FundamentalScreenDefinition& definition,
        const FundamentalScreenRow& row);
    void refreshEventChoices();
    void runEventStudy();
    [[nodiscard]] StoredFundamentalCompany currentCompany() const;
    [[nodiscard]] std::optional<double> currentPrice() const;
    [[nodiscard]] FundamentalScreenDefinition screenDefinition() const;
    void applyScreenDefinition(
        const FundamentalScreenDefinition& definition);

    FundamentalStore* store_{};
    HistoricalDataStore* historyStore_{};
    SecFundamentalsClient* client_{};
    bool onlineDataEnabled_{};
    QString symbol_;
    Bars currentBars_;
    ResearchWorkspace researchWorkspace_;
    QStringList universeSymbols_;

    QLabel* identityLabel_{};
    QLabel* pointInTimeLabel_{};
    QLabel* summaryWarningLabel_{};
    QPushButton* refreshButton_{};
    QDateEdit* asOfDateInput_{};
    QTableWidget* summaryTable_{};
    QTableWidget* derivedTable_{};

    QComboBox* graphMetricSelector_{};
    QComboBox* graphPeriodSelector_{};
    FundamentalGraphWidget* graph_{};
    QTableWidget* graphDataTable_{};
    QTableWidget* peerTable_{};

    QDoubleSpinBox* growthInput_{};
    QDoubleSpinBox* discountInput_{};
    QDoubleSpinBox* terminalInput_{};
    QSpinBox* forecastYearsInput_{};
    QLabel* valuationSummaryLabel_{};
    QTableWidget* sensitivityTable_{};

    std::vector<ScreenConditionControls> screenConditions_;
    QComboBox* screenSortSelector_{};
    QCheckBox* screenDescendingInput_{};
    QCheckBox* screenAlertEnabled_{};
    QCheckBox* filingAlertEnabled_{};
    QTableWidget* screenResultsTable_{};
    QLabel* screenCoverageLabel_{};

    QComboBox* eventSelector_{};
    QCheckBox* eventAfterCloseInput_{};
    QLineEdit* eventBenchmarkInput_{};
    QLabel* eventSummaryLabel_{};
    QTableWidget* eventResultTable_{};
};

} // namespace tvchart
