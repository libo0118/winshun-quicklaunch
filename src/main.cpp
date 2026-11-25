#include "protocol/Protocol.h"
#include "ui/AppController.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

void stderrMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Q_UNUSED(ctx);
    const char* tag = "info";
    switch (type) {
    case QtDebugMsg: tag = "debug"; break;
    case QtInfoMsg: tag = "info"; break;
    case QtWarningMsg: tag = "warn"; break;
    case QtCriticalMsg: tag = "crit"; break;
    case QtFatalMsg: tag = "fatal"; break;
    }
    const QByteArray utf8 = msg.toUtf8();
    std::fprintf(stderr, "[quicklaunch][%s] %s\n", tag, utf8.constData());
    std::fflush(stderr);
    if (type == QtFatalMsg) {
        std::abort();
    }
}

bool hasArg(int argc, char* argv[], const char* flag)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) {
            return true;
        }
    }
    return false;
}

QString argValue(int argc, char* argv[], const char* prefix)
{
    const std::size_t len = std::strlen(prefix);
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], prefix, len) == 0) {
            return QString::fromUtf8(argv[i] + len);
        }
    }
    return {};
}

} // namespace

int main(int argc, char* argv[])
{
    qInstallMessageHandler(stderrMessageHandler);

    const bool standalone = hasArg(argc, argv, "--standalone");
    QByteArray pending;
    QString initialDataDir = argValue(argc, argv, "--data-dir=");
    if (initialDataDir.isEmpty()) {
        initialDataDir = qEnvironmentVariable("WS_DATA_DIR");
    }

    if (!standalone) {
        const auto firstObj = ql::Protocol::readFirst(&pending);
        if (!firstObj) {
            std::fprintf(stderr, "[quicklaunch][fatal] Missing initialize message on stdin.\n");
            std::fflush(stderr);
            return 1;
        }
        const QJsonObject params = firstObj->value(QStringLiteral("params")).toObject();
        const QString renderer = params.value(QStringLiteral("appearance")).toObject()
            .value(QStringLiteral("renderer")).toString();
        if (renderer == QLatin1String("software")) {
            qputenv("QT_QUICK_BACKEND", "software");
        }
        if (initialDataDir.isEmpty()) {
            initialDataDir = params.value(QStringLiteral("dataDir")).toString();
        }
        QByteArray firstLine = QJsonDocument(*firstObj).toJson(QJsonDocument::Compact);
        firstLine.append('\n');
        pending.prepend(firstLine);
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QuickLaunch"));
    QCoreApplication::setOrganizationName(QStringLiteral("libo0118"));
    app.setQuitOnLastWindowClosed(standalone);

    std::unique_ptr<ql::Protocol> protocol;
    if (!standalone) {
        protocol = std::make_unique<ql::Protocol>(std::move(pending));
    }
    if (initialDataDir.isEmpty()) {
        initialDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    ql::AppController controller(protocol.get(), initialDataDir, standalone);
    if (protocol) {
        protocol->start();
    }
    return app.exec();
}
