#include "webview.h"
#include "browsercontrol.h"
#include "virtualkey.h"
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QFileInfo>

WebView::WebView(QWidget *parent)
    : QWebEngineView(parent), m_quitMsg(new QLabel), m_quitMsgStatus(0)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    m_quitMsg->setText("Back: no browser history");
    m_quitMsg->setGeometry(0, 0, 480, 120);
    m_quitMsg->setAlignment(Qt::AlignCenter);
    m_quitMsg->setStyleSheet("background: black; color: white; font: 24pt;");
    int x = (screenGeometry.width() - m_quitMsg->width()) / 2;
    int y = (screenGeometry.height() - m_quitMsg->height()) / 2;
    m_quitMsg->setGeometry(x, y, 480, 120);
    m_quitMsg->hide();

    connect(this, &QWebEngineView::titleChanged, this, &WebView::titleChanged);
    connect(this, &QWebEngineView::loadStarted, this, [this]() { qDebug() << "[OpenHbbTV] loadStarted" << url().toString(); });
    connect(this, &QWebEngineView::loadProgress, this, [this](int progress) { qDebug() << "[OpenHbbTV] loadProgress" << progress << url().toString(); });
    connect(this, &QWebEngineView::urlChanged, this, [this](const QUrl &u) { qDebug() << "[OpenHbbTV] urlChanged" << u.toString(); });
    connect(this, &QWebEngineView::loadFinished, this, &WebView::loadFinished);
    connect(page(), &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus status, int exitCode) {
                qWarning() << "[OpenHbbTV] renderProcessTerminated" << status << exitCode << url().toString();
            });
}

void WebView::injectHbbTVScripts(const QString &src)
{
    QString normalized = src;
    if (normalized.startsWith("qrc:/")) {
        normalized.replace(0, 4, ":");   // "qrc:/foo.js" -> ":/foo.js"
    } else if (normalized.startsWith(":/")) {
        // già pronto per QFile
    } else if (QFileInfo(normalized).isRelative()) {
        // se è relativo, lo trasformo in qrc
        normalized.prepend(":/");
    }
    QFile polyfill(normalized);
    if (polyfill.open(QIODevice::ReadOnly)) {
        QString source = QString::fromUtf8(polyfill.readAll());
        polyfill.close();

        QWebEngineScript script;
        script.setName("hbbtv_polyfill");
        script.setSourceCode(source);
        script.setInjectionPoint(QWebEngineScript::DocumentCreation);
        script.setRunsOnSubFrames(true);
        script.setWorldId(QWebEngineScript::MainWorld);
        page()->scripts().insert(script);

        qDebug() << "[HbbTV] Injected polyfill from" << src;
    } else {
        qWarning() << "[HbbTV] Polyfill not found:" << src;
    }
}

void WebView::injectXmlHttpRequestScripts()
{
    QFile quirks(":/xmlhttprequest_quirks.js");
    if (quirks.open(QIODevice::ReadOnly)) {
        QString source = QString::fromUtf8(quirks.readAll());
        quirks.close();

        QWebEngineScript script;
        script.setName("xmlhttprequest_quirks");
        script.setSourceCode(source);
        script.setInjectionPoint(QWebEngineScript::DocumentCreation);
        script.setRunsOnSubFrames(true);
        script.setWorldId(QWebEngineScript::MainWorld);
        page()->scripts().insert(script);

        qDebug() << "[HbbTV] xmlhttprequest_quirks injected via QWebEngineScript";
    } else {
        qWarning() << "[HbbTV] xmlhttprequest_quirks.js not found in qrc";
    }
}

void WebView::setCurrentChannel(const int &onid, const int &tsid, const int &sid)
{
    QWebEngineScript script;

    QString s = QString::fromLatin1("(function() {"
                                    "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
                                    "  window.HBBTV_POLYFILL_NS.currentChannel = {"
                                    "    onid : %1,"
                                    "    tsid : %2,"
                                    "    sid  : %3,"
                                    "    ccid : 'ccid:dvbt.%1.%2.%3'"
                                    "  };"
                                    "})();").arg(onid).arg(tsid).arg(sid);

    script.setName("current_channel");
    script.setSourceCode(s);
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setRunsOnSubFrames(true);
    script.setWorldId(QWebEngineScript::MainWorld);
    page()->scripts().insert(script);
    qDebug() << "[OpenHbbTV] setCurrentChannel" << onid << tsid << sid;
    page()->runJavaScript(s);
}

void WebView::setStreamState(int state, int error)
{
    qDebug() << "[OpenHbbTV] setStreamState" << state << error;
    QString s = QString::fromLatin1("(function() {"
                                    "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
                                    "  if (typeof window.HBBTV_POLYFILL_NS.setStreamState === 'function') {"
                                    "    window.HBBTV_POLYFILL_NS.setStreamState(%1, %2);"
                                    "  } else {"
                                    "    window.HBBTV_POLYFILL_NS.pendingStreamState = [%1, %2];"
                                    "  }"
                                    "})();").arg(state).arg(error);
    page()->runJavaScript(s);
}

void WebView::setLanguage(const QString &language)
{
    QWebEngineScript script;

    QString s = QString::fromLatin1("(function() {"
                                    "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
                                    "  window.HBBTV_POLYFILL_NS.preferredLanguage = '%1';"
                                    "})();").arg(language);

    script.setName("preferred_language");
    script.setSourceCode(s);
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setRunsOnSubFrames(true);
    script.setWorldId(QWebEngineScript::MainWorld);
    page()->scripts().insert(script);
}

void WebView::setScriptDebugging(const QString &scriptDebugging)
{
    QWebEngineScript script;

    QString s = QString::fromLatin1("(function() {"
                                    "  window.HBBTV_POLYFILL_DEBUG = %1;"
                                    "})();").arg(scriptDebugging);

    script.setName("hbbtv_polyfill_debug");
    script.setSourceCode(s);
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setRunsOnSubFrames(true);
    script.setWorldId(QWebEngineScript::MainWorld);
    page()->scripts().insert(script);
}

void WebView::sendKeyEvent(const int &keyCode)
{
    qDebug() << "[OpenHbbTV] sendKeyEvent" << keyCode;
    if (keyCode == VirtualKey::VK_BACK) {
        if (!page()->history()->canGoBack()) {
            if (!m_quitMsgStatus) {
                m_quitMsg->show();
                m_quitMsgStatus = 1;
            } else {
                m_quitMsg->hide();
                m_quitMsgStatus = 0;
            }
        }
    } else {
        if (m_quitMsgStatus) {
            m_quitMsg->hide();
            m_quitMsgStatus = 0;
        }
    }

    QMetaEnum metaEnum = QMetaEnum::fromType<VirtualKey::VirtualKeyType>();

    QString s = QString::fromLatin1("(function() {"
                                    "  var target = document.activeElement || document.body || document.documentElement || document;"
                                    "  function makeEvent(type) {"
                                    "    var e = new KeyboardEvent(type, {"
                                    "      bubbles : true,"
                                    "      cancelable : true,"
                                    "      keyCode : %1,"
                                    "      which : %1"
                                    "    });"
                                    "    if (window['%2'] !== 'undefined') {"
                                    "      try { delete e.keyCode; } catch (ignore) {}"
                                    "      try { delete e.which; } catch (ignore) {}"
                                    "      Object.defineProperty(e, 'keyCode', { value: window['%2'] });"
                                    "      Object.defineProperty(e, 'which', { value: window['%2'] });"
                                    "    }"
                                    "    return e;"
                                    "  }"
                                    "  target.dispatchEvent(makeEvent('keydown'));"
                                    "  target.dispatchEvent(makeEvent('keyup'));"
                                    "  if (%1 == 13) {"
                                    "    var mouseEvent = new MouseEvent('click', {"
                                    "      bubbles : true,"
                                    "      cancelable : true"
                                    "    });"
                                    "    target.dispatchEvent(mouseEvent);"
                                    "  }"
                                    "})();").arg(keyCode).arg(metaEnum.valueToKey(keyCode));
    qDebug() << "[OpenHbbTV] inject key" << keyCode;
    page()->runJavaScript(s);
}

void WebView::dispatchHbbtvBridgeCommand(const QString &rawCommand)
{
    qDebug() << "[OpenHbbTV] raw bridge command" << rawCommand;
    QString command = rawCommand;
    const int seqPos = command.lastIndexOf(QStringLiteral("||"));
    if (seqPos >= 0)
        command.truncate(seqPos);

    qDebug() << "[OpenHbbTV] normalized bridge command" << command;

    if (command == QStringLiteral("BROADCAST_PLAY")) {
        emit hbbtvCommand(CommandClient::CommandBroadcastPlay, QString());
    } else if (command == QStringLiteral("BROADCAST_STOP")) {
        emit hbbtvCommand(CommandClient::CommandBroadcastStop, QString());
    } else if (command == QStringLiteral("BROADCAST_HIDDEN")) {
        emit hbbtvCommand(CommandClient::CommandBroadcastHidden, QString());
    } else if (command == QStringLiteral("UNSET_VIDEO_WINDOW")) {
        emit hbbtvCommand(CommandClient::CommandUnsetVideoWindow, QString());
    } else if (command.startsWith(QStringLiteral("SET_VIDEO_WINDOW:"))) {
        emit hbbtvCommand(CommandClient::CommandSetVideoWindow, command.mid(17));
    } else if (command.startsWith(QStringLiteral("PLAY_STREAM:"))) {
        emit hbbtvCommand(CommandClient::CommandPlayStream, command.mid(12));
    } else if (command == QStringLiteral("STOP_STREAM")) {
        emit hbbtvCommand(CommandClient::CommandStopStream, QString());
    } else if (command == QStringLiteral("PAUSE_STREAM")) {
        emit hbbtvCommand(CommandClient::CommandPauseStream, QString());
    } else if (command.startsWith(QStringLiteral("SEEK_STREAM:"))) {
        emit hbbtvCommand(CommandClient::CommandSeekStream, command.mid(12));
    } else if (command.startsWith(QStringLiteral("CREATE_APPLICATION:"))) {
        emit hbbtvCommand(CommandClient::CommandCreateApplication, command.mid(19));
    } else if (command == QStringLiteral("RESTORE_BROADCAST")) {
        emit hbbtvCommand(CommandClient::CommandRestoreBroadcast, QString());
    } else if (command.startsWith(QStringLiteral("SET_CHANNEL:"))) {
        emit hbbtvCommand(CommandClient::CommandSetChannel, command.mid(12));
    } else if (command == QStringLiteral("PREV_CHANNEL")) {
        emit hbbtvCommand(CommandClient::CommandPrevChannel, QString());
    } else if (command == QStringLiteral("NEXT_CHANNEL")) {
        emit hbbtvCommand(CommandClient::CommandNextChannel, QString());
    } else if (command.startsWith(QStringLiteral("LOG:"))) {
        emit hbbtvCommand(CommandClient::CommandLog, command.mid(4));
    } else {
        qDebug() << "Unhandled HbbTV bridge command" << command;
    }
}

void WebView::titleChanged(const QString &title)
{
    qDebug() << "[OpenHbbTV] titleChanged" << title;
    if (title.startsWith(QStringLiteral("OPENATV_HBBTV:"))) {
        dispatchHbbtvBridgeCommand(title.mid(14));
    } else if (title.startsWith(QStringLiteral("OipfVideoBroadcastEmbeddedObject"))) {
        // Legacy polyfill signal. The new video/broadcast mapper sends exact state commands.
    } else if (title.startsWith(QStringLiteral("OipfAVControlObject:"))) {
        // Legacy polyfill signal. Playback starts only after PLAY_STREAM from the mapper.
    } else if (title.startsWith(QStringLiteral("createApplication:"))) {
        emit hbbtvCommand(CommandClient::CommandCreateApplication, title.mid(18));
    }
}

void WebView::loadFinished(bool ok)
{
    qDebug() << "[OpenHbbTV] loadFinished" << ok << url().toString();
    if (ok) {
        if (size().width() == 1920 && size().height() == 1080)
            page()->runJavaScript(QString::fromLatin1("document.body.style.setProperty('zoom', '150%');"));

        page()->runJavaScript(QString::fromLatin1("document.body.style.setProperty('overflow', 'hidden');"));

        page()->runJavaScript(QString::fromLatin1("if (document.getElementById('appmgr')) document.getElementById('appmgr').style.setProperty('visibility', 'hidden');"));
        page()->runJavaScript(QString::fromLatin1("if (document.getElementById('oipfcfg')) document.getElementById('oipfcfg').style.setProperty('visibility', 'hidden');"));
        page()->runJavaScript(QString::fromLatin1("if (document.getElementById('oipfCap')) document.getElementById('oipfCap').style.setProperty('visibility', 'hidden');"));
        page()->runJavaScript(QString::fromLatin1("if (document.getElementById('oipfDrm')) document.getElementById('oipfDrm').style.setProperty('visibility', 'hidden');"));
        emit hbbtvCommand(CommandClient::CommandPageLoadFinished, url().toString());
    }
}
