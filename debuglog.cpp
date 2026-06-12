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

#ifndef OPENHBBTV_DEBUG_DEFAULT
#define OPENHBBTV_DEBUG_DEFAULT 1
#endif

static bool isFalseValue(QByteArray value)
{
    value = value.trimmed().toLower();
    return value == QByteArrayLiteral("0")
        || value == QByteArrayLiteral("no")
        || value == QByteArrayLiteral("false")
        || value == QByteArrayLiteral("off")
        || value == QByteArrayLiteral("disabled");
}

static bool isTrueValue(QByteArray value)
{
    value = value.trimmed().toLower();
    return value == QByteArrayLiteral("1")
        || value == QByteArrayLiteral("yes")
        || value == QByteArrayLiteral("true")
        || value == QByteArrayLiteral("on")
        || value == QByteArrayLiteral("enabled");
}

static bool openHbbTVDebugEnabled()
{
    const QByteArray env = qgetenv("OPENHBBTV_DEBUG");
    if (!env.trimmed().isEmpty())
        return !isFalseValue(env);
    return OPENHBBTV_DEBUG_DEFAULT != 0;
}

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

static void openHbbTVQuietMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context)

    if (type == QtDebugMsg || type == QtInfoMsg)
        return;

    const QString line = QStringLiteral("%1 [%2] %3\n")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
                 messageTypeName(type),
                 msg);
    std::fwrite(line.toLocal8Bit().constData(), 1, line.toLocal8Bit().size(), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg)
        abort();
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
    if (!openHbbTVDebugEnabled()) {
        qInstallMessageHandler(openHbbTVQuietMessageHandler);
        return;
    }

    const QByteArray envPath = qgetenv("OPENHBBTV_LOGFILE");
    g_logPath = QString::fromLocal8Bit(envPath.isEmpty() ? QByteArrayLiteral("/tmp/openhbbtvbrowser-debug.log") : envPath);

    QFile file(g_logPath);
    const QIODevice::OpenMode mode = isTrueValue(qgetenv("OPENHBBTV_LOG_TRUNCATE"))
            ? QIODevice::Truncate : QIODevice::Append;
    if (file.open(QIODevice::WriteOnly | mode | QIODevice::Text)) {
        QTextStream out(&file);
        out << "===== OpenHbbTV browser start "
            << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
            << " =====\n";
        file.close();
    }

    qInstallMessageHandler(openHbbTVDebugMessageHandler);
}
