#pragma once

#include "chart/chart_view.hpp"
#include "domain/bar.hpp"

#include <QMainWindow>

#include <cstddef>

class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QListWidget;

namespace tvchart {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

signals:
    void chartReady();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void restoreSettings();
    void saveSettings() const;
    void loadDemo();
    void openCsv();
    void applyTheme(bool dark);
    void showAbout();
    [[nodiscard]] QString activeSymbol() const;
    [[nodiscard]] Timeframe activeTimeframe() const;
    [[nodiscard]] QString activeTimeframeLabel() const;
    void setStatus(const QString& source, std::size_t barCount);

    ChartView* chartView_{};
    QListWidget* watchlist_{};
    QComboBox* timeframeSelector_{};
    QComboBox* styleSelector_{};
    QLabel* sourceLabel_{};
    QAction* darkThemeAction_{};
    bool restoringSettings_{};
};

} // namespace tvchart
