#include "webpage.h"
#include <QDebug>
#include <QTimer>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

WebPage::WebPage(QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent)
    , m_teletextNavigationInterceptEnabled(false)
{
    setBackgroundColor(Qt::transparent);

    connect(this, &QWebEnginePage::windowCloseRequested, this, &WebPage::windowCloseRequested);
}

void WebPage::setTeletextNavigationInterceptEnabled(bool enabled)
{
    if (m_teletextNavigationInterceptEnabled == enabled)
        return;
    m_teletextNavigationInterceptEnabled = enabled;
    qDebug() << "[OpenHbbTV] teletext navigation intercept" << enabled << this;
}

bool WebPage::teletextNavigationInterceptEnabled() const
{
    return m_teletextNavigationInterceptEnabled;
}

bool WebPage::isTeletextUrl(const QUrl &url)
{
    const QString host = url.host().toLower();
    const QString path = url.path().toLower();
    const QString full = url.toString().toLower();

    return host.startsWith(QStringLiteral("vtx.")) ||
           host.contains(QStringLiteral("videotext")) ||
           path.contains(QStringLiteral("videotext")) ||
           full.contains(QStringLiteral("vtx."));
}

bool WebPage::acceptNavigationRequest(const QUrl &url,
                                      QWebEnginePage::NavigationType type,
                                      bool isMainFrame)
{
    if (m_teletextNavigationInterceptEnabled && isMainFrame && isTeletextUrl(url)) {
        qDebug() << "[OpenHbbTV] intercept teletext navigation"
                 << "url" << url.toString()
                 << "type" << type
                 << "page" << this;
        QTimer::singleShot(0, this, [this, url]() {
            emit teletextNavigationRequested(url);
        });
        return false;
    }

    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

void WebPage::windowCloseRequested()
{
    qDebug() << "windowCloseRequested";
}

void WebPage::javaScriptConsoleMessage(WebPage::JavaScriptConsoleMessageLevel level,
                                       const QString &message,
                                       int lineId,
                                       const QString &sourceId)
{
//    QWebEnginePage::javaScriptConsoleMessage(level, message, lineId, sourceId);
    QString levelStr;
    switch (level) {
    case InfoMessageLevel:    levelStr = "INFO"; break;
    case WarningMessageLevel: levelStr = "WARNING"; break;
    case ErrorMessageLevel:   levelStr = "ERROR"; break;
    }

    if (level == ErrorMessageLevel) {
        qCritical().noquote() << QString("[JS %1] %2 (line %3, source %4)")
                                 .arg(levelStr, message)
                                 .arg(lineId)
                                 .arg(sourceId);
    } else if (level == WarningMessageLevel) {
        qWarning().noquote() << QString("[JS %1] %2 (line %3, source %4)")
                                 .arg(levelStr, message)
                                 .arg(lineId)
                                 .arg(sourceId);
    } else {
        qDebug().noquote() << QString("[JS %1] %2 (line %3, source %4)")
                              .arg(levelStr, message)
                              .arg(lineId)
                              .arg(sourceId);
    }
}
