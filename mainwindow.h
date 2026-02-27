#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "chatclient.h"
#include "qobject.h"
#include "qobjectdefs.h"
#include <QTimer>
#include <cstdint>
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr, ChatClient* client = nullptr);
    ~MainWindow();
    void resizeEvent(QResizeEvent* event) override;
private slots:

    void on_buttonRefresh_clicked();

    void on_buttonSend_clicked();

private:
    Ui::MainWindow* ui;
    ChatClient*     m_client; // 客户端实例，从登录窗口传入，window不创建它
    QTimer*         m_timer;  // 定时器，定时请求在线列表
    int32_t         m_chatWithId   = 0;                        // 当前正在聊天的用户ID
    QString         m_chatWithName = "";                       // 当前正在聊天的用户名字
    void switchChat(uint32_t peerId, const QString& peerName); // 切换聊天对象，更新界面标题
    void createSingleMessageWidget(const QString& text);
};
#endif // MAINWINDOW_H
