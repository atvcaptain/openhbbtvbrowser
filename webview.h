#ifndef WEBVIEW_H
#define WEBVIEW_H

#include <QWebEngineView>
#include <QLabel>
#include <QWebEngineUrlRequestInterceptor>
#include <QUrl>
#include <QTimer>
#include <QRect>
#include <QPointer>
#include <QStringList>
#include <atomic>

class QWebEnginePage;

class OpenHbbTVRequestInterceptor : public QWebEngineUrlRequestInterceptor {
public:
    explicit OpenHbbTVRequestInterceptor(bool logRequests = false, QObject *parent = Q_NULLPTR)
        : QWebEngineUrlRequestInterceptor(parent)
        , m_logRequests(logRequests)
    {
    }

    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const QUrl requestUrl = info.requestUrl();
        const QString url = requestUrl.toString(QUrl::FullyEncoded);
        const QWebEngineUrlRequestInfo::ResourceType type = info.resourceType();
        const bool nativeMediaRequest = type == QWebEngineUrlRequestInfo::ResourceTypeMedia ||
                                        type == QWebEngineUrlRequestInfo::ResourceTypeObject;

        if (nativeMediaRequest) {
            qWarning().noquote() << "[OpenHbbTV] blocked native Qt media/object request"
                                 << info.requestMethod()
                                 << type
                                 << url;
            info.block(true);
            return;
        }

        if (externalPlaybackActive() && shouldBlockExternalPlaybackRequest(requestUrl, type)) {
            qWarning().noquote() << "[OpenHbbTV] blocked external playback background request"
                                 << info.requestMethod()
                                 << type
                                 << url;
            info.block(true);
            return;
        }

        if (m_logRequests) {
            qDebug().noquote() << "[NET] Request:"
                               << info.requestMethod()
                               << type
                               << url;
        }
    }

    static void setExternalPlaybackActive(bool active);
    static bool externalPlaybackActive();

private:
    static bool shouldBlockExternalPlaybackRequest(const QUrl &requestUrl, QWebEngineUrlRequestInfo::ResourceType type);
    static std::atomic_bool s_externalPlaybackActive;
    bool m_logRequests;
};

class WebView : public QWebEngineView
{
    Q_OBJECT

public:
    WebView(QWidget *parent = Q_NULLPTR);
    void injectHbbTVScripts(const QString &src);
    void injectXmlHttpRequestScripts();
    void setCurrentChannel(const int &onid, const int &tsid, const int &sid);
    void setBroadcastInfo(const QString &json);
    void setStreamState(int state, int error);
    void showApplicationOverlay(const QString &reason = QString());
    void hideApplicationOverlay(const QString &reason = QString());
    bool isStreamActive() const;
    void setInitialUrl(const QUrl &url);
    void setLanguage(const QString &language);
    void setScriptDebugging(const QString &scriptDebugging);
    void attachPageDiagnostics();
    void recordBackendCommand(int command, const QString &data);
    void recordBrowserCommand(int command, const QString &data);

Q_SIGNALS:
    void hbbtvCommand(int command, const QString &data);

public Q_SLOTS:
    void sendKeyEvent(const int &keyCode);

protected Q_SLOTS:
    void titleChanged(const QString &title);
    void loadFinished(bool ok);

private:
    void dispatchHbbtvBridgeCommand(const QString &command);
    bool isTeletextUrl() const;
    bool isTeletextUrl(const QUrl &candidate) const;
    bool isTeletextDigitKey(int keyCode) const;
    QChar teletextDigitFromKeyCode(int keyCode) const;
    bool handleTeletextDigit(int keyCode);
    void flushTeletextDigitBuffer();
    void beginTeletextReturn();
    void loadInitialUrlAfterTeletextReturn(int delayMs);
    void refreshApplicationAfterTeletextReturn();
    void runJavaScriptWithWatchdog(const QString &label, const QString &script, int timeoutMs = 1200, bool recoverOnTimeout = false);
    void recordDiagnosticEvent(const QString &event);
    void dumpRenderCrashDiagnostics(int status, int exitCode);
    QString diagnosticSnippet(const QString &text, int maxLength = 220) const;
    bool shouldForceNativeVisibleRefresh(const QString &reason) const;
    void forceNativeVisibleRefresh(QWidget *top, const QString &reason);
    void repaintOverlaySurface(const QString &reason);
    void retryOverlayRepaint(const QString &reason, int delayMs);
    void retryStreamOverlayVisible(const QString &reason, int delayMs);
    void syncDeferredStreamStateToApplication(const QString &reason);
    void requestRestartApplicationOnce(const QString &reason);
    bool streamRendererFreezeEnabled() const;
    bool streamRendererFreezeViewEnabled() const;
    bool streamRendererFreezeLifecycleEnabled() const;
    int streamRendererFreezeDelayMs() const;
    void scheduleStreamRendererFreeze(const QString &reason);
    void setStreamRendererActive(bool active, const QString &reason);
    bool isInitialUrl(const QUrl &candidate) const;
    void injectKeyEvent(int keyCode);
    bool nativeNavigationKeysEnabled() const;
    bool isNavigationOrEnterKey(int keyCode) const;
    bool handleStreamKeyFallback(int keyCode);
    QUrl m_initialUrl;
    QString m_lastBroadcastInfo;
    int m_streamState;
    int m_streamError;
    bool m_streamOverlayVisible;
    bool m_streamOverlayLowered;
    bool m_silentPlayingStatePending;
    bool m_streamRendererFrozen;
    bool m_streamViewHiddenForPlayback;
    qint64 m_streamOverlayHoldUntilMs;
    QRect m_streamOverlaySavedGeometry;
    bool m_streamOverlayGeometryValid;
    bool m_jsTimeoutRecoveryPending;
    QPointer<QWebEnginePage> m_diagnosticsPage;
    QStringList m_recentDiagnosticEvents;
    int m_diagnosticSeq;
    int m_jsSeq;
    QString m_lastJsStarted;
    QString m_lastJsCompleted;
    QString m_lastBridgeCommand;
    QString m_lastBackendCommand;
    QString m_lastBrowserCommand;
    QString m_lastLoadUrl;
    QString m_lastTitle;
    bool m_teletextReturnInProgress;
    QString m_teletextDigitBuffer;
    QTimer *m_teletextDigitTimer;
    QLabel *m_quitMsg;
    int m_quitMsgStatus;
};

#endif // WEBVIEW_H
