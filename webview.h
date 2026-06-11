#ifndef WEBVIEW_H
#define WEBVIEW_H

#include <QWebEngineView>
#include <QLabel>
#include <QWebEngineUrlRequestInterceptor>
#include <QUrl>
#include <QTimer>
#include <QRect>

class RequestLogger : public QWebEngineUrlRequestInterceptor {
public:
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        qDebug().noquote() << "[NET] Request:"
                           << info.requestMethod()
                           << info.requestUrl().toString();
    }
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
    bool shouldForceNativeVisibleRefresh(const QString &reason) const;
    void forceNativeVisibleRefresh(QWidget *top, const QString &reason);
    void retryStreamOverlayVisible(const QString &reason, int delayMs);
    bool isInitialUrl(const QUrl &candidate) const;
    void injectKeyEvent(int keyCode);
    bool nativeNavigationKeysEnabled() const;
    bool isNavigationOrEnterKey(int keyCode) const;
    bool handleStreamKeyFallback(int keyCode);
    QUrl m_initialUrl;
    QString m_lastBroadcastInfo;
    int m_streamState;
    bool m_streamOverlayVisible;
    qint64 m_streamOverlayHoldUntilMs;
    QRect m_streamOverlaySavedGeometry;
    bool m_streamOverlayGeometryValid;
    bool m_teletextReturnInProgress;
    QString m_teletextDigitBuffer;
    QTimer *m_teletextDigitTimer;
    QLabel *m_quitMsg;
    int m_quitMsgStatus;
};

#endif // WEBVIEW_H
