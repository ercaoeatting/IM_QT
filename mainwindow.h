#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <QTimer>
#include <cstdint>
#include <QProgressBar>
#include <QFile>
#include <QLabel>
#include <QDateTime>
#include <QPair>
#include <unordered_map>
#include "chatclient.h"
#include "filedialog.h"

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

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private slots:


    void on_buttonSend_clicked();

    void on_buttonFind_clicked();

    void on_buttonSendFile_clicked();

    void fileReqReceived(uint32_t fromId, uint32_t taskId, const QString& fileName, int fileSize);
    void fileRespReceived(uint32_t fromId, uint32_t taskId, const QString& fileName, bool accept);
    void fileDenyReceived(uint32_t fromId, uint32_t taskId);
    void fileDataReceived(uint32_t fromId, uint32_t taskId, const QByteArray& data);

    void on_pushButton_clicked();

    void on_btnGroupFiles_clicked();

private:
    Ui::MainWindow* ui;
    ChatClient*     m_client; // 客户端实例，从登录窗口传入，window不创建它
    QTimer*         m_timer;  // 定时器，定时请求在线列表

    // 聊天
    int32_t m_chatWithId   = 0;                                // 当前正在聊天的用户ID
    QString m_chatWithName = "";                               // 当前正在聊天的用户名字
    void switchChat(uint32_t peerId, const QString& peerName); // 切换聊天对象，更新界面标题
    void createSingleMessageWidget(const QString& text, const QString& timeStr);
    // 文件任务管理
    struct FileTask {
        uint32_t taskId;                // 任务唯一标识
        uint32_t userId;                // 对方的用户ID (发给谁/谁发来的)
        QString  fileName;              // 文件名
        QString  filePath;              // 本地绝对路径
        int      totalSize   = 0;       // 总大小
        int      nowLoadSize = 0;       // 已传大小
        bool     isSend      = false;   // true 发送方，false 接收方
        QFile*   file        = nullptr; // 本地文件句柄

        QProgressBar* progressBar = nullptr;
        QLabel*       statusLabel = nullptr;
        QTimer*       sendTimer   = nullptr;
    };
    std::unordered_map<uint32_t, FileTask> m_taskId2FileTasks;
    inline uint32_t                        generateTaskId()
    {
        return (uint32_t)(QDateTime::currentMSecsSinceEpoch() % 1000000000) + (rand() % 1000);
    }
    FileDialog* m_fileAll = nullptr;
    void        onFileDataReceived(uint32_t fromId, uint32_t taskId, const QByteArray& data);
};
#endif // MAINWINDOW_H
