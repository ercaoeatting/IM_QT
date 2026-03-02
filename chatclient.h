#ifndef CHATCLIENT_H
#define CHATCLIENT_H

#include "qstringview.h"
#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QString>
#include <QVector>

using DATA = unsigned char;
enum class TYPE : DATA {
    GROUP     = 1,
    PRIVATE   = 2,
    CONTROL   = 3,
    FILE_CTRL = 4,
    FILE_DATA = 5,
};

class ChatClient : public QObject {
    Q_OBJECT
public:
    uint32_t m_userId = 0;
    QString  m_name   = "";
    struct UserInfo {
        uint32_t id = 0;
        QString  name;
    };
    std::unordered_map<uint32_t, std::vector<QPair<QString, QString>>> m_history;
    // 聊天记录，key为对方ID，value为{时间戳,消息列表}
    explicit ChatClient(QObject* parent = nullptr);
    ~ChatClient();

    void connectTo(const QString& host, quint16 port);
    void close();
    bool isConnected() const;

    void login(uint32_t id, const QString& name);             // CONTROL {"cmd":"login"...}
    void getList();                                           // CONTROL {"cmd":"get_list"}
    void sendGroupText(const QString& text);                  // GROUP 纯文本
    void sendPrivateText(uint32_t toId, const QString& text); // PRIVATE JSON
                                                              // 发文件
    void sendFileReq(uint32_t toId, uint32_t taskId, const QString& fileName, int fileSize);
    void sendFileResp(uint32_t toId, uint32_t taskId, const QString& fileName, int fileSize,
                      bool accept);
    void sendFileDeny(uint32_t toId, uint32_t taskId);
    void sendFileData(uint32_t toId, uint32_t taskId, const QByteArray& data);
    void sendFrame(TYPE type, const QByteArray& payload);

signals:
    void connected();
    void disconnected();
    void errorOccurred(int socketError, const QString& errorString);
    void groupFileListReceived(const QJsonArray& files);
    // 群聊文本
    void groupMessageReceived(const QString& text);
    // 私聊文本
    void privateMessageReceived(uint32_t fromId, const QString& text);
    // 登录回执 {"cmd":"login_ack","ok":true/false,...}
    void loginAck(bool ok, const QString& reason);
    // 在线列表 {"cmd":"list","users":[{"id":..,"name":..}, ...]}
    void userListReceived(const QVector<UserInfo>& users);
    // 错误弹窗
    void serverError(const QString& reason);

    // 收文件
    void fileReqReceived(uint32_t fromId, uint32_t taskId, const QString& fileName, int fileSize);
    void fileRespReceived(uint32_t fromId, uint32_t taskId, const QString& fileName, bool accept);
    void fileDenyReceived(uint32_t fromId, uint32_t taskId);
    void fileDataReceived(uint32_t fromId, uint32_t taskId, const QByteArray& data);
private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError err);

private:
    static QByteArray packFrame(TYPE type, const QByteArray& payload);
    bool              tryParse(TYPE& type, QByteArray& payload);
    // 解析控制消息
    void handleControlJson(const QByteArray& payload);
    // 解析私聊消息
    void handlePrivateJson(const QByteArray& payload);

    // 解析文件相关的服务器过来的包
    // 解析文件控制消息
    void handleFileCtlJson(const QByteArray& payload);
    // 解析文件数据(64KB)
    void handleFile(const QByteArray& payload);


private:
    QTcpSocket* m_sock = nullptr;
    QByteArray  m_recvBuf;
};

#endif // CHATCLIENT_H