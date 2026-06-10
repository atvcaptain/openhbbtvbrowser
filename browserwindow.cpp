#include "browsercontrol.h"
#include "browserwindow.h"
#include "webpage.h"
#include "webview.h"
#include <QCoreApplication>
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
    m_webView->show();

    setCentralWidget(m_webView);

    connect(m_webView, &WebView::hbbtvCommand, this, &BrowserWindow::sendHbbtvCommand);
    connect(m_commandClient, &CommandClient::commandReceived, this, &BrowserWindow::onBackendCommand);
    qDebug() << "[OpenHbbTV] BrowserWindow created";
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
    const bool ok = m_commandClient->writeCommand(command, data);
    qDebug() << "[OpenHbbTV] browser->e2 command result" << command << ok;
}

void BrowserWindow::onBackendCommand(int command, const QString &data)
{
    qDebug() << "[OpenHbbTV] e2->browser command" << command << data;
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
    case CommandClient::CommandSetStreamState: {
        QStringList parts = data.split(QLatin1Char(','));
        int state = parts.value(0).toInt();
        int error = parts.size() > 1 ? parts.value(1).toInt() : -1;
        m_webView->setStreamState(state, error);
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
