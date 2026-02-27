#ifndef CHATCLIENT_H
#define CHATCLIENT_H

#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QString>
#include <QVector>

using DATA = unsigned char;
enum class TYPE : DATA {
    GROUP   = 1,
    PRIVATE = 2,
    CONTROL = 3,
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

    explicit ChatClient(QObject* parent = nullptr);
    ~ChatClient();

    void connectTo(const QString& host, quint16 port);
    void close();
    bool isConnected() const;

    void login(uint32_t id, const QString& name);             // CONTROL {"cmd":"login"...}
    void getList();                                           // CONTROL {"cmd":"get_list"}
    void sendGroupText(const QString& text);                  // GROUP 纯文本
    void sendPrivateText(uint32_t toId, const QString& text); // PRIVATE JSON
    std::unordered_map<uint32_t, std::vector<QString>>
        m_history; // 聊天记录，key为对方ID，value为消息列表
signals:
    void connected();
    void disconnected();
    void errorOccurred(int socketError, const QString& errorString);

    // 群聊文本
    void groupMessageReceived(const QString& text);

    // 私聊文本
    void privateMessageReceived(uint32_t fromId, const QString& text);

    // 登录回执 {"cmd":"login_ack","ok":true/false,...}
    void loginAck(bool ok, const QString& reason);

    // 在线列表 {"cmd":"list","users":[{"id":..,"name":..}, ...]}
    void userListReceived(const QVector<UserInfo>& users);

    void serverError(const QString& reason);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError err);

private:
    static QByteArray packFrame(TYPE type, const QByteArray& payload);
    bool              tryParse(TYPE& type, QByteArray& payload);
    void              sendFrame(TYPE type, const QByteArray& payload);
    // 解析控制消息
    void handleControlJson(const QByteArray& payload);
    // 解析私聊消息
    void handlePrivateJson(const QByteArray& payload);

private:
    QTcpSocket* m_sock = nullptr;
    QByteArray  m_recvBuf;
};

#endif // CHATCLIENT_H