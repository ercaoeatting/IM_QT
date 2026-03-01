#include "chatclient.h"
#include "qendian.h"
#include <QtEndian>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ChatClient::ChatClient(QObject* parent) : QObject(parent), m_sock(new QTcpSocket(this))
{
    connect(m_sock, &QTcpSocket::connected, this, &ChatClient::onConnected);
    connect(m_sock, &QTcpSocket::disconnected, this, &ChatClient::onDisconnected);
    connect(m_sock, &QTcpSocket::readyRead, this, &ChatClient::onReadyRead);
    connect(m_sock, &QTcpSocket::errorOccurred, this, &ChatClient::onErrorOccurred);
}

ChatClient::~ChatClient()
{
    close();
}

void ChatClient::connectTo(const QString& host, quint16 port)
{
    if (m_sock->state() != QAbstractSocket::UnconnectedState) m_sock->abort();
    m_recvBuf.clear();
    m_sock->connectToHost(host, port);
}

void ChatClient::close()
{
    if (m_sock->state() != QAbstractSocket::UnconnectedState) { m_sock->disconnectFromHost(); }
}

bool ChatClient::isConnected() const
{
    return m_sock->state() == QAbstractSocket::ConnectedState;
}

void ChatClient::sendFrame(TYPE type, const QByteArray& payload)
{
    if (!isConnected()) return;
    m_sock->write(packFrame(type, payload));
}


void ChatClient::login(uint32_t id, const QString& name)
{
    QJsonObject obj;
    obj["cmd"]               = "login";
    obj["id"]                = QString::number(id);
    obj["name"]              = name;
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    sendFrame(TYPE::CONTROL, payload);
}

void ChatClient::getList()
{
    QJsonObject obj;
    obj["cmd"]               = "get_list";
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    sendFrame(TYPE::CONTROL, payload);
}

void ChatClient::sendGroupText(const QString& text)
{
    sendFrame(TYPE::GROUP, text.toUtf8());
}

void ChatClient::sendPrivateText(uint32_t toId, const QString& text)
{
    QJsonObject obj;
    obj["cmd"]               = "chat";
    obj["to"]                = QString::number(toId);
    obj["msg"]               = text;
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    sendFrame(TYPE::PRIVATE, payload);
}


void ChatClient::onConnected()
{
    emit connected();
}
void ChatClient::onDisconnected()
{
    emit disconnected();
}

void ChatClient::onErrorOccurred(QAbstractSocket::SocketError err)
{
    emit errorOccurred((int)err, m_sock->errorString());
}

void ChatClient::onReadyRead()
{
    m_recvBuf += m_sock->readAll();

    while (true) {
        TYPE       type;
        QByteArray payload;
        if (!tryParse(type, payload)) break;

        switch (type) {
        case TYPE::GROUP: {
            emit groupMessageReceived(QString::fromUtf8(payload));
            break;
        }
        case TYPE::PRIVATE: {
            handlePrivateJson(payload);
            break;
        }
        case TYPE::CONTROL: {
            handleControlJson(payload);
            break;
        }
        case TYPE::FILE_CTRL: {
            handleFileCtlJson(payload);
            break;
        }
        case TYPE::FILE_DATA: {
            handleFile(payload);
            break;
        }
        default:
            break;
        }
    }
}

QByteArray ChatClient::packFrame(TYPE type, const QByteArray& data)
{
    const uint32_t len = 1 + data.size();
    QByteArray     frame;
    frame.resize(4 + len);

    uint32_t be = qToBigEndian(len);
    memcpy(frame.data(), &be, 4);

    frame[4] = (char)(DATA)type;

    if (!data.isEmpty()) { memcpy(frame.data() + 5, data.data(), data.size()); }
    return frame;
}

bool ChatClient::tryParse(TYPE& type, QByteArray& data)
{
    if (m_recvBuf.size() < 5) return false;
    uint32_t transLen = 0;
    memcpy(&transLen, m_recvBuf.constData(), 4);
    const uint32_t len = qFromBigEndian(transLen);

    if (len < 1 || len > 100 * 1024 * 1024) { return false; }
    if (m_recvBuf.size() < len + 4) return false;

    type = (TYPE)(DATA)m_recvBuf[4];

    const int messLen = (int)len - 1;
    data = (messLen > 0) ? QByteArray(m_recvBuf.constData() + 5, messLen) : QByteArray();

    m_recvBuf.remove(0, 4 + len);
    return true;
}

static bool parseJsonObj(const QByteArray& payload, QJsonObject& obj)
{
    QJsonParseError e{};
    QJsonDocument   doc = QJsonDocument::fromJson(payload, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) return false;
    obj = doc.object();
    return true;
}

void ChatClient::handlePrivateJson(const QByteArray& payload)
{
    // {"cmd":"chat","from":"245","msg":"hello"}
    QJsonObject obj;
    if (!parseJsonObj(payload, obj)) return;

    const QString cmd = obj.value("cmd").toString();
    if (cmd != "chat") return;

    const QString fromS  = obj.value("from").toString();
    const QString msg    = obj.value("msg").toString();
    bool          ok     = false;
    uint32_t      fromId = fromS.toUInt(&ok);
    if (!ok) return;

    emit privateMessageReceived(fromId, msg);
}


void ChatClient::handleControlJson(const QByteArray& payload)
{
    QJsonObject obj;
    if (!parseJsonObj(payload, obj)) return;

    const QString cmd = obj.value("cmd").toString();

    if (cmd == "login_ack") {
        const bool    ok     = obj.value("ok").toBool(false);
        const QString reason = obj.value("reason").toString();
        emit          loginAck(ok, reason);
        return;
    }

    if (cmd == "list") {
        QVector<UserInfo> users;
        const QJsonArray  arr = obj.value("users").toArray();
        users.reserve(arr.size());
        for (auto v : arr) {
            if (!v.isObject()) continue;
            QJsonObject u = v.toObject();
            UserInfo    info;
            info.id   = (uint32_t)u.value("id").toInt(0);
            info.name = u.value("name").toString();
            if (info.id != 0) users.push_back(info);
        }
        emit userListReceived(users);
        return;
    }

    if (cmd == "error") {
        emit serverError(obj.value("reason").toString());
        return;
    }
}

void ChatClient::handleFileCtlJson(const QByteArray& payload)
{
    QJsonObject obj;
    if (!parseJsonObj(payload, obj)) return;

    QString  cmd    = obj.value("cmd").toString();
    uint32_t fromId = obj.value("from").toString().toUInt();
    uint32_t taskId = (uint32_t)obj.value("taskId").toInt(); // 提取任务ID

    if (cmd == "file_send_req") {
        QString fileName = obj.value("fileName").toString();
        int     fileSize = obj.value("fileSize").toInt();
        emit    fileReqReceived(fromId, taskId, fileName, fileSize);
    }
    else if (cmd == "file_send_resp") {
        QString fileName = obj.value("fileName").toString();
        bool    accept   = obj.value("accept").toBool();
        emit    fileRespReceived(fromId, taskId, fileName, accept);
    }
    else if (cmd == "file_send_deny") {
        emit fileDenyReceived(fromId, taskId);
    }
}

void ChatClient::handleFile(const QByteArray& payload)
{
    // 4字节fromId + 4字节taskId
    if (payload.size() < 8) return;

    uint32_t net_from_id, net_task_id;
    memcpy(&net_from_id, payload.constData(), 4);
    memcpy(&net_task_id, payload.constData() + 4, 4);

    uint32_t   fromId   = qFromBigEndian(net_from_id);
    uint32_t   taskId   = qFromBigEndian(net_task_id); // 哪个文件的数据块
    QByteArray fileData = payload.mid(8);              // 后面数据
    emit       fileDataReceived(fromId, taskId, fileData);
}



void ChatClient::sendFileReq(uint32_t toId, uint32_t taskId, const QString& fileName, int fileSize)
{
    QJsonObject obj;
    obj["cmd"]      = "file_send_req";
    obj["to"]       = QString::number(toId);
    obj["taskId"]   = (qint64)taskId; // 塞入任务ID
    obj["fileName"] = fileName;
    obj["fileSize"] = fileSize;
    sendFrame(TYPE::FILE_CTRL, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ChatClient::sendFileResp(uint32_t toId, uint32_t taskId, const QString& fileName, int fileSize,
                              bool accept)
{
    QJsonObject obj;
    obj["cmd"]      = "file_send_resp";
    obj["to"]       = QString::number(toId);
    obj["taskId"]   = (qint64)taskId;
    obj["fileName"] = fileName;
    obj["fileSize"] = fileSize;
    obj["accept"]   = accept;
    sendFrame(TYPE::FILE_CTRL, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ChatClient::sendFileDeny(uint32_t toId, uint32_t taskId)
{
    QJsonObject obj;
    obj["cmd"]    = "file_send_deny";
    obj["to"]     = QString::number(toId);
    obj["taskId"] = (qint64)taskId;
    sendFrame(TYPE::FILE_CTRL, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// 【核心魔法】二进制拼包：[4字节接收方ID] + [4字节任务ID] + [文件纯数据]
void ChatClient::sendFileData(uint32_t toId, uint32_t taskId, const QByteArray& data)
{
    QByteArray payload;
    payload.resize(8 + data.size()); // 头部变成了 8 个字节

    uint32_t netToId   = qToBigEndian(toId);
    uint32_t netTaskId = qToBigEndian(taskId); // 任务ID也要转成网络字节序

    memcpy(payload.data(), &netToId, 4);
    memcpy(payload.data() + 4, &netTaskId, 4); // 紧跟在后面
    if (!data.isEmpty()) { memcpy(payload.data() + 8, data.constData(), data.size()); }

    sendFrame(TYPE::FILE_DATA, payload);
}