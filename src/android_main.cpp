#include "mobile/mobile_controller.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtWebView/QtWebView>

int main(int argc, char* argv[]) {
    QtWebView::initialize();
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("buicongnguyen"));
    QCoreApplication::setApplicationName(QStringLiteral("TradeChartLab"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral(TRADINGVIEW_CHART_VERSION));

    tvchart::MobileController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("mobileController"),
        &controller);
    engine.loadFromModule(QStringLiteral("TradingViewChart"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return application.exec();
}
