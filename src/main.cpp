#include "main_window.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("buicongnguyen"));
    QCoreApplication::setApplicationName(QStringLiteral("TradingViewChart"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Offline-first Qt chart viewer using TradingView Lightweight Charts"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Exit successfully after the embedded chart reports ready."));
    parser.addOption(smokeOption);
    parser.process(application);

    tvchart::MainWindow window;
    if (parser.isSet(smokeOption)) {
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &application, [&application]() {
            qCritical() << "SMOKE_TIMEOUT";
            application.exit(2);
        });
        QObject::connect(&window, &tvchart::MainWindow::chartReady, &application, [&]() {
            qInfo() << "SMOKE_OK";
            timeout.stop();
            window.close();
            QTimer::singleShot(1'000, &application, [&application]() {
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
