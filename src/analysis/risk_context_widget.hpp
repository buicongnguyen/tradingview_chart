#pragma once

#include "research/research_models.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QSettings;
class QTableWidget;

namespace tvchart {

class FundamentalStore;
class HistoricalDataStore;

class RiskContextWidget final : public QWidget {
    Q_OBJECT

public:
    explicit RiskContextWidget(
        HistoricalDataStore* historyStore,
        FundamentalStore* fundamentalStore,
        QWidget* parent = nullptr);

    void setCurrentContext(QString symbol);
    void setResearchWorkspace(ResearchWorkspace workspace);
    void restoreSettings(QSettings& settings);
    void saveSettings(QSettings& settings) const;

signals:
    void statusMessage(QString message);
    void settingsChanged();

private:
    void analyze();
    void clearReport(const QString& message);

    HistoricalDataStore* historyStore_{};
    FundamentalStore* fundamentalStore_{};
    QString symbol_;
    ResearchWorkspace researchWorkspace_;

    QLabel* symbolLabel_{};
    QLineEdit* benchmarkInput_{};
    QLabel* headlineLabel_{};
    QLabel* coverageLabel_{};
    QLabel* datesLabel_{};
    QLabel* historicalLabel_{};
    QLabel* warningLabel_{};
    QTableWidget* evidenceTable_{};
    QTableWidget* constructiveTable_{};
};

} // namespace tvchart
