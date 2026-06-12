#include "webview.h"
#include "browsercontrol.h"
#include "virtualkey.h"
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QFileInfo>
#include <QTimer>
#include <QWidget>
#include <QDateTime>
#include <QCoreApplication>
#include <QPointer>
#include <QSharedPointer>
#include <QVariant>

WebView::WebView(QWidget *parent)
    : QWebEngineView(parent)
    , m_streamState(0)
    , m_streamOverlayVisible(true)
    , m_streamOverlayLowered(false)
    , m_streamOverlayHoldUntilMs(0)
    , m_streamOverlayGeometryValid(false)
    , m_jsTimeoutRecoveryPending(false)
    , m_teletextReturnInProgress(false)
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

    setCursor(Qt::BlankCursor);
    setMouseTracking(false);

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
    attachPageDiagnostics();
}

void WebView::attachPageDiagnostics()
{
    QWebEnginePage *currentPage = page();
    if (!currentPage || m_diagnosticsPage == currentPage)
        return;

    m_diagnosticsPage = currentPage;
    connect(currentPage, &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus status, int exitCode) {
                qWarning() << "[OpenHbbTV] renderProcessTerminated" << status << exitCode
                           << "stream" << m_streamState << url().toString();
                requestRestartApplicationOnce(QStringLiteral("render-process-terminated"));
            });
    qDebug() << "[OpenHbbTV] page diagnostics attached" << currentPage;
}

void WebView::requestRestartApplicationOnce(const QString &reason)
{
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

void WebView::runJavaScriptWithWatchdog(const QString &label, const QString &script, int timeoutMs, bool recoverOnTimeout)
{
    QSharedPointer<bool> completed(new bool(false));
    page()->runJavaScript(script, [completed, label](const QVariant &result) {
        *completed = true;
        qDebug() << "[OpenHbbTV] JS result" << label << result;
    });

    QTimer::singleShot(timeoutMs, this, [this, completed, label, recoverOnTimeout]() {
        if (*completed)
            return;
        qWarning() << "[OpenHbbTV] JS timeout" << label << "stream" << m_streamState << "url" << url().toString();
        if (recoverOnTimeout)
            requestRestartApplicationOnce(QStringLiteral("js-timeout ") + label);
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
    runJavaScriptWithWatchdog(QStringLiteral("setCurrentChannel"), s);
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
    const bool streamReason = isStreamActive() && reason.toLower().contains(QStringLiteral("stream"));
    const bool wasOverlayVisible = m_streamOverlayVisible;
    const bool wasOverlayLowered = m_streamOverlayLowered;
    m_streamOverlayVisible = true;
    if (isStreamActive())
        m_streamOverlayHoldUntilMs = QDateTime::currentMSecsSinceEpoch() + 2000;

    qDebug() << "[OpenHbbTV] show application overlay" << reason
             << "streamReason" << streamReason << "wasVisible" << wasOverlayVisible
             << "wasLowered" << wasOverlayLowered;

    QWidget *top = window();
    const bool duplicateVisibleStreamOverlay = streamReason && wasOverlayVisible && !wasOverlayLowered && top && top->isVisible();
    if (duplicateVisibleStreamOverlay) {
        // Preserve the proven key/RCU path.  Only suppress repeated native
        // EGL/libvupl refresh work when the stream overlay is already visible.
        // Repeating showFullScreen/raise/activate/repaint on every key can
        // leave shifted or transparent browser fragments.
        qDebug() << "[OpenHbbTV] skip duplicate stream overlay native refresh" << reason
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
    if (streamReason && !duplicateVisibleStreamOverlay) {
        retryStreamOverlayVisible(reason, 120);
        retryStreamOverlayVisible(reason, 450);
        retryStreamOverlayVisible(reason, 900);
    }
    const bool postStreamReturn = reason.toLower().contains(QStringLiteral("stream state stopped"));
    if ((streamReason && !duplicateVisibleStreamOverlay) || postStreamReturn) {
        repaintOverlaySurface(reason);
        retryOverlayRepaint(reason, 120);
        retryOverlayRepaint(reason, 450);
    }
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

void WebView::hideApplicationOverlay(const QString &reason)
{
    const QString reasonLower = reason.toLower();
    const bool preStreamLiveHide = reasonLower.contains(QStringLiteral("live-dash"))
        || reasonLower.contains(QStringLiteral("live stream"));
    if (!isStreamActive() && !preStreamLiveHide)
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
        const bool liveLikeReason = reasonLower.contains(QStringLiteral("live"))
            || reasonLower.contains(QStringLiteral("dash"));
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
            || hideMode == QStringLiteral("safe")
            || (hideMode.isEmpty() && liveLikeReason);

        if (nativeHide) {
            // Diagnostic fallback only. On Vu+/eglfs_libvupl this is the path
            // that can produce a one-frame flash on OK and then leave the native
            // surface invisible while Qt still reports visible=true.
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
            // Default for live DASH on Vu+/libvupl: keep the EGL surface alive,
            // but park it as a tiny visible window instead of using
            // QWidget::lower()/hide(). This keeps the browser recoverable for
            // SHOW_APPLICATION while the E2 video plane remains unobstructed.
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
            // Diagnostic fallback. This was the old default, but on Vu+/libvupl
            // it can make the Chromium display compositor report incomplete
            // framebuffers and crash the render process during live DASH. It is
            // still the default for non-live streams because VOD is currently
            // stable on this path.
            if (m_streamOverlayGeometryValid)
                top->setGeometry(m_streamOverlaySavedGeometry);
            if (!top->isVisible()) {
                top->showFullScreen();
                qDebug() << "[OpenHbbTV] recreate visible browser surface before lower" << reason << top->geometry();
            }
            top->lower();
            qDebug() << "[OpenHbbTV] lower browser window for stream without native hide" << reason
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
    runJavaScriptWithWatchdog(QStringLiteral("setStreamState %1,%2").arg(state).arg(error), s);
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
    } else if (state == 2) {
        showApplicationOverlay(QStringLiteral("stream state paused"));
    } else if (state == 0) {
        showApplicationOverlay(QStringLiteral("stream state stopped"));
        QTimer::singleShot(150, this, [this]() { showApplicationOverlay(QStringLiteral("stream state stopped retry 1")); });
        QTimer::singleShot(650, this, [this]() { showApplicationOverlay(QStringLiteral("stream state stopped retry 2")); });
    }
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
    page()->runJavaScript(s, [completed, keyCode](const QVariant &result) {
        *completed = true;
        qDebug() << "[OpenHbbTV] inject key JS result" << keyCode << result;
    });
    QTimer::singleShot(1500, this, [this, completed, keyCode]() {
        if (*completed)
            return;
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
        // Do not change the local stream state here. The HbbTV page should be able
        // to show its connecting/player UI until E2 has really accepted the
        // external stream and sends HIDE_APPLICATION / SET_STREAM_STATE back.
        // Hiding immediately here breaks ARD live-stream transitions because the
        // browser is lowered before the application finished building the player.
        qDebug() << "[OpenHbbTV] forward PLAY_STREAM to backend and wait for backend hide/state";
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
    if (ok)
        m_jsTimeoutRecoveryPending = false;
    if (m_teletextReturnInProgress && ok && isInitialUrl(url())) {
        qDebug() << "[OpenHbbTV] teletext leading-zero return completed" << url().toString();
        m_teletextReturnInProgress = false;
        QTimer::singleShot(80, this, &WebView::refreshApplicationAfterTeletextReturn);
        QTimer::singleShot(450, this, &WebView::refreshApplicationAfterTeletextReturn);
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
