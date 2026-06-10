#include "debuglog.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QTextStream>
#include <QtGlobal>
#include <cstdio>

static QMutex g_logMutex;
static QString g_logPath;

static QString messageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return QStringLiteral("DEBUG");
    case QtInfoMsg:     return QStringLiteral("INFO");
    case QtWarningMsg:  return QStringLiteral("WARNING");
    case QtCriticalMsg: return QStringLiteral("CRITICAL");
    case QtFatalMsg:    return QStringLiteral("FATAL");
    }
    return QStringLiteral("LOG");
}

static void openHbbTVDebugMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const QString line = QStringLiteral("%1 [%2] %3%4%5\n")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")))
            .arg(messageTypeName(type),
                 msg,
                 context.file ? QStringLiteral(" (") + QString::fromLocal8Bit(context.file) : QString(),
                 context.file ? QStringLiteral(":") + QString::number(context.line) + QStringLiteral(")") : QString());

    {
        QMutexLocker locker(&g_logMutex);
        QFile file(g_logPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            file.write(line.toUtf8());
            file.close();
        }
    }

    std::fwrite(line.toLocal8Bit().constData(), 1, line.toLocal8Bit().size(), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg)
        abort();
}

void installOpenHbbTVDebugLogger()
{
    const QByteArray envPath = qgetenv("OPENHBBTV_LOGFILE");
    g_logPath = QString::fromLocal8Bit(envPath.isEmpty() ? QByteArrayLiteral("/tmp/openhbbtvbrowser-debug.log") : envPath);

    QFile file(g_logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&file);
        out << "===== OpenHbbTV browser start "
            << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
            << " =====\n";
        file.close();
    }

    qInstallMessageHandler(openHbbTVDebugMessageHandler);
}
