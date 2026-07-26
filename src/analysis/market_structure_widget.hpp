#pragma once

#include "analysis/market_structure_analyzer.hpp"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSettings;
class QSpinBox;
class QTableWidget;

namespace tvchart {

class MarketStructureWidget final : public QWidget {
    Q_OBJECT

public:
    explicit MarketStructureWidget(QWidget* parent = nullptr);

    void setCurrentContext(
        QString symbol,
        Timeframe timeframe,
        const Bars& bars);
    void restoreSettings(QSettings& settings);
    void saveSettings(QSettings& settings) const;

    [[nodiscard]] const MarketStructureReport& report() const noexcept;

signals:
    void reportChanged();
    void statusMessage(QString message);
    void settingsChanged();

private:
    void analyze();
    void clearReport(const QString& message);
    [[nodiscard]] MarketStructureSettings settings() const;

    QString symbol_;
    Timeframe timeframe_{Timeframe::OneDay};
    Bars bars_;
    MarketStructureReport report_;

    QLabel* symbolLabel_{};
    QSpinBox* pivotStrengthInput_{};
    QDoubleSpinBox* zoneAtrInput_{};
    QSpinBox* minimumTouchesInput_{};
    QCheckBox* historicalValidationInput_{};
    QLabel* headlineLabel_{};
    QLabel* confluenceLabel_{};
    QLabel* warningLabel_{};
    QTableWidget* zoneTable_{};
    QTableWidget* patternTable_{};
    QTableWidget* outcomeTable_{};
};

} // namespace tvchart
