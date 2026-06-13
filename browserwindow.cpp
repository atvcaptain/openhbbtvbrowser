#include "browsercontrol.h"
#include "browserwindow.h"
#include "webpage.h"
#include "webview.h"
#include <QCoreApplication>
#include <QCursor>
#include <QStringList>
#include <QUrl>
#include <QWebEngineProfile>

BrowserWindow::BrowserWindow(QWidget *parent, Qt::WindowFlags flags)
    : QMainWindow(parent, flags)
    , m_commandClient(new CommandClient)
    , m_webView(new WebView(this))
{
    WebPage *page = new WebPage(QWebEngineProfile::defaultProfile(), m_webView);
    m_webView->setPage(page);
    m_webView->attachPageDiagnostics();
    m_webView->setCursor(Qt::BlankCursor);
    m_webView->show();

    setCursor(Qt::BlankCursor);
    setCentralWidget(m_webView);

    connect(m_webView, &WebView::hbbtvCommand, this, &BrowserWindow::sendHbbtvCommand);
    connect(m_commandClient, &CommandClient::commandReceived, this, &BrowserWindow::onBackendCommand);
    qDebug() << "[OpenHbbTV] BrowserWindow created";
    qDebug() << "[OpenHbbTV] build id e2-rcu-owner-v51-safe-zdf-probes-20260613";
    qDebug() << "[OpenHbbTV] backend command support OPEN_URL SET_CHANNEL BROADCAST_INFO SHOW_APPLICATION HIDE_APPLICATION INJECT_KEY SET_STREAM_STATE SET_STREAM_POSITION QUIT";
}

void BrowserWindow::showEvent(QShowEvent *event)
{
    qDebug() << "[OpenHbbTV] BrowserWindow show" << geometry() << "visible" << isVisible();
    QMainWindow::showEvent(event);
}

void BrowserWindow::hideEvent(QHideEvent *event)
{
    qDebug() << "[OpenHbbTV] BrowserWindow hide" << geometry() << "visible" << isVisible();
    QMainWindow::hideEvent(event);
}

void BrowserWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "[OpenHbbTV] BrowserWindow close" << geometry() << "visible" << isVisible();
    QMainWindow::closeEvent(event);
}

WebView *BrowserWindow::webView()
{
    return m_webView;
}

void BrowserWindow::sendHbbtvCommand(int command, const QString &data)
{
    qDebug() << "[OpenHbbTV] browser->e2 command" << command << data;
    m_webView->recordBrowserCommand(command, data);
    const bool ok = m_commandClient->writeCommand(command, data);
    qDebug() << "[OpenHbbTV] browser->e2 command result" << command << ok;
}

void BrowserWindow::onBackendCommand(int command, const QString &data)
{
    qDebug() << "[OpenHbbTV] e2->browser command" << command << data;
    m_webView->recordBackendCommand(command, data);
    switch (command) {
    case CommandClient::CommandOpenUrl:
        if (!data.isEmpty()) {
            qDebug() << "[OpenHbbTV] open url from backend" << data;
            m_webView->setUrl(QUrl::fromUserInput(data));
        }
        break;
    case CommandClient::CommandSetCurrentChannel: {
        QStringList parts = data.split(QLatin1Char(','));
        if (parts.size() >= 3)
            m_webView->setCurrentChannel(parts.at(0).toInt(), parts.at(1).toInt(), parts.at(2).toInt());
        break;
    }
    case CommandClient::CommandSetBroadcastInfo:
        m_webView->setBroadcastInfo(data);
        break;
    case CommandClient::CommandShowApplication:
        qDebug() << "[OpenHbbTV] backend SHOW_APPLICATION" << data;
        m_webView->showApplicationOverlay(data);
        break;
    case CommandClient::CommandHideApplication:
        qDebug() << "[OpenHbbTV] backend HIDE_APPLICATION" << data;
        m_webView->hideApplicationOverlay(data);
        break;
    case CommandClient::CommandInjectKey: {
        bool ok = false;
        const int keyCode = data.toInt(&ok);
        qDebug() << "[OpenHbbTV] backend INJECT_KEY" << data << "parsed" << ok << keyCode;
        if (ok)
            m_webView->sendKeyEvent(keyCode);
        else
            qWarning() << "[OpenHbbTV] invalid INJECT_KEY payload" << data;
        break;
    }
    case CommandClient::CommandSetStreamState: {
        QStringList parts = data.split(QLatin1Char(','));
        int state = parts.value(0).toInt();
        int error = parts.size() > 1 ? parts.value(1).toInt() : -1;
        m_webView->setStreamState(state, error);
        break;
    }
    case CommandClient::CommandSetStreamPosition: {
        QStringList parts = data.split(QLatin1Char(','));
        qint64 positionMs = parts.value(0).toLongLong();
        qint64 durationMs = parts.size() > 1 ? parts.value(1).toLongLong() : -1;
        m_webView->setStreamPosition(positionMs, durationMs);
        break;
    }
    case CommandClient::CommandQuit:
        qDebug() << "[OpenHbbTV] quit requested by backend";
        QCoreApplication::quit();
        break;
    default:
        qDebug() << "Unhandled backend command" << command << data;
        break;
    }
}
