#include "webview.h"
#include "browsercontrol.h"
#include "virtualkey.h"
#include "webpage.h"
#include <QApplication>
#include <QByteArray>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QFileInfo>
#include <QTimer>
#include <QWidget>
#include <QDateTime>
#include <QCoreApplication>
#include <QMetaEnum>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPointer>
#include <QSharedPointer>
#include <QStringList>
#include <QtGlobal>
#include <QVariant>

static bool openHbbTVEnvEnabled(const char *name, bool defaultEnabled)
{
    const QString value = QString::fromLocal8Bit(qgetenv(name)).trimmed().toLower();
    if (value.isEmpty())
        return defaultEnabled;
    return value == QStringLiteral("1") ||
           value == QStringLiteral("yes") ||
           value == QStringLiteral("true") ||
           value == QStringLiteral("on") ||
           value == QStringLiteral("enabled");
}

static int openHbbTVEnvInt(const char *name, int defaultValue, int minValue, int maxValue)
{
    bool ok = false;
    const int value = QString::fromLocal8Bit(qgetenv(name)).trimmed().toInt(&ok);
    if (!ok)
        return defaultValue;
    return qBound(minValue, value, maxValue);
}

std::atomic_bool OpenHbbTVRequestInterceptor::s_externalPlaybackActive(false);

void OpenHbbTVRequestInterceptor::setExternalPlaybackActive(bool active)
{
    const bool previous = s_externalPlaybackActive.exchange(active);
    if (previous != active)
        qDebug() << "[OpenHbbTV] external playback request guard" << active;
}

bool OpenHbbTVRequestInterceptor::externalPlaybackActive()
{
    return s_externalPlaybackActive.load();
}

bool OpenHbbTVRequestInterceptor::shouldBlockExternalPlaybackRequest(
    const QUrl &requestUrl,
    QWebEngineUrlRequestInfo::ResourceType type)
{
    Q_UNUSED(type);

    if (!openHbbTVEnvEnabled("OPENHBBTV_STREAM_BLOCK_BACKGROUND", false))
        return false;

    const QString scheme = requestUrl.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        return false;

    const QString host = requestUrl.host().toLower();
    if (host.isEmpty())
        return false;

    QString blockedHosts = QString::fromLocal8Bit(qgetenv("OPENHBBTV_STREAM_BLOCKED_HOSTS")).trimmed().toLower();
    if (blockedHosts.isEmpty())
        blockedHosts = QStringLiteral("nmrodam.com");

    blockedHosts.replace(QLatin1Char(';'), QLatin1Char(','));
    blockedHosts.replace(QLatin1Char(' '), QLatin1Char(','));
    blockedHosts.replace(QLatin1Char('\n'), QLatin1Char(','));
    blockedHosts.replace(QLatin1Char('\t'), QLatin1Char(','));

    const QStringList entries = blockedHosts.split(QLatin1Char(','), QString::SkipEmptyParts);
    for (QString entry : entries) {
        entry = entry.trimmed();
        if (entry.isEmpty())
            continue;
        if (entry.startsWith(QStringLiteral("*.")))
            entry.remove(0, 1);

        if (entry.startsWith(QLatin1Char('.'))) {
            const QString bare = entry.mid(1);
            if (host == bare || host.endsWith(entry))
                return true;
            continue;
        }

        const QString subdomainSuffix = QStringLiteral(".") + entry;
        if (host == entry || host.endsWith(subdomainSuffix))
            return true;
    }

    return false;
}

WebView::WebView(QWidget *parent)
    : QWebEngineView(parent)
    , m_streamState(0)
    , m_streamError(-1)
    , m_streamOverlayVisible(true)
    , m_streamOverlayLowered(false)
    , m_silentPlayingStatePending(false)
    , m_streamRendererFrozen(false)
    , m_streamViewHiddenForPlayback(false)
    , m_streamOverlayHoldUntilMs(0)
    , m_streamOverlayGeometryValid(false)
    , m_jsTimeoutRecoveryPending(false)
    , m_diagnosticSeq(0)
    , m_jsSeq(0)
    , m_currentOnid(-1)
    , m_currentTsid(-1)
    , m_currentSid(-1)
    , m_teletextReturnInProgress(false)
    , m_usingTeletextPage(false)
    , m_teletextDigitTimer(new QTimer(this))
    , m_quitMsg(new QLabel)
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

    OpenHbbTVRequestInterceptor::setExternalPlaybackActive(false);

    setCursor(Qt::BlankCursor);
    setMouseTracking(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet(QStringLiteral("background: transparent;"));

    m_teletextDigitTimer->setSingleShot(true);
    m_teletextDigitTimer->setInterval(1200);
    connect(m_teletextDigitTimer, &QTimer::timeout, this, &WebView::flushTeletextDigitBuffer);

    connect(this, &QWebEngineView::titleChanged, this, &WebView::titleChanged);
    connect(this, &QWebEngineView::loadStarted, this, [this]() {
        const QString currentUrl = url().toString();
        recordDiagnosticEvent(QStringLiteral("loadStarted url=") + diagnosticSnippet(currentUrl));
        qDebug() << "[OpenHbbTV] loadStarted" << currentUrl;
    });
    connect(this, &QWebEngineView::loadProgress, this, [this](int progress) {
        const QString currentUrl = url().toString();
        recordDiagnosticEvent(QStringLiteral("loadProgress %1 url=%2").arg(progress).arg(diagnosticSnippet(currentUrl)));
        qDebug() << "[OpenHbbTV] loadProgress" << progress << currentUrl;
    });
    connect(this, &QWebEngineView::urlChanged, this, [this](const QUrl &u) {
        m_lastLoadUrl = u.toString();
        recordDiagnosticEvent(QStringLiteral("urlChanged ") + diagnosticSnippet(m_lastLoadUrl));
        qDebug() << "[OpenHbbTV] urlChanged" << u.toString();
        if (m_teletextReturnInProgress && isTeletextUrl(u)) {
            qDebug() << "[OpenHbbTV] block teletext reload during leading-zero return" << u.toString();
            stop();
            setUrl(QUrl(QStringLiteral("about:blank")));
            loadInitialUrlAfterTeletextReturn(120);
        }
    });
    connect(this, &QWebEngineView::loadFinished, this, &WebView::loadFinished);
    attachPageDiagnostics();
}

void WebView::attachPageDiagnostics()
{
    QWebEnginePage *currentPage = page();
    if (!currentPage)
        return;

    WebPage *webPage = qobject_cast<WebPage *>(currentPage);
    if (webPage) {
        if (!webPage->property("openhbbtvTeletextSignalConnected").toBool()) {
            connect(webPage, &WebPage::teletextNavigationRequested,
                    this, &WebView::openTeletextPage);
            webPage->setProperty("openhbbtvTeletextSignalConnected", true);
        }

        if (!m_usingTeletextPage && !isTeletextUrl(webPage->url())) {
            m_applicationPage = webPage;
            webPage->setTeletextNavigationInterceptEnabled(true);
        }
    }

    if (currentPage->property("openhbbtvDiagnosticsAttached").toBool()) {
        m_diagnosticsPage = currentPage;
        return;
    }

    m_streamRendererFrozen = false;
    m_diagnosticsPage = currentPage;
    currentPage->setProperty("openhbbtvDiagnosticsAttached", true);
    connect(currentPage, &QWebEnginePage::renderProcessTerminated, this,
            [this, currentPage](QWebEnginePage::RenderProcessTerminationStatus status, int exitCode) {
                recordDiagnosticEvent(QStringLiteral("renderProcessTerminated status=%1 exit=%2").arg(status).arg(exitCode));
                qWarning() << "[OpenHbbTV] renderProcessTerminated" << status << exitCode
                           << "stream" << m_streamState << "page" << currentPage << url().toString();
                dumpRenderCrashDiagnostics(static_cast<int>(status), exitCode);
                requestRestartApplicationOnce(QStringLiteral("render-process-terminated"));
            });
    qDebug() << "[OpenHbbTV] page diagnostics attached" << currentPage;
}

bool WebView::streamRendererFreezeEnabled() const
{
    return openHbbTVEnvEnabled("OPENHBBTV_STREAM_FREEZE_RENDERER", false);
}

bool WebView::streamRendererFreezeViewEnabled() const
{
    return openHbbTVEnvEnabled("OPENHBBTV_STREAM_FREEZE_VIEW", true);
}

bool WebView::streamRendererFreezeLifecycleEnabled() const
{
    return openHbbTVEnvEnabled("OPENHBBTV_STREAM_FREEZE_LIFECYCLE", true);
}

int WebView::streamRendererFreezeDelayMs() const
{
    return openHbbTVEnvInt("OPENHBBTV_STREAM_FREEZE_RENDERER_DELAY_MS", 2200, 0, 10000);
}

void WebView::scheduleStreamRendererFreeze(const QString &reason)
{
    if (!streamRendererFreezeEnabled())
        return;

    const int delayMs = streamRendererFreezeDelayMs();
    qDebug() << "[OpenHbbTV] schedule stream renderer freeze" << delayMs << reason
             << "state" << m_streamState << "overlayVisible" << m_streamOverlayVisible;
    QTimer::singleShot(delayMs, this, [this, reason, delayMs]() {
        if (m_streamState != 1 || m_streamOverlayVisible) {
            qDebug() << "[OpenHbbTV] skip stream renderer freeze" << delayMs << reason
                     << "state" << m_streamState << "overlayVisible" << m_streamOverlayVisible;
            return;
        }
        setStreamRendererActive(false, QStringLiteral("scheduled ") + reason);
    });
}

void WebView::setStreamRendererActive(bool active, const QString &reason)
{
    if (!streamRendererFreezeEnabled())
        return;

    recordDiagnosticEvent(QStringLiteral("setStreamRendererActive active=%1 reason=%2")
        .arg(active)
        .arg(diagnosticSnippet(reason)));

    QWebEnginePage *currentPage = page();
    if (!currentPage)
        return;

    if (!active && streamRendererFreezeViewEnabled() && !m_streamViewHiddenForPlayback) {
        QWebEngineView::hide();
        m_streamViewHiddenForPlayback = true;
        qDebug() << "[OpenHbbTV] hide WebEngine view during external E2 stream" << reason;
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }

    if (streamRendererFreezeLifecycleEnabled()) {
        const QMetaObject *meta = currentPage->metaObject();
        const int propertyIndex = meta ? meta->indexOfProperty("lifecycleState") : -1;
        const int enumIndex = meta ? meta->indexOfEnumerator("LifecycleState") : -1;
        if (propertyIndex >= 0 && enumIndex >= 0) {
            const QMetaProperty property = meta->property(propertyIndex);
            const QMetaEnum states = meta->enumerator(enumIndex);
            const int stateValue = states.keyToValue(active ? "Active" : "Frozen");
            if (property.isWritable() && stateValue >= 0) {
                const bool ok = property.write(currentPage, QVariant(stateValue));
                const QVariant actual = property.read(currentPage);
                bool actualOk = false;
                const int actualValue = actual.toInt(&actualOk);
                qDebug() << "[OpenHbbTV] stream renderer lifecycle"
                         << (active ? "active" : "frozen") << "ok" << ok
                         << "requested" << stateValue << "actual" << actual
                         << reason;
                if (ok && actualOk && actualValue == stateValue) {
                    m_streamRendererFrozen = !active;
                } else {
                    qWarning() << "[OpenHbbTV] stream renderer lifecycle transition did not stick"
                               << "active" << active << "ok" << ok
                               << "requested" << stateValue << "actual" << actual
                               << reason;
                    if (active)
                        m_streamRendererFrozen = false;
                }
            } else {
                qDebug() << "[OpenHbbTV] stream renderer lifecycle unavailable"
                         << "writable" << property.isWritable()
                         << "stateValue" << stateValue << reason;
            }
        } else {
            qDebug() << "[OpenHbbTV] stream renderer lifecycle property missing"
                     << "property" << propertyIndex << "enum" << enumIndex << reason;
        }
    }

    if (active && m_streamViewHiddenForPlayback) {
        QWebEngineView::show();
        m_streamViewHiddenForPlayback = false;
        qDebug() << "[OpenHbbTV] show WebEngine view after stream freeze" << reason;
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
}

void WebView::requestRestartApplicationOnce(const QString &reason)
{
    recordDiagnosticEvent(QStringLiteral("restart requested ") + diagnosticSnippet(reason));
    if (m_jsTimeoutRecoveryPending) {
        qWarning() << "[OpenHbbTV] restart application already pending" << reason
                   << "stream" << m_streamState << "url" << url().toString();
        return;
    }

    m_jsTimeoutRecoveryPending = true;
    qWarning() << "[OpenHbbTV] restart application requested" << reason
               << "stream" << m_streamState << "url" << url().toString();
    emit hbbtvCommand(CommandClient::CommandRestartApplication, reason);
}

QString WebView::diagnosticSnippet(const QString &text, int maxLength) const
{
    QString value = text;
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('\t'), QLatin1Char(' '));
    while (value.contains(QStringLiteral("  ")))
        value.replace(QStringLiteral("  "), QStringLiteral(" "));
    if (value.length() > maxLength)
        value = value.left(maxLength) + QStringLiteral("...");
    return value;
}

void WebView::recordDiagnosticEvent(const QString &event)
{
    const QString entry = QStringLiteral("%1 #%2 %3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(++m_diagnosticSeq)
        .arg(event);
    m_recentDiagnosticEvents.append(entry);
    while (m_recentDiagnosticEvents.size() > 30)
        m_recentDiagnosticEvents.removeFirst();
}

void WebView::recordBackendCommand(int command, const QString &data)
{
    m_lastBackendCommand = QStringLiteral("%1 %2")
        .arg(command)
        .arg(diagnosticSnippet(data));
    recordDiagnosticEvent(QStringLiteral("backend-command ") + m_lastBackendCommand);
}

void WebView::recordBrowserCommand(int command, const QString &data)
{
    m_lastBrowserCommand = QStringLiteral("%1 %2")
        .arg(command)
        .arg(diagnosticSnippet(data));
    recordDiagnosticEvent(QStringLiteral("browser-command ") + m_lastBrowserCommand);
}

void WebView::dumpRenderCrashDiagnostics(int status, int exitCode)
{
    QWidget *top = window();
    qWarning() << "[OpenHbbTV] render crash diagnostics begin"
               << "status" << status
               << "exit" << exitCode
               << "url" << url().toString()
               << "lastLoadUrl" << m_lastLoadUrl
               << "title" << m_lastTitle
               << "streamState" << m_streamState
               << "streamError" << m_streamError
               << "overlayVisible" << m_streamOverlayVisible
               << "overlayLowered" << m_streamOverlayLowered
               << "rendererFrozen" << m_streamRendererFrozen
               << "viewHiddenForPlayback" << m_streamViewHiddenForPlayback
               << "viewVisible" << isVisible()
               << "windowVisible" << (top ? top->isVisible() : false)
               << "viewGeometry" << geometry()
               << "windowGeometry" << (top ? top->geometry() : QRect())
               << "externalPlaybackGuard" << OpenHbbTVRequestInterceptor::externalPlaybackActive();
    qWarning() << "[OpenHbbTV] render crash last activity"
               << "jsStarted" << m_lastJsStarted
               << "jsCompleted" << m_lastJsCompleted
               << "bridge" << m_lastBridgeCommand
               << "backend" << m_lastBackendCommand
               << "browser" << m_lastBrowserCommand
               << "teletext" << isTeletextUrl();
    for (const QString &entry : m_recentDiagnosticEvents)
        qWarning() << "[OpenHbbTV] render crash recent" << entry;
    qWarning() << "[OpenHbbTV] render crash diagnostics end";
}

void WebView::runJavaScriptWithWatchdog(const QString &label, const QString &script, int timeoutMs, bool recoverOnTimeout)
{
    QSharedPointer<bool> completed(new bool(false));
    const QString diagnosticLabel = QStringLiteral("#%1 %2").arg(++m_jsSeq).arg(label);
    m_lastJsStarted = diagnosticLabel;
    recordDiagnosticEvent(QStringLiteral("js-start ") + diagnosticSnippet(diagnosticLabel));
    QPointer<WebView> self(this);
    page()->runJavaScript(script, [completed, label, diagnosticLabel, self](const QVariant &result) {
        *completed = true;
        if (self) {
            self->m_lastJsCompleted = diagnosticLabel;
            self->recordDiagnosticEvent(QStringLiteral("js-result ") + self->diagnosticSnippet(diagnosticLabel)
                + QStringLiteral(" result=") + self->diagnosticSnippet(result.toString()));
        }
        qDebug() << "[OpenHbbTV] JS result" << label << result;
    });

    QTimer::singleShot(timeoutMs, this, [this, completed, label, recoverOnTimeout]() {
        if (*completed)
            return;
        recordDiagnosticEvent(QStringLiteral("js-timeout ") + diagnosticSnippet(label));
        qWarning() << "[OpenHbbTV] JS timeout" << label << "stream" << m_streamState << "url" << url().toString();
        if (recoverOnTimeout)
            requestRestartApplicationOnce(QStringLiteral("js-timeout ") + label);
    });
}

void WebView::injectHbbTVScripts(const QString &src)
{
    m_hbbtvScriptSrc = src;
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

        QWebEngineScript cursorScript;
        cursorScript.setName("openhbbtv_hide_cursor");
        cursorScript.setSourceCode(QString::fromLatin1(
            "(function() {"
            "  try {"
            "    var style = document.createElement('style');"
            "    style.id = 'openhbbtv-hide-cursor';"
            "    style.textContent = '* { cursor: none !important; } html, body { cursor: none !important; }';"
            "    (document.head || document.documentElement).appendChild(style);"
            "  } catch (e) {}"
            "})();"));
        cursorScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
        cursorScript.setRunsOnSubFrames(true);
        cursorScript.setWorldId(QWebEngineScript::MainWorld);
        page()->scripts().insert(cursorScript);

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
        const bool authHttpDebug = openHbbTVEnvEnabled("OPENHBBTV_AUTH_HTTP_DEBUG", false);
        const bool hbbtvHttpDebug = openHbbTVEnvEnabled("OPENHBBTV_HBBTV_HTTP_DEBUG", false);
        const bool hbbtvHttpBodyDebug = openHbbTVEnvEnabled("OPENHBBTV_HBBTV_HTTP_BODY_DEBUG", false);
        const bool zdfConsoleDebug = openHbbTVEnvEnabled("OPENHBBTV_ZDF_CONSOLE_DEBUG", false);
        const bool zdfBootDebug = openHbbTVEnvEnabled("OPENHBBTV_ZDF_BOOT_DEBUG", false);
        const bool apiAuditDebug = openHbbTVEnvEnabled("OPENHBBTV_API_AUDIT_DEBUG", false);
        source.prepend(QStringLiteral("window.OPENHBBTV_AUTH_HTTP_DEBUG=%1;\n"
                                      "window.OPENHBBTV_HBBTV_HTTP_DEBUG=%2;\n"
                                      "window.OPENHBBTV_HBBTV_HTTP_BODY_DEBUG=%3;\n"
                                      "window.OPENHBBTV_ZDF_CONSOLE_DEBUG=%4;\n"
                                      "window.OPENHBBTV_ZDF_BOOT_DEBUG=%5;\n"
                                      "window.OPENHBBTV_API_AUDIT_DEBUG=%6;\n")
                           .arg(authHttpDebug ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(hbbtvHttpDebug ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(hbbtvHttpBodyDebug ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(zdfConsoleDebug ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(zdfBootDebug ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(apiAuditDebug ? QStringLiteral("true") : QStringLiteral("false")));

        QWebEngineScript script;
        script.setName("xmlhttprequest_quirks");
        script.setSourceCode(source);
        script.setInjectionPoint(QWebEngineScript::DocumentCreation);
        script.setRunsOnSubFrames(true);
        script.setWorldId(QWebEngineScript::MainWorld);
        page()->scripts().insert(script);

        qDebug() << "[HbbTV] xmlhttprequest_quirks injected via QWebEngineScript";
        qDebug() << "[OpenHbbTV] auth HTTP debug" << authHttpDebug << "hbbtv HTTP debug" << hbbtvHttpDebug
                 << "hbbtv HTTP body debug" << hbbtvHttpBodyDebug
                 << "zdf console debug" << zdfConsoleDebug
                 << "zdf boot debug" << zdfBootDebug;
    } else {
        qWarning() << "[HbbTV] xmlhttprequest_quirks.js not found in qrc";
    }
}

void WebView::setCurrentChannel(const int &onid, const int &tsid, const int &sid)
{
    m_currentOnid = onid;
    m_currentTsid = tsid;
    m_currentSid = sid;
    QWebEngineScript script;
    recordDiagnosticEvent(QStringLiteral("setCurrentChannel %1,%2,%3").arg(onid).arg(tsid).arg(sid));

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
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setRunsOnSubFrames(true);
    script.setWorldId(QWebEngineScript::MainWorld);
    page()->scripts().insert(script);
    qDebug() << "[OpenHbbTV] setCurrentChannel" << onid << tsid << sid;
    runJavaScriptWithWatchdog(QStringLiteral("setCurrentChannel"), s);
}

void WebView::setBroadcastInfo(const QString &json)
{
    qDebug() << "[OpenHbbTV] setBroadcastInfo" << json.left(240);
    recordDiagnosticEvent(QStringLiteral("setBroadcastInfo len=%1 teletext=%2")
        .arg(json.length())
        .arg(isTeletextUrl()));
    m_lastBroadcastInfo = json;
    if (isTeletextUrl() && !openHbbTVEnvEnabled("OPENHBBTV_TELETEXT_BROADCAST_INFO", true)) {
        recordDiagnosticEvent(QStringLiteral("skip setBroadcastInfo JS on teletext page"));
        qDebug() << "[OpenHbbTV] skip setBroadcastInfo JS on teletext page" << url().toString();
        return;
    }
    const QByteArray encoded = json.toUtf8().toBase64();
    QString s = QString::fromLatin1(
        "(function() {"
        "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
        "  var raw = '';"
        "  try { raw = decodeURIComponent(escape(atob('%1'))); } catch (e) { raw = '{}'; }"
        "  try {"
        "    var info = JSON.parse(raw);"
        "    window.HBBTV_POLYFILL_NS.broadcastInfo = info;"
        "    window.HBBTV_POLYFILL_NS.broadcastInfoReceivedAt = Date.now ? Date.now() : (new Date()).getTime();"
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
    runJavaScriptWithWatchdog(QStringLiteral("setBroadcastInfo"), s);
}



bool WebView::shouldForceNativeVisibleRefresh(const QString &reason) const
{
    const QString force = QString::fromLocal8Bit(qgetenv("OPENHBBTV_FORCE_VISIBLE_REFRESH")).trimmed().toLower();
    if (force == QStringLiteral("1") || force == QStringLiteral("true") || force == QStringLiteral("yes"))
        return true;

    // Default: never use QWidget::hide()/showFullScreen() as a visibility refresh.
    // On Vu+/eglfs_libvupl this can destroy or detach the native EGL surface: the
    // user then sees the browser only for one frame or not at all, while Qt still
    // reports visible=true. This is not limited to external DASH playback; ARD also
    // emits BROADCAST_PLAY/BROADCAST_HIDDEN during normal Mediathek navigation.
    // The safe path is to keep the surface alive and only raise/focus/repaint it.
    Q_UNUSED(reason);
    return false;
}

void WebView::forceNativeVisibleRefresh(QWidget *top, const QString &reason)
{
    if (!top)
        return;

    qDebug() << "[OpenHbbTV] safe visible refresh for overlay" << reason << top->geometry() << "visible" << top->isVisible();

    const QRect geometry = m_streamOverlayGeometryValid ? m_streamOverlaySavedGeometry : top->geometry();
    if (geometry.isValid())
        top->setGeometry(geometry);

    if (!top->isVisible())
        top->showFullScreen();
    else
        top->showFullScreen();

    top->raise();
    top->activateWindow();
    show();
    setFocus(Qt::OtherFocusReason);
    repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    qDebug() << "[OpenHbbTV] safe visible refresh done" << reason << top->geometry() << "visible" << top->isVisible();
}

void WebView::repaintOverlaySurface(const QString &reason)
{
    QWidget *top = window();
    if (!top)
        return;
    qDebug() << "[OpenHbbTV] repaint overlay surface" << reason
             << top->geometry() << "visible" << top->isVisible()
             << "stream" << m_streamState;
    top->raise();
    show();
    setFocus(Qt::OtherFocusReason);
    update();
    repaint();
    top->update();
    top->repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
}

void WebView::retryOverlayRepaint(const QString &reason, int delayMs)
{
    QPointer<QWidget> top = window();
    QTimer::singleShot(delayMs, this, [this, top, reason, delayMs]() {
        if (!top || !top->isVisible()) {
            qDebug() << "[OpenHbbTV] skip overlay repaint retry" << delayMs << reason
                     << "top" << static_cast<bool>(top)
                     << "visible" << (top ? top->isVisible() : false);
            return;
        }
        qDebug() << "[OpenHbbTV] overlay repaint retry" << delayMs << reason;
        repaintOverlaySurface(reason + QStringLiteral(" retry %1").arg(delayMs));
    });
}

void WebView::retryStreamOverlayVisible(const QString &reason, int delayMs)
{
    if (!isStreamActive())
        return;

    QPointer<QWidget> top = window();
    QTimer::singleShot(delayMs, this, [this, top, reason, delayMs]() {
        if (!top || !isStreamActive() || !m_streamOverlayVisible) {
            qDebug() << "[OpenHbbTV] skip stream overlay native show retry" << delayMs << reason
                     << "top" << static_cast<bool>(top) << "stream" << isStreamActive()
                     << "overlay" << m_streamOverlayVisible;
            return;
        }

        if (m_streamOverlayGeometryValid)
            top->setGeometry(m_streamOverlaySavedGeometry);

        qDebug() << "[OpenHbbTV] stream overlay native show retry" << delayMs << reason
                 << top->geometry() << "visible" << top->isVisible();
        top->showFullScreen();
        top->raise();
        top->activateWindow();
        show();
        setFocus(Qt::OtherFocusReason);
        repaint();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        qDebug() << "[OpenHbbTV] stream overlay native show retry done" << delayMs << reason
                 << top->geometry() << "visible" << top->isVisible();
    });
}

void WebView::showApplicationOverlay(const QString &reason)
{
    recordDiagnosticEvent(QStringLiteral("showApplicationOverlay ") + diagnosticSnippet(reason));
    setStreamRendererActive(true, QStringLiteral("show overlay ") + reason);

    const QString reasonLower = reason.toLower();
    const bool streamReason = isStreamActive() && reasonLower.contains(QStringLiteral("stream"));
    const bool postStreamReturn = reasonLower.contains(QStringLiteral("stream state stopped"))
        || reasonLower.contains(QStringLiteral("live stream stopped"))
        || reasonLower == QStringLiteral("stream stopped");
    const bool pageLoadRefresh = reasonLower.contains(QStringLiteral("page load finished"));
    const bool wasOverlayVisible = m_streamOverlayVisible;
    const bool wasOverlayLowered = m_streamOverlayLowered;
    m_streamOverlayVisible = true;
    if (isStreamActive())
        m_streamOverlayHoldUntilMs = QDateTime::currentMSecsSinceEpoch() + 2000;

    qDebug() << "[OpenHbbTV] show application overlay" << reason
             << "streamReason" << streamReason << "wasVisible" << wasOverlayVisible
             << "wasLowered" << wasOverlayLowered;

    QWidget *top = window();
    const bool duplicateVisibleOverlay = (streamReason || postStreamReturn || pageLoadRefresh)
        && wasOverlayVisible && !wasOverlayLowered && top && top->isVisible();
    if (duplicateVisibleOverlay) {
        // Preserve the proven key/RCU path.  Only suppress repeated native
        // EGL/libvupl refresh work when the overlay is already visible.
        // Repeating showFullScreen/raise/activate/repaint on every key or
        // post-stream page load can leave shifted fragments or crash the
        // WebEngine renderer on libvupl.
        qDebug() << "[OpenHbbTV] skip duplicate visible overlay native refresh" << reason
                 << top->geometry() << "visible" << top->isVisible();
    } else if (top) {
        if (wasOverlayLowered) {
            qDebug() << "[OpenHbbTV] restore parked browser native surface for overlay" << reason
                     << "current" << top->geometry()
                     << "restore" << m_streamOverlaySavedGeometry
                     << "visible" << top->isVisible();
        }
        if (m_streamOverlayGeometryValid) {
            top->setGeometry(m_streamOverlaySavedGeometry);
            qDebug() << "[OpenHbbTV] restore browser window geometry" << m_streamOverlaySavedGeometry << reason;
        }
        if (shouldForceNativeVisibleRefresh(reason)) {
            forceNativeVisibleRefresh(top, reason);
        } else if (!top->isVisible()) {
            top->showFullScreen();
            top->raise();
            top->activateWindow();
            show();
            setFocus(Qt::OtherFocusReason);
            qDebug() << "[OpenHbbTV] show browser window for overlay" << reason << top->geometry() << "visible" << top->isVisible();
        } else {
            top->showFullScreen();
            top->raise();
            top->activateWindow();
            show();
            setFocus(Qt::OtherFocusReason);
            qDebug() << "[OpenHbbTV] refresh visible browser window for overlay" << reason << top->geometry() << "visible" << top->isVisible();
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        m_streamOverlayLowered = false;
    }
    if (streamReason && !duplicateVisibleOverlay) {
        retryStreamOverlayVisible(reason, 120);
        retryStreamOverlayVisible(reason, 450);
        retryStreamOverlayVisible(reason, 900);
    }
    if ((streamReason && !duplicateVisibleOverlay) || (postStreamReturn && !duplicateVisibleOverlay)) {
        repaintOverlaySurface(reason);
        retryOverlayRepaint(reason, 120);
        retryOverlayRepaint(reason, 450);
    }
    const bool skipDuplicateOverlayJs = duplicateVisibleOverlay &&
        openHbbTVEnvEnabled("OPENHBBTV_STREAM_SKIP_DUPLICATE_OVERLAY_JS", true);
    if (skipDuplicateOverlayJs) {
        qDebug() << "[OpenHbbTV] skip duplicate visible overlay JS refresh" << reason;
    } else {
        runJavaScriptWithWatchdog(QStringLiteral("showApplicationOverlay ") + reason, QString::fromLatin1(
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
    if (m_silentPlayingStatePending && m_streamState == 1) {
        QTimer::singleShot(180, this, [this, reason]() {
            syncDeferredStreamStateToApplication(reason);
        });
    }
}

void WebView::syncDeferredStreamStateToApplication(const QString &reason)
{
    if (!m_silentPlayingStatePending || m_streamState != 1 || !m_streamOverlayVisible)
        return;

    const QString syncValue = QString::fromLocal8Bit(qgetenv("OPENHBBTV_STREAM_SYNC_PLAYING_ON_OVERLAY")).trimmed().toLower();
    const bool syncEnabled = syncValue.isEmpty()
        || syncValue == QStringLiteral("1")
        || syncValue == QStringLiteral("yes")
        || syncValue == QStringLiteral("true")
        || syncValue == QStringLiteral("on")
        || syncValue == QStringLiteral("enabled");
    if (!syncEnabled) {
        qDebug() << "[OpenHbbTV] deferred stream state visible sync disabled" << reason;
        return;
    }

    m_silentPlayingStatePending = false;
    qDebug() << "[OpenHbbTV] sync deferred stream playing state to visible overlay" << reason
             << "error" << m_streamError;
    QString script = QString::fromLatin1("(function() {"
                                         "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
                                         "  if (typeof window.HBBTV_POLYFILL_NS.setStreamState === 'function') {"
                                         "    window.HBBTV_POLYFILL_NS.setStreamState(1, %1, { reason: 'overlay-visible-sync' });"
                                         "  } else {"
                                         "    window.HBBTV_POLYFILL_NS.pendingStreamState = [1, %1, { reason: 'overlay-visible-sync' }];"
                                         "  }"
                                         "})();").arg(m_streamError);
    runJavaScriptWithWatchdog(QStringLiteral("setStreamState visible sync 1,%1").arg(m_streamError), script);
}

void WebView::hideApplicationOverlay(const QString &reason)
{
    recordDiagnosticEvent(QStringLiteral("hideApplicationOverlay ") + diagnosticSnippet(reason));
    const QString reasonLower = reason.toLower();
    const bool preStreamHide = reasonLower.contains(QStringLiteral("playstreamrequest"))
        || reasonLower.contains(QStringLiteral("hard restart stream"))
        || reasonLower.contains(QStringLiteral("live-dash"))
        || reasonLower.contains(QStringLiteral("live stream"))
        || reasonLower.contains(QStringLiteral("vod-native-hide"))
        || reasonLower.contains(QStringLiteral("native-hide"));
    if (!isStreamActive() && !preStreamHide)
        return;
    if (reasonLower.contains(QStringLiteral("auto hide")) && m_streamOverlayVisible && QDateTime::currentMSecsSinceEpoch() < m_streamOverlayHoldUntilMs) {
        qDebug() << "[OpenHbbTV] skip auto hide while stream overlay is explicitly visible" << reason;
        return;
    }
    if (reasonLower.contains(QStringLiteral("auto hide")) && !m_streamOverlayVisible && m_streamOverlayLowered) {
        qDebug() << "[OpenHbbTV] skip duplicate auto hide after browser already hidden/parked" << reason;
        return;
    }
    m_streamOverlayVisible = false;
    if (!reasonLower.contains(QStringLiteral("auto hide")))
        m_streamOverlayHoldUntilMs = 0;
    qDebug() << "[OpenHbbTV] hide application overlay" << reason;
    QWidget *top = window();
    if (top) {
        if (!m_streamOverlayGeometryValid && top->geometry().isValid()) {
            m_streamOverlaySavedGeometry = top->geometry();
            m_streamOverlayGeometryValid = true;
            qDebug() << "[OpenHbbTV] save browser window geometry" << m_streamOverlaySavedGeometry;
        }

        const QString hideMode = QString::fromLocal8Bit(qgetenv("OPENHBBTV_STREAM_BROWSER_HIDE_MODE")).trimmed().toLower();
        const bool reasonNativeHide = reasonLower.contains(QStringLiteral("vod-native-hide"))
            || reasonLower.contains(QStringLiteral("native-hide"));
        const bool nativeHide = reasonNativeHide
            || hideMode == QStringLiteral("hide")
            || hideMode == QStringLiteral("native")
            || hideMode == QStringLiteral("hidden")
            || hideMode == QStringLiteral("1")
            || hideMode == QStringLiteral("true");
        const bool keepMode = hideMode == QStringLiteral("keep")
            || hideMode == QStringLiteral("none")
            || hideMode == QStringLiteral("alive")
            || hideMode == QStringLiteral("visible");
        const bool parkMode = hideMode == QStringLiteral("park")
            || hideMode == QStringLiteral("tiny")
            || hideMode == QStringLiteral("move")
            || hideMode == QStringLiteral("safe");

        if (nativeHide) {
            // Diagnostic opt-in. On Vu+/eglfs_libvupl with alpha enabled,
            // top-level hide maps to VUGLES_SetVisible(false) and can crash the
            // WebEngine renderer. The launcher defaults to lower/transparent.
            top->lower();
            if (top->isVisible()) {
                top->hide();
                qDebug() << "[OpenHbbTV] native-hide browser window for stream" << reason;
            } else {
                qDebug() << "[OpenHbbTV] browser window already native-hidden for stream" << reason;
            }
        } else if (keepMode) {
            qDebug() << "[OpenHbbTV] keep browser window alive and visible for stream" << reason
                     << top->geometry() << "visible" << top->isVisible();
        } else if (parkMode) {
            // Diagnostic opt-in only. The default path below intentionally
            // mirrors the stable VOD handling and avoids tiny native geometry.
            if (!top->isVisible()) {
                top->showFullScreen();
                qDebug() << "[OpenHbbTV] recreate visible browser surface before park" << reason << top->geometry();
            }
            const QScreen *screen = QGuiApplication::primaryScreen();
            const QRect screenGeometry = screen ? screen->geometry() : top->geometry();
            const int x = screenGeometry.x() + (screenGeometry.width() > 0 ? screenGeometry.width() - 1 : 0);
            const int y = screenGeometry.y() + (screenGeometry.height() > 0 ? screenGeometry.height() - 1 : 0);
            const QRect parkGeometry(x, y, 1, 1);
            top->setGeometry(parkGeometry);
            qDebug() << "[OpenHbbTV] park browser window for stream without lower/hide" << reason
                     << parkGeometry << "visible" << top->isVisible()
                     << "restore" << m_streamOverlaySavedGeometry;
        } else {
            // Default for all external E2 playback. Keep the browser's native
            // surface full-size and alive, but lower the Qt window so the E2
            // video plane is visible. This mirrors the stable VOD path and
            // avoids the 1x1 native park geometry that crashed live DASH.
            if (m_streamOverlayGeometryValid)
                top->setGeometry(m_streamOverlaySavedGeometry);
            if (!top->isVisible()) {
                top->showFullScreen();
                qDebug() << "[OpenHbbTV] recreate visible browser surface before lower" << reason << top->geometry();
            }
            top->lower();
            qDebug() << "[OpenHbbTV] lower browser window for stream without native hide (vod-style)" << reason
                     << top->geometry() << "visible" << top->isVisible();
        }
        m_streamOverlayLowered = true;
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
}

void WebView::refreshApplicationAfterTeletextReturn()
{
    qDebug() << "[OpenHbbTV] refresh application after teletext return";
    runJavaScriptWithWatchdog(QStringLiteral("refreshApplicationAfterTeletextReturn"), QString::fromLatin1(
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
    recordDiagnosticEvent(QStringLiteral("setStreamState %1,%2 overlay=%3")
        .arg(state)
        .arg(error)
        .arg(m_streamOverlayVisible));
    const bool wasOverlayVisible = m_streamOverlayVisible;
    m_streamState = state;
    m_streamError = error;
    OpenHbbTVRequestInterceptor::setExternalPlaybackActive(state == 1 || state == 2);
    if (state != 1)
        setStreamRendererActive(true, QStringLiteral("stream state not playing"));
    const QString silentPlayingValue = QString::fromLocal8Bit(qgetenv("OPENHBBTV_STREAM_SILENT_PLAYING_STATE")).trimmed().toLower();
    const bool silentPlayingState = state == 1 && !m_streamOverlayVisible &&
        (silentPlayingValue == QStringLiteral("1") ||
         silentPlayingValue == QStringLiteral("yes") ||
         silentPlayingValue == QStringLiteral("true") ||
         silentPlayingValue == QStringLiteral("on") ||
         silentPlayingValue == QStringLiteral("enabled"));
    const QString streamStateOptions = silentPlayingState
        ? QStringLiteral(", { silentEvent: true, reason: 'hidden-e2-playback' }")
        : QString();
    if (silentPlayingState)
        m_silentPlayingStatePending = true;
    else
        m_silentPlayingStatePending = false;
    const bool skipVisibleStopStateJs = state == 0 && wasOverlayVisible &&
        openHbbTVEnvEnabled("OPENHBBTV_STREAM_SKIP_VISIBLE_STOP_STATE_JS", true);
    qDebug() << "[OpenHbbTV] setStreamState" << state << error
             << "overlayVisible" << m_streamOverlayVisible
             << "silentPlayingEvent" << silentPlayingState
             << "skipVisibleStopStateJs" << skipVisibleStopStateJs;
    QString s = QString::fromLatin1("(function() {"
                                    "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
                                    "  if (typeof window.HBBTV_POLYFILL_NS.setStreamState === 'function') {"
                                    "    window.HBBTV_POLYFILL_NS.setStreamState(%1, %2%3);"
                                    "  } else {"
                                    "    window.HBBTV_POLYFILL_NS.pendingStreamState = [%1, %2%3];"
                                    "  }"
                                    "})();").arg(state).arg(error).arg(streamStateOptions);
    if (skipVisibleStopStateJs) {
        qDebug() << "[OpenHbbTV] skip visible stream stop state JS; overlay already visible";
    } else {
        runJavaScriptWithWatchdog(QStringLiteral("setStreamState %1,%2").arg(state).arg(error), s);
    }
    if (state == 1) {
        // When external E2 playback starts the browser must get out of the
        // video plane immediately. Especially on Vu+/libvupl the full-screen
        // EGL surface can otherwise cover the GStreamer video: audio is heard
        // but the HbbTV page remains visible and the user can accidentally
        // start a second stream. Keep several retries because some HbbTV apps
        // call Application.show around PLAY_STREAM.
        QTimer::singleShot(80, this, [this]() { hideApplicationOverlay(QStringLiteral("stream state playing auto hide 1")); });
        QTimer::singleShot(250, this, [this]() { hideApplicationOverlay(QStringLiteral("stream state playing auto hide 2")); });
        QTimer::singleShot(700, this, [this]() { hideApplicationOverlay(QStringLiteral("stream state playing auto hide 3")); });
        scheduleStreamRendererFreeze(QStringLiteral("stream state playing"));
    } else if (state == 2) {
        showApplicationOverlay(QStringLiteral("stream state paused"));
    } else if (state == 0) {
        if (wasOverlayVisible && openHbbTVEnvEnabled("OPENHBBTV_STREAM_SKIP_VISIBLE_STOP_OVERLAY_JS", true)) {
            qDebug() << "[OpenHbbTV] skip visible stream stopped overlay JS; overlay already visible";
        } else {
            showApplicationOverlay(QStringLiteral("stream state stopped"));
            QTimer::singleShot(150, this, [this]() { showApplicationOverlay(QStringLiteral("stream state stopped retry 1")); });
            QTimer::singleShot(650, this, [this]() { showApplicationOverlay(QStringLiteral("stream state stopped retry 2")); });
        }
    }
}

void WebView::setStreamPosition(qint64 positionMs, qint64 durationMs)
{
    qDebug() << "[OpenHbbTV] setStreamPosition" << positionMs << durationMs
             << "streamState" << m_streamState
             << "overlayVisible" << m_streamOverlayVisible;
    recordDiagnosticEvent(QStringLiteral("setStreamPosition %1,%2").arg(positionMs).arg(durationMs));
    QString s = QString::fromLatin1("(function() {"
                                    "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
                                    "  if (typeof window.HBBTV_POLYFILL_NS.setStreamPosition === 'function') {"
                                    "    window.HBBTV_POLYFILL_NS.setStreamPosition(%1, %2, { reason: 'e2-position-sync' });"
                                    "  } else {"
                                    "    window.HBBTV_POLYFILL_NS.pendingStreamPosition = [%1, %2, { reason: 'e2-position-sync' }];"
                                    "  }"
                                    "})();").arg(positionMs).arg(durationMs);
    runJavaScriptWithWatchdog(QStringLiteral("setStreamPosition %1,%2").arg(positionMs).arg(durationMs), s, 800);
}

bool WebView::isStreamActive() const
{
    return m_streamState == 1 || m_streamState == 2;
}

bool WebView::nativeNavigationKeysEnabled() const
{
    const QString value = QString::fromLocal8Bit(qgetenv("OPENHBBTV_NATIVE_NAVIGATION_KEYS")).trimmed().toLower();
    return value == QStringLiteral("1") || value == QStringLiteral("yes") ||
           value == QStringLiteral("true") || value == QStringLiteral("on") ||
           value == QStringLiteral("enabled");
}

bool WebView::isNavigationOrEnterKey(int keyCode) const
{
    switch (keyCode) {
    case VirtualKey::VK_LEFT:
    case VirtualKey::VK_UP:
    case VirtualKey::VK_RIGHT:
    case VirtualKey::VK_DOWN:
    case VirtualKey::VK_ENTER:
        return true;
    default:
        return false;
    }
}

bool WebView::handleStreamKeyFallback(int keyCode)
{
    if (!isStreamActive())
        return false;

    switch (keyCode) {
    case VirtualKey::VK_ENTER: {
        const bool wasVisible = m_streamOverlayVisible;
        qDebug() << "[OpenHbbTV] stream enter key: show application overlay";
        showApplicationOverlay(QStringLiteral("stream enter"));
        if (!wasVisible)
            return true;
        return false;
    }
    case VirtualKey::VK_STOP:
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
    m_language = language;
    QWebEngineScript script;

    QString s = QString::fromLatin1("(function() {"
                                    "  window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};"
                                    "  window.HBBTV_POLYFILL_NS.preferredLanguage = '%1';"
                                    "  window.HBBTV_POLYFILL_NS.preferredCountry = 'DEU';"
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
    m_scriptDebugging = scriptDebugging;
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
    recordDiagnosticEvent(QStringLiteral("setInitialUrl ") + diagnosticSnippet(m_initialUrl.toString()));
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

void WebView::openTeletextPage(const QUrl &teletextUrl)
{
    if (!teletextUrl.isValid() || teletextUrl.isEmpty())
        return;

    QWebEnginePage *currentPage = page();
    QWebEngineProfile *profile = currentPage ? currentPage->profile() : Q_NULLPTR;
    if (!profile) {
        qWarning() << "[OpenHbbTV] teletext page switch skipped: no profile" << teletextUrl.toString();
        return;
    }

    if (m_usingTeletextPage) {
        qDebug() << "[OpenHbbTV] teletext page already active; load" << teletextUrl.toString();
        setUrl(teletextUrl);
        return;
    }

    WebPage *applicationPage = qobject_cast<WebPage *>(currentPage);
    if (applicationPage)
        m_applicationPage = applicationPage;

    if (!m_applicationPage) {
        qWarning() << "[OpenHbbTV] teletext page switch has no application page; load inline"
                   << teletextUrl.toString();
        setUrl(teletextUrl);
        return;
    }

    qDebug() << "[OpenHbbTV] open teletext isolated page"
             << "fromPage" << m_applicationPage.data()
             << "fromUrl" << m_applicationPage->url().toString()
             << "teletext" << teletextUrl.toString();
    recordDiagnosticEvent(QStringLiteral("teletext isolated page open ") + diagnosticSnippet(teletextUrl.toString()));

    WebPage *teletextPage = new WebPage(profile, this);
    teletextPage->setTeletextNavigationInterceptEnabled(false);
    m_teletextPage = teletextPage;
    m_usingTeletextPage = true;

    setPage(teletextPage);
    attachPageDiagnostics();
    setCursor(Qt::BlankCursor);
    show();

    injectHbbTVScripts(m_hbbtvScriptSrc.isEmpty() ? QStringLiteral("qrc:/hbbtv_polyfill.js") : m_hbbtvScriptSrc);
    injectXmlHttpRequestScripts();
    if (m_currentOnid != -1 && m_currentTsid != -1 && m_currentSid != -1)
        setCurrentChannel(m_currentOnid, m_currentTsid, m_currentSid);
    if (!m_language.isEmpty())
        setLanguage(m_language);
    if (!m_scriptDebugging.isEmpty())
        setScriptDebugging(m_scriptDebugging);

    setUrl(teletextUrl);
}

bool WebView::switchBackFromTeletextPage()
{
    if (!m_usingTeletextPage || !m_applicationPage)
        return false;

    WebPage *teletextPage = qobject_cast<WebPage *>(page());
    qDebug() << "[OpenHbbTV] switch back from teletext isolated page"
             << "teletextPage" << teletextPage
             << "applicationPage" << m_applicationPage.data()
             << "target" << m_applicationPage->url().toString();
    recordDiagnosticEvent(QStringLiteral("teletext isolated page return ") +
                          diagnosticSnippet(m_applicationPage->url().toString()));

    m_usingTeletextPage = false;
    setPage(m_applicationPage);
    attachPageDiagnostics();
    setCursor(Qt::BlankCursor);
    show();

    m_lastLoadUrl = url().toString();
    m_teletextReturnInProgress = false;
    m_teletextReturnUrl = QUrl();

    if (teletextPage && teletextPage != m_applicationPage) {
        teletextPage->disconnect(this);
        teletextPage->triggerAction(QWebEnginePage::Stop);
        teletextPage->deleteLater();
    }
    m_teletextPage = Q_NULLPTR;

    showApplicationOverlay(QStringLiteral("teletext isolated page return"));
    refreshApplicationAfterTeletextReturn();
    retryOverlayRepaint(QStringLiteral("teletext isolated page return"), 80);
    retryOverlayRepaint(QStringLiteral("teletext isolated page return"), 240);
    emit hbbtvCommand(CommandClient::CommandPageLoadFinished, url().toString());
    return true;
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

void WebView::resetPageForTeletextReturn()
{
    QWebEnginePage *oldPage = page();
    QWebEngineProfile *profile = oldPage ? oldPage->profile() : Q_NULLPTR;
    if (!profile) {
        qWarning() << "[OpenHbbTV] teletext page reset skipped: no profile";
        return;
    }

    qDebug() << "[OpenHbbTV] reset WebEngine page for teletext return"
             << "oldPage" << oldPage
             << "target" << m_teletextReturnUrl.toString();
    if (oldPage) {
        oldPage->disconnect(this);
        oldPage->triggerAction(QWebEnginePage::Stop);
    }

    WebPage *newPage = new WebPage(profile, this);
    setPage(newPage);
    attachPageDiagnostics();
    setCursor(Qt::BlankCursor);
    show();

    injectHbbTVScripts(m_hbbtvScriptSrc.isEmpty() ? QStringLiteral("qrc:/hbbtv_polyfill.js") : m_hbbtvScriptSrc);
    injectXmlHttpRequestScripts();
    if (m_currentOnid != -1 && m_currentTsid != -1 && m_currentSid != -1)
        setCurrentChannel(m_currentOnid, m_currentTsid, m_currentSid);
    if (!m_language.isEmpty())
        setLanguage(m_language);
    if (!m_scriptDebugging.isEmpty())
        setScriptDebugging(m_scriptDebugging);

    if (oldPage)
        oldPage->deleteLater();
}

void WebView::beginTeletextReturn()
{
    if (m_teletextReturnInProgress) {
        qDebug() << "[OpenHbbTV] ignore repeated teletext leading zero during return";
        return;
    }

    m_teletextReturnInProgress = true;
    m_teletextReturnUrl = QUrl();
    m_teletextDigitBuffer.clear();
    m_teletextDigitTimer->stop();

    QWebEngineHistory *history = page() ? page()->history() : Q_NULLPTR;
    const QUrl backUrl = history && history->canGoBack() ? history->backItem().url() : QUrl();
    const bool forceRestart = openHbbTVEnvEnabled("OPENHBBTV_TELETEXT_RESTART_ON_ZERO", false);
    const QString returnMode = QString::fromLocal8Bit(qgetenv("OPENHBBTV_TELETEXT_RETURN_MODE")).trimmed().toLower();
    const bool forceHistoryBack = returnMode == QStringLiteral("history");
    const QUrl applicationUrl = m_applicationPage ? m_applicationPage->url() : QUrl();
    const QUrl targetUrl = applicationUrl.isValid() && !applicationUrl.isEmpty() && !isTeletextUrl(applicationUrl)
        ? applicationUrl
        : (backUrl.isValid() && !isTeletextUrl(backUrl) ? backUrl : m_initialUrl);

    if (forceHistoryBack && !forceRestart && backUrl.isValid() && !isTeletextUrl(backUrl)) {
        m_teletextReturnUrl = backUrl;
        qDebug() << "[OpenHbbTV] teletext leading zero history back"
                 << "from" << url().toString()
                 << "to" << backUrl.toString();
        recordDiagnosticEvent(QStringLiteral("teletext leading-zero history back ") + diagnosticSnippet(backUrl.toString()));
        history->back();
        QTimer::singleShot(900, this, [this]() {
            if (m_teletextReturnInProgress && isTeletextUrl()) {
                qDebug() << "[OpenHbbTV] teletext history back still on teletext; force target url"
                         << m_teletextReturnUrl.toString();
                if (m_teletextReturnUrl.isValid() && !m_teletextReturnUrl.isEmpty())
                    setUrl(m_teletextReturnUrl);
            }
        });
    } else if (!forceRestart && targetUrl.isValid() && !targetUrl.isEmpty()) {
        m_teletextReturnUrl = targetUrl;
        qDebug() << "[OpenHbbTV] teletext leading zero direct return"
                 << "from" << url().toString()
                 << "to" << targetUrl.toString()
                 << "backUrl" << backUrl.toString()
                 << "mode" << (returnMode.isEmpty() ? QStringLiteral("direct") : returnMode);
        recordDiagnosticEvent(QStringLiteral("teletext leading-zero direct-return ") + diagnosticSnippet(targetUrl.toString()));
        QTimer::singleShot(80, this, [this]() {
            if (!m_teletextReturnInProgress)
                return;
            if (switchBackFromTeletextPage())
                return;
            m_usingTeletextPage = false;
            resetPageForTeletextReturn();
            qDebug() << "[OpenHbbTV] load teletext direct return url" << m_teletextReturnUrl.toString();
            setUrl(m_teletextReturnUrl);
        });
    } else {
        qDebug() << "[OpenHbbTV] teletext leading zero request fresh red-button restart"
                 << m_initialUrl.toString()
                 << "force" << forceRestart
                 << "backUrl" << backUrl.toString()
                 << "mode" << returnMode;
        recordDiagnosticEvent(QStringLiteral("teletext leading-zero restart ") + diagnosticSnippet(m_initialUrl.toString()));
        emit hbbtvCommand(CommandClient::CommandRestartApplication, QStringLiteral("redbutton"));
    }

    QTimer::singleShot(2500, this, [this]() {
        if (m_teletextReturnInProgress) {
            qDebug() << "[OpenHbbTV] teletext return still pending"
                     << "current" << url().toString()
                     << "target" << m_teletextReturnUrl.toString();
            m_teletextReturnInProgress = false;
            m_teletextReturnUrl = QUrl();
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
    // and return to the HbbTV page that opened teletext.
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
    recordDiagnosticEvent(QStringLiteral("teletext page input ") + page);
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
    recordDiagnosticEvent(QStringLiteral("sendKeyEvent %1").arg(keyCode));

    if (handleTeletextDigit(keyCode))
        return;

    if (!m_teletextDigitBuffer.isEmpty())
        flushTeletextDigitBuffer();

    const bool nativeNav = nativeNavigationKeysEnabled();

    if (handleStreamKeyFallback(keyCode))
        return;

    if (nativeNav && isNavigationOrEnterKey(keyCode)) {
        if (isStreamActive() && !m_streamOverlayVisible) {
            qDebug() << "[OpenHbbTV] stream hidden navigation key: show application overlay" << keyCode;
            showApplicationOverlay(QStringLiteral("stream hidden navigation key"));
        } else {
            qDebug() << "[OpenHbbTV] ignore direct navigation key; native Qt/libvupl handles visible UI" << keyCode;
        }
        return;
    }

    if (isStreamActive())
        showApplicationOverlay(QStringLiteral("stream key"));

    if (keyCode == VirtualKey::VK_BACK && !isStreamActive()) {
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
    const QString vkName = QString::fromLatin1(metaEnum.valueToKey(keyCode));

    QString s = QString::fromLatin1("(function() {"
                                    "  var code = %1;"
                                    "  var vkName = '%2';"
                                    "  function describe(target) {"
                                    "    try {"
                                    "      if (!target) return 'null';"
                                    "      var tag = target.tagName || target.nodeName || typeof target;"
                                    "      var id = target.id ? ('#' + target.id) : '';"
                                    "      var cls = target.className ? ('.' + String(target.className).replace(/\\s+/g, '.').slice(0, 60)) : '';"
                                    "      return tag + id + cls;"
                                    "    } catch (ignore) { return 'unknown'; }"
                                    "  }"
                                    "  function sendLog(message) {"
                                    "    try { if (window.signalopenhbbtvbrowser) window.signalopenhbbtvbrowser('LOG:' + message); } catch (ignore) {}"
                                    "  }"
                                    "  if (typeof window.__openhbbtvInjectKey === 'function') {"
                                    "    try {"
                                    "      var brokerResult = window.__openhbbtvInjectKey(code, vkName);"
                                    "      return 'broker:' + String(brokerResult);"
                                    "    }"
                                    "    catch (brokerError) {"
                                    "      console.log('OpenHbbTV key broker failed', brokerError);"
                                    "      sendLog('InjectedKey broker failed key=' + code + ' vk=' + vkName + ' error=' + String(brokerError));"
                                    "    }"
                                    "  }"
                                    "  var resolved = parseInt(code, 10) || 0;"
                                    "  if (!resolved && typeof window[vkName] !== 'undefined') {"
                                    "    resolved = parseInt(window[vkName], 10) || code;"
                                    "  }"
                                    "  var target = document.body || document.documentElement || document.activeElement || document;"
                                    "  try { if (target && target.focus) target.focus(); } catch (ignore) {}"
                                    "  function keyName(value) {"
                                    "    if (value >= 48 && value <= 57) return String.fromCharCode(value);"
                                    "    if (value === 13) return 'Enter';"
                                    "    if (value === 37) return 'ArrowLeft';"
                                    "    if (value === 38) return 'ArrowUp';"
                                    "    if (value === 39) return 'ArrowRight';"
                                    "    if (value === 40) return 'ArrowDown';"
                                    "    if (value === 403) return 'ColorF0Red';"
                                    "    if (value === 404) return 'ColorF1Green';"
                                    "    if (value === 405) return 'ColorF2Yellow';"
                                    "    if (value === 406) return 'ColorF3Blue';"
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
                                    "  var eventKey = keyName(resolved);"
                                    "  var downResult = true;"
                                    "  try { downResult = target.dispatchEvent(makeEvent('keydown')); }"
                                    "  catch (e) { console.log('OpenHbbTV keydown failed', e); sendLog('InjectedKey fallback keydown failed target=' + describe(target) + ' error=' + String(e)); }"
                                    "  window.setTimeout(function() {"
                                    "    try { target.dispatchEvent(makeEvent('keyup')); }"
                                    "    catch (e) { console.log('OpenHbbTV keyup failed', e); sendLog('InjectedKey fallback keyup failed target=' + describe(target) + ' error=' + String(e)); }"
                                    "  }, 25);"
                                    "  sendLog('InjectedKey fallback target=' + describe(target) + ' active=' + describe(document.activeElement) + ' key=' + resolved + ' vk=' + (vkName || '') + ' eventKey=' + eventKey + ' accepted=' + (!!downResult));"
                                    "  return 'fallback:' + resolved + ':' + describe(target);"
                                    "})();").arg(keyCode).arg(vkName);
    qDebug() << "[OpenHbbTV] inject keydown+keyup broker" << keyCode;
    QSharedPointer<bool> completed(new bool(false));
    const QString diagnosticLabel = QStringLiteral("#%1 injectKey %2").arg(++m_jsSeq).arg(keyCode);
    m_lastJsStarted = diagnosticLabel;
    recordDiagnosticEvent(QStringLiteral("js-start ") + diagnosticSnippet(diagnosticLabel));
    QPointer<WebView> self(this);
    page()->runJavaScript(s, [completed, keyCode, diagnosticLabel, self](const QVariant &result) {
        *completed = true;
        if (self) {
            self->m_lastJsCompleted = diagnosticLabel;
            self->recordDiagnosticEvent(QStringLiteral("js-result ") + self->diagnosticSnippet(diagnosticLabel)
                + QStringLiteral(" result=") + self->diagnosticSnippet(result.toString()));
        }
        qDebug() << "[OpenHbbTV] inject key JS result" << keyCode << result;
    });
    QTimer::singleShot(1500, this, [this, completed, keyCode, diagnosticLabel]() {
        if (*completed)
            return;
        recordDiagnosticEvent(QStringLiteral("js-timeout ") + diagnosticSnippet(diagnosticLabel));
        qWarning() << "[OpenHbbTV] inject key JS timeout" << keyCode
                   << "stream" << m_streamState << "url" << url().toString();
        requestRestartApplicationOnce(QStringLiteral("inject-key-js-timeout %1").arg(keyCode));
    });
}

void WebView::dispatchHbbtvBridgeCommand(const QString &rawCommand)
{
    qDebug() << "[OpenHbbTV] raw bridge command" << rawCommand;
    QString command = rawCommand;
    const int seqPos = command.lastIndexOf(QStringLiteral("||"));
    if (seqPos >= 0)
        command.truncate(seqPos);

    qDebug() << "[OpenHbbTV] normalized bridge command" << command;
    m_lastBridgeCommand = diagnosticSnippet(command);
    recordDiagnosticEvent(QStringLiteral("bridge-command ") + m_lastBridgeCommand);

    if (command == QStringLiteral("BROADCAST_PLAY")) {
        OpenHbbTVRequestInterceptor::setExternalPlaybackActive(false);
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
        // Do not change the local stream state here. The HbbTV page should be able
        // to show its connecting/player UI until E2 has really accepted the
        // external stream and sends HIDE_APPLICATION / SET_STREAM_STATE back.
        // Hiding immediately here breaks ARD live-stream transitions because the
        // browser is lowered before the application finished building the player.
        OpenHbbTVRequestInterceptor::setExternalPlaybackActive(true);
        qDebug() << "[OpenHbbTV] forward PLAY_STREAM to backend and wait for backend hide/state";
        emit hbbtvCommand(CommandClient::CommandPlayStream, command.mid(12));
    } else if (command == QStringLiteral("STOP_STREAM")) {
        OpenHbbTVRequestInterceptor::setExternalPlaybackActive(false);
        emit hbbtvCommand(CommandClient::CommandStopStream, QString());
    } else if (command == QStringLiteral("PAUSE_STREAM")) {
        emit hbbtvCommand(CommandClient::CommandPauseStream, QString());
    } else if (command.startsWith(QStringLiteral("SEEK_STREAM:"))) {
        emit hbbtvCommand(CommandClient::CommandSeekStream, command.mid(12));
    } else if (command.startsWith(QStringLiteral("CREATE_APPLICATION:"))) {
        emit hbbtvCommand(CommandClient::CommandCreateApplication, command.mid(19));
    } else if (command == QStringLiteral("RESTORE_BROADCAST")) {
        OpenHbbTVRequestInterceptor::setExternalPlaybackActive(false);
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
    m_lastTitle = diagnosticSnippet(title);
    recordDiagnosticEvent(QStringLiteral("titleChanged ") + m_lastTitle);
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
    m_lastLoadUrl = url().toString();
    recordDiagnosticEvent(QStringLiteral("loadFinished ok=%1 url=%2")
        .arg(ok)
        .arg(diagnosticSnippet(m_lastLoadUrl)));
    if (ok)
        m_jsTimeoutRecoveryPending = false;
    if (m_teletextReturnInProgress && ok) {
        const QUrl currentUrl = url();
        const bool targetReached = m_teletextReturnUrl.isValid() && !m_teletextReturnUrl.isEmpty() &&
            currentUrl.adjusted(QUrl::RemoveFragment) == m_teletextReturnUrl.adjusted(QUrl::RemoveFragment);
        if (targetReached || !isTeletextUrl(currentUrl)) {
            qDebug() << "[OpenHbbTV] teletext leading-zero return completed"
                     << currentUrl.toString()
                     << "target" << m_teletextReturnUrl.toString();
            m_teletextReturnInProgress = false;
            m_teletextReturnUrl = QUrl();
            showApplicationOverlay(QStringLiteral("teletext return"));
            retryOverlayRepaint(QStringLiteral("teletext return"), 120);
            retryOverlayRepaint(QStringLiteral("teletext return"), 450);
            QTimer::singleShot(80, this, &WebView::refreshApplicationAfterTeletextReturn);
            QTimer::singleShot(450, this, &WebView::refreshApplicationAfterTeletextReturn);
        }
    }
    if (ok) {
        if (size().width() == 1920 && size().height() == 1080)
            page()->runJavaScript(QString::fromLatin1("document.body.style.setProperty('zoom', '150%');"));

        page()->runJavaScript(QString::fromLatin1(
            "(function() {"
            "  try { document.documentElement.style.setProperty('cursor', 'none', 'important'); } catch (e) {}"
            "  try { document.body.style.setProperty('cursor', 'none', 'important'); } catch (e) {}"
            "  try {"
            "    var style = document.getElementById('openhbbtv-hide-cursor');"
            "    if (!style) {"
            "      style = document.createElement('style');"
            "      style.id = 'openhbbtv-hide-cursor';"
            "      style.textContent = '* { cursor: none !important; } html, body { cursor: none !important; }';"
            "      (document.head || document.documentElement).appendChild(style);"
            "    }"
            "  } catch (e) {}"
            "})();"));

        page()->runJavaScript(QString::fromLatin1("document.body.style.setProperty('overflow', 'hidden');"));

        page()->runJavaScript(QString::fromLatin1("if (document.getElementById('appmgr')) document.getElementById('appmgr').style.setProperty('visibility', 'hidden');"));
        page()->runJavaScript(QString::fromLatin1("if (document.getElementById('oipfcfg')) document.getElementById('oipfcfg').style.setProperty('visibility', 'hidden');"));
        page()->runJavaScript(QString::fromLatin1("if (document.getElementById('oipfCap')) document.getElementById('oipfCap').style.setProperty('visibility', 'hidden');"));
        page()->runJavaScript(QString::fromLatin1("if (document.getElementById('oipfDrm')) document.getElementById('oipfDrm').style.setProperty('visibility', 'hidden');"));
        emit hbbtvCommand(CommandClient::CommandPageLoadFinished, url().toString());
    }
}
