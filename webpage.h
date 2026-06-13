#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <QWebEnginePage>
#include <QUrl>

class WebPage : public QWebEnginePage
{
    Q_OBJECT

public:
    WebPage(QWebEngineProfile *profile, QObject *parent = Q_NULLPTR);
    void setTeletextNavigationInterceptEnabled(bool enabled);
    bool teletextNavigationInterceptEnabled() const;

Q_SIGNALS:
    void teletextNavigationRequested(const QUrl &url);

protected Q_SLOTS:
    void windowCloseRequested();

protected:
    bool acceptNavigationRequest(const QUrl &url,
                                 QWebEnginePage::NavigationType type,
                                 bool isMainFrame) override;
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString &message,
                                  int lineId,
                                  const QString &sourceId) override;

private:
    static bool isTeletextUrl(const QUrl &url);

    bool m_teletextNavigationInterceptEnabled;
};

#endif // WEBPAGE_H
