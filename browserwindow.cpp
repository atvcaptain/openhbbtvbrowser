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
}

WebView *BrowserWindow::webView()
{
    return m_webView;
}

void BrowserWindow::sendHbbtvCommand(int command, const QString &data)
{
    m_commandClient->writeCommand(command, data);
}

void BrowserWindow::onBackendCommand(int command, const QString &data)
{
    switch (command) {
    case CommandClient::CommandOpenUrl:
        if (!data.isEmpty())
            m_webView->setUrl(QUrl::fromUserInput(data));
        break;
    case CommandClient::CommandSetCurrentChannel: {
        QStringList parts = data.split(QLatin1Char(','));
        if (parts.size() >= 3)
            m_webView->setCurrentChannel(parts.at(0).toInt(), parts.at(1).toInt(), parts.at(2).toInt());
        break;
    }
    case CommandClient::CommandQuit:
        QCoreApplication::quit();
        break;
    default:
        qDebug() << "Unhandled backend command" << command << data;
        break;
    }
}
