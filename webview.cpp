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
#include <QTimer>

WebView::WebView(QWidget *parent)
    : QWebEngineView(parent)
    , m_streamState(0)
    , m_teletextReturnInProgress(false)
    , m_quitMsg(new QLabel)
    , m_teletextDigitTimer(new QTimer(this))
    , m_quitMsgStatus(0)
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

    m_teletextDigitTimer->setSingleShot(true);
    m_teletextDigitTimer->setInterval(1200);
    connect(m_teletextDigitTimer, &QTimer::timeout, this, &WebView::flushTeletextDigitBuffer);

    connect(this, &QWebEngineView::titleChanged, this, &WebView::titleChanged);
    connect(this, &QWebEngineView::loadStarted, this, [this]() { qDebug() << "[OpenHbbTV] loadStarted" << url().toString(); });
    connect(this, &QWebEngineView::loadProgress, this, [this](int progress) { qDebug() << "[OpenHbbTV] loadProgress" << progress << url().toString(); });
    connect(this, &QWebEngineView::urlChanged, this, [this](const QUrl &u) {
        qDebug() << "[OpenHbbTV] urlChanged" << u.toString();
        if (m_teletextReturnInProgress && isTeletextUrl(u)) {
            qDebug() << "[OpenHbbTV] block teletext reload during leading-zero return" << u.toString();
            stop();
            setUrl(QUrl(QStringLiteral("about:blank")));
            loadInitialUrlAfterTeletextReturn(120);
        }
    });
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

void WebView::setBroadcastInfo(const QString &json)
{
    qDebug() << "[OpenHbbTV] setBroadcastInfo" << json.left(240);
    m_lastBroadcastInfo = json;
    const QByteArray encoded = json.toUtf8().toBase64();
    QString s = QString::fromLatin1(
        "(function() {"
        "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
        "  var raw = '';"
        "  try { raw = decodeURIComponent(escape(atob('%1'))); } catch (e) { raw = '{}'; }"
        "  try {"
        "    var info = JSON.parse(raw);"
        "    window.HBBTV_POLYFILL_NS.broadcastInfo = info;"
        "    if (info.channel) {"
        "      window.HBBTV_POLYFILL_NS.currentChannel = info.channel;"
        "    }"
        "    window.HBBTV_POLYFILL_NS.broadcastProgrammes = info.programmes || [];"
        "    if (typeof window.HBBTV_POLYFILL_NS.applyBroadcastInfo === 'function') {"
        "      window.HBBTV_POLYFILL_NS.applyBroadcastInfo(info);"
        "    }"
        "  } catch (e) {"
        "    console.log('OpenHbbTV setBroadcastInfo failed', e);"
        "  }"
        "})();").arg(QString::fromLatin1(encoded));
    page()->runJavaScript(s);
}


void WebView::showApplicationOverlay(const QString &reason)
{
    qDebug() << "[OpenHbbTV] show application overlay" << reason;
    page()->runJavaScript(QString::fromLatin1(
        "(function() {"
        "  try { if (document.documentElement) document.documentElement.style.visibility = 'visible'; } catch (e) {}"
        "  try { if (document.body) { document.body.style.visibility = 'visible'; document.body.style.display = ''; document.body.style.opacity = '1'; if (document.body.focus) document.body.focus(); } } catch (e) {}"
        "  try {"
        "    if (window.oipfApplicationManager && window.oipfApplicationManager.getOwnerApplication) {"
        "      var app = window.oipfApplicationManager.getOwnerApplication(document);"
        "      if (app && app.show) app.show();"
        "    }"
        "  } catch (e) {}"
        "  try { window.dispatchEvent(new Event('focus')); document.dispatchEvent(new Event('focus')); } catch (e) {}"
        "})();"));
}

void WebView::refreshApplicationAfterTeletextReturn()
{
    qDebug() << "[OpenHbbTV] refresh application after teletext return";
    page()->runJavaScript(QString::fromLatin1(
        "(function() {"
        "  try { document.body.style.visibility = 'visible'; } catch (e) {}"
        "  try {"
        "    if (window.oipfApplicationManager && window.oipfApplicationManager.getOwnerApplication) {"
        "      var app = window.oipfApplicationManager.getOwnerApplication(document);"
        "      if (app && app.show) app.show();"
        "    }"
        "  } catch (e) {}"
        "  try {"
        "    if (window.HBBTV_POLYFILL_NS && window.HBBTV_POLYFILL_NS.broadcastInfo &&"
        "        typeof window.HBBTV_POLYFILL_NS.applyBroadcastInfo === 'function') {"
        "      window.HBBTV_POLYFILL_NS.applyBroadcastInfo(window.HBBTV_POLYFILL_NS.broadcastInfo);"
        "    }"
        "  } catch (e) {}"
        "  try { window.dispatchEvent(new Event('focus')); document.dispatchEvent(new Event('focus')); } catch (e) {}"
        "})();"));
    if (!m_lastBroadcastInfo.isEmpty())
        QTimer::singleShot(150, this, [this]() { setBroadcastInfo(m_lastBroadcastInfo); });
    if (!m_lastBroadcastInfo.isEmpty())
        QTimer::singleShot(650, this, [this]() { setBroadcastInfo(m_lastBroadcastInfo); });
}

void WebView::setStreamState(int state, int error)
{
    m_streamState = state;
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
    if (state == 0) {
        showApplicationOverlay(QStringLiteral("stream state stopped"));
        QTimer::singleShot(150, this, [this]() { showApplicationOverlay(QStringLiteral("stream state stopped retry 1")); });
        QTimer::singleShot(650, this, [this]() { showApplicationOverlay(QStringLiteral("stream state stopped retry 2")); });
    }
}

bool WebView::isStreamActive() const
{
    return m_streamState == 1 || m_streamState == 2;
}

bool WebView::handleStreamKeyFallback(int keyCode)
{
    if (!isStreamActive())
        return false;

    switch (keyCode) {
    case VirtualKey::VK_ENTER:
        qDebug() << "[OpenHbbTV] stream enter key: show application overlay";
        showApplicationOverlay(QStringLiteral("stream enter"));
        return false;
    case VirtualKey::VK_STOP:
    case VirtualKey::VK_BACK:
        qDebug() << "[OpenHbbTV] direct stream stop fallback for key" << keyCode;
        m_streamState = 0;
        emit hbbtvCommand(CommandClient::CommandStopStream, QString());
        showApplicationOverlay(QStringLiteral("direct stream stop fallback"));
        return true;
    case VirtualKey::VK_PAUSE:
        qDebug() << "[OpenHbbTV] direct stream pause fallback for key" << keyCode;
        emit hbbtvCommand(CommandClient::CommandPauseStream, QString());
        return true;
    default:
        break;
    }

    return false;
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

void WebView::setInitialUrl(const QUrl &url)
{
    m_initialUrl = url;
    qDebug() << "[OpenHbbTV] initial url stored" << m_initialUrl.toString();
}

bool WebView::isTeletextUrl(const QUrl &candidate) const
{
    const QString host = candidate.host().toLower();
    const QString path = candidate.path().toLower();
    const QString full = candidate.toString().toLower();

    return host.startsWith(QStringLiteral("vtx.")) ||
           host.contains(QStringLiteral("videotext")) ||
           path.contains(QStringLiteral("videotext")) ||
           full.contains(QStringLiteral("vtx."));
}

bool WebView::isTeletextUrl() const
{
    return isTeletextUrl(url());
}

bool WebView::isInitialUrl(const QUrl &candidate) const
{
    if (!m_initialUrl.isValid() || m_initialUrl.isEmpty())
        return false;

    const QUrl normalizedCandidate = candidate.adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery);
    const QUrl normalizedInitial = m_initialUrl.adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery);
    return normalizedCandidate == normalizedInitial;
}

void WebView::loadInitialUrlAfterTeletextReturn(int delayMs)
{
    if (!m_initialUrl.isValid() || m_initialUrl.isEmpty()) {
        qDebug() << "[OpenHbbTV] teletext return requested but initial url is empty";
        m_teletextReturnInProgress = false;
        return;
    }

    QTimer::singleShot(delayMs, this, [this]() {
        if (!m_teletextReturnInProgress)
            return;
        qDebug() << "[OpenHbbTV] force teletext return url" << m_initialUrl.toString();
        setUrl(m_initialUrl);
    });
}

void WebView::beginTeletextReturn()
{
    if (m_teletextReturnInProgress) {
        qDebug() << "[OpenHbbTV] ignore repeated teletext leading zero during return";
        return;
    }

    qDebug() << "[OpenHbbTV] teletext leading zero request fresh red-button restart" << m_initialUrl.toString();
    m_teletextReturnInProgress = true;
    m_teletextDigitBuffer.clear();
    m_teletextDigitTimer->stop();

    // Do not forward the leading zero to the teletext application. A local URL
    // reload leaves stale ARD/VTX JavaScript state behind, so ask the Enigma2
    // backend to stop the current browser process and start a fresh Red Button
    // application instance through the normal eHbbTV activation path.
    emit hbbtvCommand(CommandClient::CommandRestartApplication, QStringLiteral("redbutton"));

    QTimer::singleShot(2500, this, [this]() {
        if (m_teletextReturnInProgress) {
            qDebug() << "[OpenHbbTV] teletext fresh restart still pending" << url().toString();
            m_teletextReturnInProgress = false;
        }
    });
}

bool WebView::isTeletextDigitKey(int keyCode) const
{
    return keyCode >= VirtualKey::VK_0 && keyCode <= VirtualKey::VK_9;
}

QChar WebView::teletextDigitFromKeyCode(int keyCode) const
{
    if (!isTeletextDigitKey(keyCode))
        return QChar();
    return QChar(QLatin1Char('0' + (keyCode - VirtualKey::VK_0)));
}

bool WebView::handleTeletextDigit(int keyCode)
{
    if (!isTeletextUrl() || !isTeletextDigitKey(keyCode))
        return false;

    const QChar digit = teletextDigitFromKeyCode(keyCode);

    // ARD HbbTV teletext pages are addressed from 100 to 899. A leading
    // zero is therefore not a page-number prefix; consume it immediately
    // and reopen the broadcaster start application via Enigma2/eHbbTV.
    // Zeros in the second or third position remain normal page input, e.g.
    // 100, 101, 110.
    if (m_teletextReturnInProgress) {
        qDebug() << "[OpenHbbTV] ignore teletext digit during return" << digit;
        return true;
    }

    if (m_teletextDigitBuffer.isEmpty() && digit == QLatin1Char('0')) {
        qDebug() << "[OpenHbbTV] teletext leading zero detected";
        beginTeletextReturn();
        return true;
    }

    if (m_teletextDigitBuffer.size() >= 3)
        m_teletextDigitBuffer.clear();

    m_teletextDigitBuffer.append(digit);
    qDebug() << "[OpenHbbTV] teletext digit buffer" << m_teletextDigitBuffer;

    if (m_teletextDigitBuffer.size() < 3) {
        m_teletextDigitTimer->start();
        return true;
    }

    m_teletextDigitTimer->stop();
    const QString page = m_teletextDigitBuffer;
    m_teletextDigitBuffer.clear();

    qDebug() << "[OpenHbbTV] teletext page input" << page;
    for (const QChar ch : page)
        injectKeyEvent(VirtualKey::VK_0 + ch.digitValue());
    return true;
}

void WebView::flushTeletextDigitBuffer()
{
    if (m_teletextDigitBuffer.isEmpty())
        return;

    const QString page = m_teletextDigitBuffer;
    m_teletextDigitBuffer.clear();
    qDebug() << "[OpenHbbTV] flush teletext digit buffer" << page;

    for (const QChar ch : page) {
        if (ch.isDigit())
            injectKeyEvent(VirtualKey::VK_0 + ch.digitValue());
    }
}

void WebView::sendKeyEvent(const int &keyCode)
{
    qDebug() << "[OpenHbbTV] sendKeyEvent" << keyCode;

    if (handleTeletextDigit(keyCode))
        return;

    if (!m_teletextDigitBuffer.isEmpty())
        flushTeletextDigitBuffer();

    if (handleStreamKeyFallback(keyCode))
        return;

    if (isStreamActive())
        showApplicationOverlay(QStringLiteral("stream key"));

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

    injectKeyEvent(keyCode);
}

void WebView::injectKeyEvent(int keyCode)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<VirtualKey::VirtualKeyType>();

    QString s = QString::fromLatin1("(function() {"
                                    "  var code = %1;"
                                    "  var vkName = '%2';"
                                    "  var resolved = (typeof window[vkName] !== 'undefined') ? window[vkName] : code;"
                                    "  var target = document.activeElement || document.body || document.documentElement || document;"
                                    "  try { if (document.body && document.body.focus) document.body.focus(); } catch (ignore) {}"
                                    "  function keyName(value) {"
                                    "    if (value >= 48 && value <= 57) return String.fromCharCode(value);"
                                    "    if (value === 13) return 'Enter';"
                                    "    if (value === 37 || value === 403) return 'ArrowLeft';"
                                    "    if (value === 38 || value === 404) return 'ArrowUp';"
                                    "    if (value === 39 || value === 405) return 'ArrowRight';"
                                    "    if (value === 40 || value === 406) return 'ArrowDown';"
                                    "    if (value === 461) return 'Backspace';"
                                    "    if (value === 413) return 'Stop';"
                                    "    if (value === 415) return 'Play';"
                                    "    return vkName || String(value);"
                                    "  }"
                                    "  function makeEvent(type) {"
                                    "    var e = new KeyboardEvent(type, {"
                                    "      bubbles : true,"
                                    "      cancelable : true,"
                                    "      composed : true,"
                                    "      key : keyName(resolved),"
                                    "      code : vkName,"
                                    "      keyCode : resolved,"
                                    "      which : resolved"
                                    "    });"
                                    "    try { Object.defineProperty(e, 'keyCode', { value: resolved }); } catch (ignore) {}"
                                    "    try { Object.defineProperty(e, 'which', { value: resolved }); } catch (ignore) {}"
                                    "    try { Object.defineProperty(e, 'charCode', { value: 0 }); } catch (ignore) {}"
                                    "    return e;"
                                    "  }"
                                    "  try { target.dispatchEvent(makeEvent('keydown')); } catch (e) { console.log('OpenHbbTV keydown failed', e); }"
                                    "  window.setTimeout(function() {"
                                    "    try { target.dispatchEvent(makeEvent('keyup')); } catch (e) { console.log('OpenHbbTV keyup failed', e); }"
                                    "  }, 25);"
                                    "})();").arg(keyCode).arg(metaEnum.valueToKey(keyCode));
    qDebug() << "[OpenHbbTV] inject keydown+keyup" << keyCode;
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
    if (m_teletextReturnInProgress && ok && isInitialUrl(url())) {
        qDebug() << "[OpenHbbTV] teletext leading-zero return completed" << url().toString();
        m_teletextReturnInProgress = false;
        QTimer::singleShot(80, this, &WebView::refreshApplicationAfterTeletextReturn);
        QTimer::singleShot(450, this, &WebView::refreshApplicationAfterTeletextReturn);
    }
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
