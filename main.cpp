#include "browsercontrol.h"
#include "browserwindow.h"
#include "debuglog.h"
#include "hardwareprofile.h"
#include "webview.h"
#include <QApplication>
#include <QByteArray>
#include <QCursor>
#include <QCommandLineParser>
#include <QDir>
#include <QLockFile>
#include <QUrl>
#include <QWebEngineSettings>
#include <QWebEngineProfile>
#include <QList>
#include <QScreen>

#if defined(EMBEDDED_BUILD)
#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <stddef.h>

int mkdir_mount_devshm(void)
{
    const char mountpoint[] = "/dev/shm";
    struct stat s;

    if (stat(mountpoint, &s) == -1) {
        if (errno == ENOENT) {
            if (mkdir(mountpoint, 0755))
                return -1;

            if (mount("tmpfs", mountpoint, "tmpfs", 0, NULL))
                return -1;
        } else {
            return -1;
        }
    }

    return 0;
}
#endif

QUrl commandLineUrlArgument()
{
    const QStringList args = QCoreApplication::arguments();
    for (const QString &arg : args.mid(1)) {
        if (!arg.startsWith(QLatin1Char('-')))
            return QUrl::fromUserInput(arg);
    }
    return QUrl();
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_EGLFS_HIDECURSOR", QByteArrayLiteral("1"));
    installOpenHbbTVDebugLogger();
    qDebug() << "[OpenHbbTV] process start argc" << argc;
    qDebug() << "[OpenHbbTV] process build id e2-rcu-owner-live-switch-dash-controls-20260611";
    qDebug() << "[OpenHbbTV] build mode e2-rcu-owner-live-switch-dash-controls";
#if defined(EMBEDDED_BUILD)
    HardwareProfile::applyEnvironment(argc, argv);

    if (mkdir_mount_devshm())
        return 1;
#endif

    QCoreApplication::setOrganizationName(QLatin1String("Qt"));
    QCoreApplication::setApplicationName("openhbbtvbrowser");
    QCoreApplication::setApplicationVersion(QLatin1String("0.1"));
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QByteArrayList args = QByteArrayList()
            << QByteArrayLiteral("--disable-web-security")
            << QByteArrayLiteral("--no-sandbox")
            << QByteArrayLiteral("--log-level=0");
    const int count = args.size() + argc;
    QVector<char *> qargv(count);

    qargv[0] = argv[0];
    for (int i = 0; i < args.size(); ++i)
        qargv[i + 1] = args[i].data();
    for (int i = args.size() + 1; i < count; ++i)
        qargv[i] = argv[i - args.size()];

    int qAppArgCount = qargv.size();

    QApplication app(qAppArgCount, qargv.data());
    app.setOverrideCursor(QCursor(Qt::BlankCursor));

#if defined(EMBEDDED_BUILD)
    QLockFile lockFile(QDir::tempPath() + "/openhbbtvbrowser.lock");
    if(!lockFile.tryLock(100)) {
        qDebug() << "The application is already running.";
        return 1;
    }
#endif

    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, true);
    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, true);
    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, true);
    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);
    QWebEngineProfile::defaultProfile()->setHttpUserAgent(
        QWebEngineProfile::defaultProfile()->httpUserAgent() + QStringLiteral(" HbbTV/1.4.1 SmartTV2015"));

    QString src = QStringLiteral("qrc:/hbbtv_polyfill.js");
    int onid = -1;
    int tsid = -1;
    int sid = -1;

    QCommandLineParser parser;
    parser.setApplicationDescription("OpenHbbTVBrowser");
    parser.addOption(QCommandLineOption("src", "Script Src", "src"));
    parser.addOption(QCommandLineOption("onid", "Original Network ID", "onid"));
    parser.addOption(QCommandLineOption("tsid", "Transport Stream ID", "tsid"));
    parser.addOption(QCommandLineOption("sid", "Service ID", "sid"));
    parser.addOption(QCommandLineOption("enable-script-debugging", "EnableScript Debugging"));
    parser.addOption(QCommandLineOption("enable-netlog", "Enable HTTP request logging"));
    parser.addOption(QCommandLineOption("openhbbtv-platform", "Runtime platform backend: auto, eglfs, eglfs_mali or linuxfb", "platform"));
    parser.addOption(QCommandLineOption("openhbbtv-remote-device", "Remote input device path or auto", "device"));
    parser.addOption(QCommandLineOption("openhbbtv-remote-navigation-keys", "Forward navigation/OK keys from direct evdev reader: auto, on or off", "mode"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.parse(QCoreApplication::arguments());
    if (parser.isSet("help"))
        parser.showHelp();
    if (parser.isSet("version"))
        parser.showVersion();
    if (parser.isSet("src"))
        src = parser.value("src");
    if (parser.isSet("onid"))
        onid = parser.value("onid").toInt();
    if (parser.isSet("tsid"))
        tsid = parser.value("tsid").toInt();
    if (parser.isSet("sid"))
        sid = parser.value("sid").toInt();
    bool scriptDebugging = parser.isSet("enable-script-debugging");
    bool enableNetlog = parser.isSet("enable-netlog");

    QUrl url = commandLineUrlArgument();
    qDebug() << "[OpenHbbTV] command line url" << url.toString();

    auto window = new BrowserWindow();
#if defined(EMBEDDED_BUILD)
    window->setWindowFlags(Qt::FramelessWindowHint);
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->setStyleSheet("background: transparent;");
#else
    window->resize(1280, 720);
#endif
    window->webView()->injectHbbTVScripts(src);
    window->webView()->injectXmlHttpRequestScripts();
    if (onid != -1 && tsid != -1 && sid != -1)
        window->webView()->setCurrentChannel(onid, tsid, sid);
    window->webView()->setLanguage(QStringLiteral("DEU")); // TODO:
    window->webView()->setScriptDebugging(scriptDebugging ? QStringLiteral("true") : QStringLiteral("false"));
    qDebug() << "[OpenHbbTV] set initial url" << url.toString();
    window->webView()->setInitialUrl(url);
    window->webView()->setUrl(url);
#if defined(EMBEDDED_BUILD)
    QSize embeddedWindowSize;
    const QString vuplSize = QString::fromLocal8Bit(qgetenv("EGLFS_LIBVUPL_SIZE")).trimmed();
    const QStringList vuplParts = vuplSize.split(QLatin1Char('x'));
    if (vuplParts.size() == 2) {
        bool okW = false;
        bool okH = false;
        const int w = vuplParts.at(0).toInt(&okW);
        const int h = vuplParts.at(1).toInt(&okH);
        if (okW && okH && w > 0 && h > 0)
            embeddedWindowSize = QSize(w, h);
    }
    if (!embeddedWindowSize.isValid() && app.primaryScreen())
        embeddedWindowSize = app.primaryScreen()->size();
    if (embeddedWindowSize.isValid()) {
        window->resize(embeddedWindowSize);
        qDebug() << "[OpenHbbTV] resize embedded window" << embeddedWindowSize;
    }
    qDebug() << "[OpenHbbTV] showFullScreen" << window->geometry();
    window->showFullScreen();
#else
    qDebug() << "[OpenHbbTV] show" << window->geometry();
    window->show();
#endif

#if defined(EMBEDDED_BUILD)
    const QString disableEvdev = QString::fromLocal8Bit(qgetenv("OPENHBBTV_DISABLE_EVDEV")).trimmed().toLower();
    const bool e2OwnsRcu = disableEvdev == QStringLiteral("1") ||
                           disableEvdev == QStringLiteral("yes") ||
                           disableEvdev == QStringLiteral("true") ||
                           disableEvdev == QStringLiteral("on") ||
                           disableEvdev == QStringLiteral("enabled");
    QList<RemoteController *> remotes;
    if (e2OwnsRcu) {
        qDebug() << "[OpenHbbTV] direct evdev input disabled; E2 owns RCU";
        qDebug() << "[OpenHbbTV] no browser evdev reader will be opened in E2 RCU owner mode";
    } else {
        const QStringList remoteDevices = HardwareProfile::remoteDevices(argc, argv);
        const bool filterNavigationKeys = HardwareProfile::filterRemoteNavigationKeys(argc, argv);
        qDebug() << "[OpenHbbTV] remote devices" << remoteDevices
                 << "direct-navigation-keys" << (!filterNavigationKeys);
        for (const QString &remoteDevice : remoteDevices) {
            auto remote = new RemoteController(remoteDevice, filterNavigationKeys);
            remote->setParent(window);
            remotes << remote;
            QObject::connect(remote, &RemoteController::activate, window->webView(), &WebView::sendKeyEvent);
        }
    }
#else
    auto filter = new WindowEventFilter();
    app.installEventFilter(filter);
    QObject::connect(filter, &WindowEventFilter::activate, window->webView(), &WebView::sendKeyEvent);
#endif

    if (enableNetlog) {
        qDebug() << "[NET] RequestLogger enabled";
        window->webView()->page()->profile()->setUrlRequestInterceptor(new RequestLogger());
    }

    int result = app.exec();
    qDebug() << "[OpenHbbTV] process exit" << result;
    return result;
}
