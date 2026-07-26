#include "data/market_data_client.hpp"
#include "main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDebug>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("buicongnguyen"));
    QCoreApplication::setApplicationName(QStringLiteral("TradeChartLab"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("TradeChart Lab market analysis and simulation"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Exit successfully after the embedded chart reports ready."));
    parser.addOption(smokeOption);
    const QCommandLineOption marketDataSmokeOption(
        QStringLiteral("market-data-smoke-test"),
        QStringLiteral("Fetch AAPL from the default Yahoo Finance provider and exit."));
    parser.addOption(marketDataSmokeOption);
    parser.process(application);

    if (parser.isSet(marketDataSmokeOption)) {
        tvchart::MarketDataClient client;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &application, [&application]() {
            qCritical() << "MARKET_DATA_SMOKE_TIMEOUT";
            application.exit(2);
        });
        client.fetch(
            QStringLiteral("AAPL"),
            tvchart::Timeframe::FiveMinutes,
            [&application, &timeout](tvchart::MarketDataResult result) {
                timeout.stop();
                if (!result.ok() || result.source != QStringLiteral("Yahoo Finance")) {
                    qCritical() << "MARKET_DATA_SMOKE_FAILED" << result.error;
                    application.exit(3);
                    return;
                }
                qInfo() << "MARKET_DATA_SMOKE_OK" << result.source
                        << result.bars.size();
                application.exit(0);
            });
        timeout.start(20'000);
        return application.exec();
    }

    const auto smokeMode = parser.isSet(smokeOption);
    tvchart::MainWindow window(nullptr, !smokeMode, !smokeMode);
    if (smokeMode) {
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &application, [&application]() {
            qCritical() << "SMOKE_TIMEOUT";
            application.exit(2);
        });
        auto* chartView = window.findChild<tvchart::ChartView*>();
        if (!chartView) {
            qCritical() << "SMOKE_CHART_VIEW_MISSING";
            return 4;
        }
        QObject::connect(
            chartView,
            &tvchart::ChartView::chartError,
            &application,
            [&application](const QString& message) {
                qCritical() << "SMOKE_CHART_ERROR" << message;
                application.exit(3);
            });
        QObject::connect(&window, &tvchart::MainWindow::chartReady, &application, [&]() {
            auto* indicatorTable =
                window.findChild<QTableWidget*>(QStringLiteral("indicatorTable"));
            auto* scaleSelector =
                window.findChild<QComboBox*>(QStringLiteral("scaleSelector"));
            auto* chartLayoutSelector =
                window.findChild<QComboBox*>(
                    QStringLiteral("chartLayoutSelector"));
            auto* scaleToolbarAction =
                window.findChild<QAction*>(
                    QStringLiteral("scaleToolbarAction"));
            auto* layoutToolbarAction =
                window.findChild<QAction*>(
                    QStringLiteral("layoutToolbarAction"));
            auto* targetTable = window.findChild<QTableWidget*>(
                QStringLiteral("targetEstimateTable"));
            auto* eventTable = window.findChild<QTableWidget*>(
                QStringLiteral("researchEventTable"));
            auto* marginValue = window.findChild<QDoubleSpinBox*>(
                QStringLiteral("marginLongValueInput"));
            auto* strategyRun = window.findChild<QPushButton*>(
                QStringLiteral("strategyRunBacktest"));
            auto* strategyTrades = window.findChild<QTableWidget*>(
                QStringLiteral("strategyTradesTable"));
            auto* marketStructureRun =
                window.findChild<QPushButton*>(
                    QStringLiteral(
                        "marketStructureAnalyzeButton"));
            auto* marketStructurePatterns =
                window.findChild<QTableWidget*>(
                    QStringLiteral(
                        "marketStructurePatternTable"));
            auto* analysisDock = window.findChild<QDockWidget*>(
                QStringLiteral("analysisDock"));
            auto* marketStructureDock =
                window.findChild<QDockWidget*>(
                    QStringLiteral("marketStructureDock"));
            auto* strategyDock = window.findChild<QDockWidget*>(
                QStringLiteral("strategyLabDock"));
            auto* basicWorkspace = window.findChild<QAction*>(
                QStringLiteral("workspaceBasicAction"));
            auto* intermediateWorkspace =
                window.findChild<QAction*>(
                    QStringLiteral(
                        "workspaceIntermediateAction"));
            auto* advancedWorkspace =
                window.findChild<QAction*>(
                    QStringLiteral("workspaceAdvancedAction"));
            auto* structureOverlay = window.findChild<QAction*>(
                QStringLiteral(
                    "marketStructureOverlayAction"));
            auto* theoryLabAction = window.findChild<QAction*>(
                QStringLiteral(
                    "theoryValidationLabAction"));
            auto* theoryAnalyze = window.findChild<QPushButton*>(
                QStringLiteral("theoryAnalyzeButton"));
            auto* theoryTable = window.findChild<QTableWidget*>(
                QStringLiteral("theoryValidationTable"));
            auto* simulationAction =
                window.findChild<QAction*>(
                    QStringLiteral("tradingSimulationAction"));
            auto* simulationStart =
                window.findChild<QPushButton*>(
                    QStringLiteral("simulationStartButton"));
            auto* simulationSummary =
                window.findChild<QLabel*>(
                    QStringLiteral("simulationAccountSummary"));
            auto* scriptAction =
                window.findChild<QAction*>(
                    QStringLiteral("safeScriptLabAction"));
            auto* scriptSource =
                window.findChild<QTextEdit*>(
                    QStringLiteral("scriptSourceEditor"));
            auto* scriptCompile =
                window.findChild<QPushButton*>(
                    QStringLiteral("scriptCompileButton"));
            auto* scriptApply =
                window.findChild<QPushButton*>(
                    QStringLiteral("scriptApplyButton"));
            if (!indicatorTable || !scaleSelector ||
                !chartLayoutSelector || !scaleToolbarAction ||
                !layoutToolbarAction || !targetTable ||
                !eventTable || !marginValue || !strategyRun ||
                !strategyTrades || !marketStructureRun ||
                !marketStructurePatterns || !analysisDock ||
                !marketStructureDock || !strategyDock ||
                !basicWorkspace || !intermediateWorkspace ||
                !advancedWorkspace || !structureOverlay ||
                !theoryLabAction || !theoryAnalyze ||
                !theoryTable || !simulationAction ||
                !simulationStart || !simulationSummary ||
                !scriptAction || !scriptSource ||
                !scriptCompile || !scriptApply) {
                qCritical() << "SMOKE_ANALYSIS_CONTROLS_MISSING";
                application.exit(4);
                return;
            }
            if (!window.tabifiedDockWidgets(analysisDock).contains(
                    marketStructureDock)) {
                qCritical() << "SMOKE_DOCK_LAYOUT_NOT_TABBED";
                application.exit(5);
                return;
            }
            basicWorkspace->trigger();
            if (!analysisDock->isVisible()) {
                qCritical() << "SMOKE_BASIC_ANALYSIS_HIDDEN";
                application.exit(6);
                return;
            }
            if (marketStructureDock->isVisible() ||
                strategyDock->isVisible()) {
                qCritical() << "SMOKE_BASIC_ADVANCED_PANEL_VISIBLE";
                application.exit(9);
                return;
            }
            if (structureOverlay->isChecked()) {
                qCritical() << "SMOKE_BASIC_OVERLAY_VISIBLE";
                application.exit(10);
                return;
            }
            if (scaleToolbarAction->isVisible() ||
                layoutToolbarAction->isVisible()) {
                qCritical() << "SMOKE_BASIC_TOOLBAR_TOO_COMPLEX";
                application.exit(11);
                return;
            }
            advancedWorkspace->trigger();
            if (!marketStructureDock->isVisible() ||
                !strategyDock->isVisible() ||
                !structureOverlay->isChecked() ||
                !scaleToolbarAction->isVisible() ||
                !layoutToolbarAction->isVisible()) {
                qCritical() << "SMOKE_ADVANCED_WORKSPACE_INVALID";
                application.exit(12);
                return;
            }
            intermediateWorkspace->trigger();
            if (!marketStructureDock->isVisible() ||
                strategyDock->isVisible() ||
                !scaleToolbarAction->isVisible() ||
                layoutToolbarAction->isVisible()) {
                qCritical() << "SMOKE_INTERMEDIATE_WORKSPACE_INVALID";
                application.exit(13);
                return;
            }
            theoryLabAction->trigger();
            if (!strategyDock->isVisible()) {
                qCritical() << "SMOKE_THEORY_LAB_NOT_VISIBLE";
                application.exit(14);
                return;
            }
            theoryAnalyze->click();
            if (theoryTable->rowCount() < 1) {
                qCritical() << "SMOKE_THEORY_RESULTS_MISSING";
                application.exit(15);
                return;
            }
            scriptAction->trigger();
            scriptSource->setPlainText(
                QStringLiteral(
                    "//@version=6\n"
                    "strategy(\"Smoke Script\")\n"
                    "fast = ta.ema(close, 5)\n"
                    "slow = ta.ema(close, 10)\n"
                    "enterLong = ta.crossover(fast, slow)\n"
                    "exitLong = ta.crossunder(fast, slow)\n"
                    "if enterLong\n"
                    "    strategy.entry(\"Long\", strategy.long)\n"
                    "if exitLong\n"
                    "    strategy.close(\"Long\")\n"));
            scriptCompile->click();
            if (!scriptApply->isEnabled()) {
                qCritical() << "SMOKE_SCRIPT_COMPILE_FAILED";
                application.exit(16);
                return;
            }
            scriptApply->click();
            simulationAction->trigger();
            simulationStart->click();
            if (simulationSummary->text().contains(
                    QStringLiteral("inactive"),
                    Qt::CaseInsensitive) ||
                simulationSummary->text().contains(
                    QStringLiteral("could not"),
                    Qt::CaseInsensitive)) {
                qCritical() << "SMOKE_SIMULATION_START_FAILED"
                            << simulationSummary->text();
                application.exit(17);
                return;
            }
            strategyRun->click();
            indicatorTable->item(3, 0)->setCheckState(Qt::Checked);
            scaleSelector->setCurrentIndex(
                scaleSelector->findData(QStringLiteral("logarithmic")));
            QTimer::singleShot(250, &window, [indicatorTable]() {
                indicatorTable->item(4, 0)->setCheckState(Qt::Checked);
            });
            QTimer::singleShot(500, &window, [indicatorTable, scaleSelector]() {
                indicatorTable->item(3, 0)->setCheckState(Qt::Unchecked);
                indicatorTable->item(4, 0)->setCheckState(Qt::Unchecked);
                scaleSelector->setCurrentIndex(
                    scaleSelector->findData(QStringLiteral("percentage")));
            });
            QTimer::singleShot(750, &application, [&]() {
                qInfo() << "SMOKE_OK";
                timeout.stop();
                window.close();
                application.exit(0);
            });
        });
        timeout.start(20'000);
        window.show();
        return application.exec();
    }

    window.show();
    return application.exec();
}
