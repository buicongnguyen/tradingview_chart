#pragma once

#include "portfolio/portfolio_models.hpp"
#include "research/research_models.hpp"

#include <QHash>
#include <QWidget>

#include <cstdint>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QSettings;
class QTableWidget;

namespace tvchart {

class HistoricalDataStore;

class PortfolioWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PortfolioWidget(
        HistoricalDataStore* historyStore,
        QWidget* parent = nullptr);

    void setCurrentQuote(
        QString symbol,
        double price,
        std::int64_t asOfUtc,
        QString currency);
    void setResearchEvents(std::vector<ResearchEvent> events);
    void restoreSettings(QSettings& settings);
    void saveSettings(QSettings& settings) const;

signals:
    void statusMessage(QString message);
    void portfolioChanged();

private:
    void buildUi();
    [[nodiscard]] Portfolio* activePortfolio() noexcept;
    [[nodiscard]] const Portfolio* activePortfolio() const noexcept;
    void refreshPortfolioSelector(const QString& preferredId = {});
    void refreshDisplay();
    void createPortfolio();
    void renamePortfolio();
    void deletePortfolio();
    void addTransaction();
    void removeTransaction();
    void addTarget();
    void removeTarget();
    void refreshPortfolioRisk(
        const Portfolio& portfolio,
        const PortfolioSnapshot& snapshot);
    void refreshTargets(
        const Portfolio& portfolio,
        const PortfolioSnapshot& snapshot);

    HistoricalDataStore* historyStore_{};
    PortfolioWorkspace workspace_{defaultPortfolioWorkspace()};
    QHash<QString, PortfolioPrice> prices_;
    std::vector<ResearchEvent> researchEvents_;
    QString currentSymbol_;
    double currentPrice_{};

    QComboBox* portfolioSelector_{};
    QLabel* summaryLabel_{};
    QLabel* riskLabel_{};
    QLabel* eventRiskLabel_{};
    QLineEdit* benchmarkInput_{};
    QLabel* portfolioRiskSummary_{};
    QTableWidget* riskContributionTable_{};
    QTableWidget* correlationTable_{};
    QLabel* targetSummary_{};
    QTableWidget* targetsTable_{};
    QTableWidget* rebalanceTable_{};
    QTableWidget* holdingsTable_{};
    QTableWidget* transactionsTable_{};
};

} // namespace tvchart
