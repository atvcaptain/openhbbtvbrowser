#include "hardwareprofile.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>
#include <QStringList>
#include <cstdio>

QString HardwareProfile::argumentValue(int argc, char *argv[], const QString &name)
{
    const QString prefix = QStringLiteral("--%1=").arg(name);
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(prefix))
            return arg.mid(prefix.length()).trimmed();
        if (arg == QStringLiteral("--") + name && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]).trimmed();
    }
    return QString();
}

QString HardwareProfile::environmentValue(const char *name)
{
    const QByteArray value = qgetenv(name);
    return QString::fromLocal8Bit(value).trimmed();
}

QString HardwareProfile::readFirstExistingFile(const QStringList &paths)
{
    for (const QString &path : paths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QString value = QString::fromLocal8Bit(file.readAll()).trimmed();
            if (!value.isEmpty())
                return value;
        }
    }
    return QString();
}

QString HardwareProfile::readHardwareText()
{
    QStringList values;
    const QStringList paths = QStringList()
            << QStringLiteral("/proc/stb/info/boxtype")
            << QStringLiteral("/proc/stb/info/model")
            << QStringLiteral("/proc/stb/info/vumodel")
            << QStringLiteral("/proc/stb/info/chipset")
            << QStringLiteral("/proc/device-tree/model")
            << QStringLiteral("/etc/hostname");

    for (const QString &path : paths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QString value = QString::fromLocal8Bit(file.readAll()).trimmed();
            if (!value.isEmpty())
                values << value;
        }
    }
    return values.join(QLatin1Char(' ')).toLower();
}

QString HardwareProfile::modelSummary()
{
    return readHardwareText();
}

bool HardwareProfile::isTruthy(const QString &value)
{
    const QString v = value.trimmed().toLower();
    return v == QStringLiteral("1") || v == QStringLiteral("yes") ||
           v == QStringLiteral("true") || v == QStringLiteral("on") ||
           v == QStringLiteral("enabled");
}

bool HardwareProfile::isFalsy(const QString &value)
{
    const QString v = value.trimmed().toLower();
    return v == QStringLiteral("0") || v == QStringLiteral("no") ||
           v == QStringLiteral("false") || v == QStringLiteral("off") ||
           v == QStringLiteral("disabled");
}

QString HardwareProfile::detectPlatform()
{
    const QString configured = readFirstExistingFile(QStringList()
            << QStringLiteral("/etc/openhbbtvbrowser-platform")
            << QStringLiteral("/etc/openhbbtvbrowser/platform"));
    if (!configured.isEmpty())
        return configured.toLower();

    const QString hw = readHardwareText();

    const bool hasMaliDevice = QFileInfo::exists(QStringLiteral("/dev/mali")) ||
                               QFileInfo::exists(QStringLiteral("/dev/mali0")) ||
                               QFileInfo::exists(QStringLiteral("/sys/class/misc/mali")) ||
                               QFileInfo::exists(QStringLiteral("/proc/mali"));

    if (hasMaliDevice || hw.contains(QStringLiteral("mali")) ||
        hw.contains(QStringLiteral("meson")) || hw.contains(QStringLiteral("amlogic")) ||
        hw.contains(QStringLiteral("dreamone")) || hw.contains(QStringLiteral("dream one")) ||
        hw.contains(QStringLiteral("dreamtwo")) || hw.contains(QStringLiteral("dream two")) ||
        hw.contains(QStringLiteral("dreambox one")) || hw.contains(QStringLiteral("dreambox two"))) {
        return QStringLiteral("eglfs_mali");
    }

    // Some older/no-EGL framebuffer-only builds used a hard linuxfb patch.
    // Keep that path auto-selectable for known framebuffer-only profiles, but
    // prefer eglfs for unknown hardware because Qt WebEngine normally needs EGL.
    if (hw.contains(QStringLiteral("linuxfb")) || hw.contains(QStringLiteral("noegl")) ||
        hw.contains(QStringLiteral("dm800")) || hw.contains(QStringLiteral("dm500"))) {
        return QStringLiteral("linuxfb");
    }

    return QStringLiteral("eglfs");
}

void HardwareProfile::applyEnvironment(int argc, char *argv[])
{
    QString platform = argumentValue(argc, argv, QStringLiteral("openhbbtv-platform"));
    if (platform.isEmpty())
        platform = environmentValue("OPENHBBTV_QPA_PLATFORM");
    if (platform.isEmpty())
        platform = detectPlatform();
    platform = platform.toLower();

    if (qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").isNull())
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-web-security --no-sandbox");
    if (qgetenv("QTWEBENGINE_DISABLE_SANDBOX").isNull())
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    if (qgetenv("QT_QPA_FONTDIR").isNull())
        qputenv("QT_QPA_FONTDIR", "/usr/share/fonts");

    if (qgetenv("QT_QPA_PLATFORM").isNull()) {
        if (platform == QStringLiteral("linuxfb")) {
            qputenv("QT_QPA_PLATFORM", "linuxfb");
        } else {
            qputenv("QT_QPA_PLATFORM", "eglfs");
        }
    }

    if ((platform == QStringLiteral("eglfs_mali") || platform == QStringLiteral("mali") || platform == QStringLiteral("eglfs-mali")) &&
        qgetenv("QT_QPA_EGLFS_INTEGRATION").isNull()) {
        qputenv("QT_QPA_EGLFS_INTEGRATION", "eglfs_mali");
    }

    if (qgetenv("QT_QPA_EGLFS_HIDECURSOR").isNull())
        qputenv("QT_QPA_EGLFS_HIDECURSOR", "1");

    std::fprintf(stderr,
                 "[OpenHbbTV] hardware='%s' platform='%s' QT_QPA_PLATFORM='%s' EGLFS_INTEGRATION='%s'\n",
                 qPrintable(readHardwareText()),
                 qPrintable(platform),
                 qgetenv("QT_QPA_PLATFORM").constData(),
                 qgetenv("QT_QPA_EGLFS_INTEGRATION").constData());
}

QString HardwareProfile::detectRemoteDevice()
{
    QFile devices(QStringLiteral("/proc/bus/input/devices"));
    if (devices.open(QIODevice::ReadOnly)) {
        const QString content = QString::fromLocal8Bit(devices.readAll());
        const QStringList blocks = content.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts);
        QString firstEvent;
        QString firstKeyboardEvent;

        const QRegularExpression eventRe(QStringLiteral("event[0-9]+"));
        for (const QString &block : blocks) {
            const QRegularExpressionMatch match = eventRe.match(block);
            if (!match.hasMatch())
                continue;

            const QString device = QStringLiteral("/dev/input/") + match.captured(0);
            if (firstEvent.isEmpty())
                firstEvent = device;

            const QString lower = block.toLower();
            if (firstKeyboardEvent.isEmpty() && lower.contains(QStringLiteral("kbd")))
                firstKeyboardEvent = device;

            if (lower.contains(QStringLiteral("remote")) || lower.contains(QStringLiteral(" rc")) ||
                lower.contains(QStringLiteral("ir")) || lower.contains(QStringLiteral("frontpanel")) ||
                lower.contains(QStringLiteral("front panel")) || lower.contains(QStringLiteral("meson"))) {
                return device;
            }
        }

        if (!firstKeyboardEvent.isEmpty())
            return firstKeyboardEvent;
    }

    if (QFileInfo::exists(QStringLiteral("/dev/input/event1")))
        return QStringLiteral("/dev/input/event1");
    return QStringLiteral("/dev/input/event0");
}

QString HardwareProfile::remoteDevice(int argc, char *argv[])
{
    QString device = argumentValue(argc, argv, QStringLiteral("openhbbtv-remote-device"));
    if (device.isEmpty())
        device = environmentValue("OPENHBBTV_REMOTE_DEVICE");
    if (device.isEmpty() || device == QStringLiteral("auto"))
        device = detectRemoteDevice();
    return device;
}

bool HardwareProfile::filterRemoteNavigationKeys(int argc, char *argv[])
{
    QString value = argumentValue(argc, argv, QStringLiteral("openhbbtv-remote-navigation-keys"));
    if (value.isEmpty())
        value = environmentValue("OPENHBBTV_REMOTE_NAVIGATION_KEYS");

    if (isTruthy(value))
        return false;
    if (isFalsy(value))
        return true;

    // Auto mode: keep the old hardware patch behavior by default. Qt's platform
    // plugin normally injects cursor/OK keys into Chromium already; sending them
    // a second time through the synthetic HbbTV path causes duplicate navigation
    // on affected boxes. Color/media/numeric keys are still forwarded here.
    return true;
}
