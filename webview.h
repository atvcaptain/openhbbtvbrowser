#ifndef WEBVIEW_H
#define WEBVIEW_H

#include <QWebEngineView>
#include <QLabel>
#include <QWebEngineUrlRequestInterceptor>
#include <QUrl>

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
    QUrl m_initialUrl;
    QLabel *m_quitMsg;
    int m_quitMsgStatus;
};

#endif // WEBVIEW_H
