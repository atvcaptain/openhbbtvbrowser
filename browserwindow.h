#ifndef BROWSERWINDOW_H
#define BROWSERWINDOW_H

#include <QMainWindow>
#include <QCloseEvent>
#include <QHideEvent>
#include <QShowEvent>

class CommandClient;
class WebView;

class BrowserWindow : public QMainWindow
{
    Q_OBJECT

public:
    BrowserWindow(QWidget *parent = Q_NULLPTR, Qt::WindowFlags flags = Qt::Widget);
    WebView *webView();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

protected Q_SLOTS:
    void sendHbbtvCommand(int command, const QString &data);
    void onBackendCommand(int command, const QString &data);

private:
    CommandClient *m_commandClient;
    WebView *m_webView;
};

#endif // BROWSERWINDOW_H
