#include <QObject>
#include <QMessageBox>
#include <QDateTime>
#include <cstdint>

#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QAction>
#include <QObject>
#include <QStringView>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include "chatclient.h"
#include "filedialog.h"
#include "qicon.h"
#include "qmenu.h"
#include "qmessagebox.h"
#include "ui_mainwindow.h"
#include "mainwindow.h"
#include "searchdialog.h"

static void parseMsgDetails(const QString& raw, bool& isMe, QString& sender, QString& plain)
{
    QString s = raw.trimmed();
    isMe      = false;
    sender    = "未知用户";

    if (s.startsWith("[me]")) {
        isMe   = true;
        sender = "我";
        plain  = s.mid(4).trimmed();
    }
    else if (s.startsWith('[')) {
        int r = s.indexOf(']');
        if (r > 1) {
            sender = s.mid(1, r - 1); // 提取 ID
            plain  = s.mid(r + 1).trimmed();
        }
    }
    else {
        plain = s;
    }
}
MainWindow::MainWindow(QWidget* parent, ChatClient* client)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_client(client)

{
    ui->setupUi(this);
    // ui->splitter->setSizes(QList<int>{1, 2});
    this->setWindowIcon(QIcon(":/icon/group.png"));
    ui->buttonSend->setStyleSheet("QPushButton {"
                                  "    background-color: #07C160;"
                                  "    color: white;"
                                  "    border-radius: 4px;"
                                  "    border: none;"
                                  "    padding: 6px 15px;"
                                  "}"
                                  "QPushButton:hover {"
                                  "    background-color: #06AD56;"
                                  "}"
                                  "QPushButton:disabled {"
                                  "    background-color: #E5E5E5;"
                                  "    color: #B2B2B2;"
                                  "}");

    ui->buttonSend->setEnabled(false);
    connect(ui->textEditInput, &QTextEdit::textChanged, this, [this]() {
        QString text = ui->textEditInput->toPlainText().trimmed();
        ui->buttonSend->setEnabled(!text.isEmpty());
    });
    m_client->setParent(this);
    m_client->getList();
    QString title = QString("Chat - [%1] %2 已登录").arg(m_client->m_userId).arg(m_client->m_name);
    setWindowTitle(title);
    connect(m_client, &ChatClient::errorOccurred, this,
            [this](int, const QString& errStr) { QMessageBox::warning(this, "网络错误", errStr); });
    // 每秒更新在线列表
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() { m_client->getList(); });
    m_timer->start(1000);
    // 错误弹窗
    connect(m_client, &ChatClient::serverError, this,
            [this](const QString& errorReson) { QMessageBox::critical(this, "错误", errorReson); });
    // 更新在线列表逻辑
    connect(m_client, &ChatClient::userListReceived, this,
            [this](const QVector<ChatClient::UserInfo>& users) {
                ui->listWidget->clear();
                QListWidgetItem* groupItem = new QListWidgetItem("[🌐]群聊大厅");
                groupItem->setData(Qt::UserRole, 9999);
                groupItem->setData(Qt::UserRole + 1, "group");
                ui->listWidget->insertItem(0, groupItem);
                for (const auto& u : users) {
                    auto* item = new QListWidgetItem(QString("🟢[%1] %2").arg(u.id).arg(u.name));
                    item->setData(Qt::UserRole, u.id);
                    item->setData(Qt::UserRole + 1, u.name);
                    ui->listWidget->addItem(item);
                }
                ui->labelOnlineText->setText(QString("在线用户 (%1)").arg(users.size()));
            });
    // 点击在线列表项，切换聊天对象
    connect(ui->listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        uint32_t peerId   = item->data(Qt::UserRole).toUInt();
        QString  peerName = item->data(Qt::UserRole + 1).toString();
        m_chatWithId      = peerId;
        m_chatWithName    = peerName;
        switchChat(peerId, peerName);
        if (peerId == 9999) {
            setWindowTitle(QString("Chat - [%1] %2 已登录 - 群聊大厅")
                               .arg(m_client->m_userId)
                               .arg(m_client->m_name));
            ui->chatWith->setText(" [🌐] 群聊大厅 ");
        }
        else {
            setWindowTitle(QString("Chat - [%1] %2 已登录 - 正在和 [%3] %4 聊天")
                               .arg(m_client->m_userId)
                               .arg(m_client->m_name)
                               .arg(peerId)
                               .arg(peerName));
            ui->chatWith->setText(QString(" [%1] %2 ").arg(QString::number(peerId)).arg(peerName));
        }
    });
    connect(m_client, &ChatClient::groupMessageReceived, this, [this](const QString& text) {
        QString message = QString("%1").arg(text.toHtmlEscaped());
        QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
        m_client->m_history[9999].push_back({timeStr, message});
        if (m_chatWithId == 9999) { createSingleMessageWidget(message, timeStr); }
    });
    connect(m_client, &ChatClient::privateMessageReceived, this,
            [this](uint32_t fromId, const QString& text) {
                QString message;
                if (fromId == m_client->m_userId) {
                    message = QString("[me] %1").arg(text.toHtmlEscaped());
                }
                else {
                    message = QString("[%1] %2").arg(fromId).arg(text.toHtmlEscaped());
                }
                // 防止自聊时重复存入历史记录（因为 send 按钮已经存过一次了）
                if (fromId != m_client->m_userId) {
                    m_client->m_history[fromId].push_back(
                        {QDateTime::currentDateTime().toString("HH:mm:ss"), message});
                }

                if (m_chatWithId == fromId) {
                    createSingleMessageWidget(message,
                                              QDateTime::currentDateTime().toString("HH:mm:ss"));
                }
            });
    connect(ui->chat->model(), &QAbstractItemModel::rowsInserted, ui->chat,
            &QListWidget::scrollToBottom);
    ui->textEditInput->installEventFilter(this);
    // 菜单：退出程序
    QAction* exitAction = new QAction("退出程序", this);
    ui->menuExit->addAction(exitAction);
    connect(exitAction, &QAction::triggered, this, []() { QApplication::quit(); });

    // 文件
    m_fileAll = new FileDialog(this);
    connect(m_client, &ChatClient::fileReqReceived, this, &MainWindow::fileReqReceived);
    connect(m_client, &ChatClient::fileRespReceived, this, &MainWindow::fileRespReceived);
    connect(m_client, &ChatClient::fileDenyReceived, this, &MainWindow::fileDenyReceived);
    connect(m_client, &ChatClient::fileDataReceived, this, &MainWindow::fileDataReceived);
    connect(m_fileAll, &FileDialog::taskAccepted, this, [this](uint32_t taskId) {
        if (m_taskId2FileTasks.find(taskId) == m_taskId2FileTasks.end()) return;
        FileTask& task = m_taskId2FileTasks[taskId];

        QString savePath = QFileDialog::getSaveFileName(
            this, task.userId == 9999 ? "保存群文件" : "选择保存位置", task.fileName);
        if (savePath.isEmpty()) {
            if (task.userId != 9999)
                m_client->sendFileResp(task.userId, taskId, task.fileName, task.totalSize, false);
            m_fileAll->updateStatus(taskId, "已取消保存");
            return;
        }

        task.filePath = savePath;
        task.file     = new QFile(savePath);
        task.file->open(QIODevice::WriteOnly);

        if (task.userId == 9999) { // === 拉取群文件 ===
            m_fileAll->updateStatus(taskId, "正在下载...");
            QJsonObject obj;
            obj["cmd"]      = "pull_group_file";
            obj["fileName"] = task.fileName;
            obj["offset"]   = 0;
            obj["taskId"] = (qint64)taskId; // 【关键修复 3】：必须告诉服务器当前任务的 ID！
            m_client->sendFrame(TYPE::CONTROL, QJsonDocument(obj).toJson(QJsonDocument::Compact));
        }
        else { // === 接收私聊文件 ===
            m_client->sendFileResp(task.userId, taskId, task.fileName, task.totalSize, true);
            m_fileAll->updateStatus(taskId, "正在接收中...");
        }
    });

    connect(m_fileAll, &FileDialog::taskRejected, this, [this](uint32_t taskId) {
        if (m_taskId2FileTasks.find(taskId) == m_taskId2FileTasks.end()) return;
        FileTask& task = m_taskId2FileTasks[taskId];
        if (task.userId != 9999)
            m_client->sendFileResp(task.userId, taskId, task.fileName, task.totalSize, false);
        m_fileAll->updateStatus(taskId, "已拒绝");
    });

    connect(m_fileAll, &FileDialog::taskCanceled, this, [this](uint32_t taskId) {
        if (m_taskId2FileTasks.find(taskId) == m_taskId2FileTasks.end()) return;
        FileTask& task = m_taskId2FileTasks[taskId];
        if (task.userId != 9999) m_client->sendFileDeny(task.userId, taskId);
        m_fileAll->updateStatus(taskId, "已手动取消");
        if (task.sendTimer) {
            task.sendTimer->stop();
            task.sendTimer->deleteLater();
            task.sendTimer = nullptr;
        }
        if (task.file) {
            task.file->close();
            task.file->deleteLater();
            task.file = nullptr;
        }
    });

    connect(m_client, &ChatClient::groupFileListReceived, this, [this](const QJsonArray& files) {
        for (int i = 0; i < files.size(); ++i) {
            QJsonObject f              = files[i].toObject();
            QString     serverFileName = f["name"].toString(); // 比如："123456_作业.doc"
            int         size           = f["size"].toInt();

            uint32_t taskId   = 0;
            QString  realName = serverFileName;

            // 1. 切割字符串，提取真正的 taskId 和 干净的文件名
            int idx = serverFileName.indexOf('_');
            if (idx != -1) {
                taskId   = serverFileName.left(idx).toUInt();
                realName = serverFileName.mid(idx + 1);
            }
            else {
                taskId = qHash(serverFileName); // 兜底防爆：万一没下划线，用名字算个固定哈希
            }



            // 3. 确认是新任务，才登记并画到界面上
            FileTask task;
            task.taskId      = taskId;
            task.userId      = 9999;
            task.fileName    = realName; // 这里只存真实名字 "作业.doc"
            task.totalSize   = size;
            task.nowLoadSize = 0;
            task.isSend      = false;
            task.file        = nullptr;
            task.sendTimer   = nullptr;

            m_taskId2FileTasks[taskId] = task;

            // 界面上显示的也是干净的文件名
            m_fileAll->addTask(taskId, "[群文件] " + realName, size, false);
        }
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::switchChat(uint32_t peerId, const QString& peerName)
{
    m_chatWithId   = peerId;
    m_chatWithName = peerName;
    ui->chat->setUpdatesEnabled(false);
    ui->chat->clear();
    if (m_client->m_history.contains(peerId)) {
        const auto& history = m_client->m_history[peerId];
        for (const auto& pair : history) { createSingleMessageWidget(pair.second, pair.first); }
    }
    ui->chat->setUpdatesEnabled(true);
    ui->chat->scrollToBottom();
}
void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    for (int i = 0; i < ui->chat->count(); ++i) {
        QListWidgetItem* item   = ui->chat->item(i);
        QWidget*         widget = ui->chat->itemWidget(item);
        if (widget) {
            item->setSizeHint(QSize(ui->chat->viewport()->width(), widget->sizeHint().height()));
        }
    }
}
bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // 按下ctrl+enter键发送消息
    if (watched == ui->textEditInput && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return &&
            keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
            on_buttonSend_clicked();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
void MainWindow::createSingleMessageWidget(const QString& otext, const QString& timeStr)
{
    bool    isMe = false;
    QString senderId, text;
    parseMsgDetails(otext, isMe, senderId, text);

    auto*        item       = new QListWidgetItem(ui->chat);
    QWidget*     container  = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(10, 5, 10, 5);
    mainLayout->setSpacing(2); // 信息栏和气泡紧凑一点

    // --- 1. 信息栏 (用户名 + 时间) ---
    QLabel* infoLabel = new QLabel(QString("%1  %2").arg(senderId).arg(timeStr));
    infoLabel->setStyleSheet("color: #888; font-size: 10px;");

    // --- 2. 气泡层 (水平布局) ---
    QWidget*     bubbleRow    = new QWidget();
    QHBoxLayout* bubbleLayout = new QHBoxLayout(bubbleRow);
    bubbleLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* label = new QLabel(text);
    label->setWordWrap(true);
    label->setMaximumWidth(ui->chat->viewport()->width() * 0.7);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    if (isMe) {
        label->setStyleSheet(
            "background:#409EFF; color:white; border-radius:8px; padding:6px 10px;");
        infoLabel->setAlignment(Qt::AlignRight);
        bubbleLayout->addStretch(1);
        bubbleLayout->addWidget(label);
    }
    else {
        label->setStyleSheet(
            "background:#F2F3F5; color:#111; border-radius:8px; padding:6px 10px;");
        infoLabel->setAlignment(Qt::AlignLeft);
        bubbleLayout->addWidget(label);
        bubbleLayout->addStretch(1);
    }

    // 将信息栏和气泡行加入主布局
    mainLayout->addWidget(infoLabel);
    mainLayout->addWidget(bubbleRow);

    ui->chat->setItemWidget(item, container);
    container->adjustSize();
    item->setSizeHint(QSize(0, container->sizeHint().height()));
}

// void MainWindow::on_buttonFind_clicked()
// {
//     m_client->getList();
// }

void MainWindow::on_buttonSend_clicked()
{
    QString text = ui->textEditInput->toPlainText().trimmed();
    if (text.isEmpty()) return;

    QString message = QString("[me] %1").arg(text.toHtmlEscaped());

    m_client->m_history[m_chatWithId].push_back(
        {QDateTime::currentDateTime().toString("HH:mm:ss"), message});

    if (m_chatWithId != m_client->m_userId) {
        createSingleMessageWidget(message, QDateTime::currentDateTime().toString("HH:mm:ss"));
    }

    ui->textEditInput->clear();

    if (m_chatWithId == 9999) { m_client->sendGroupText(text); }
    else {
        m_client->sendPrivateText(m_chatWithId, text);
    }
}

void MainWindow::on_buttonFind_clicked()
{
    if (m_chatWithId == 0) {
        QMessageBox::information(this, "提示", "请先选择一个聊天对象");
        return;
    }

    if (!m_client->m_history.contains(m_chatWithId) ||
        m_client->m_history.at(m_chatWithId).empty()) {
        QMessageBox::information(this, "提示", "当前没有聊天记录");
        return;
    }

    const auto&  currentHistory = m_client->m_history.at(m_chatWithId);
    SearchDialog dlg(currentHistory, m_chatWithName, this);
    dlg.exec();
}

void MainWindow::on_buttonSendFile_clicked()
{
    if (m_chatWithId == m_client->m_userId) {
        QMessageBox::warning(this, "提示", "请不要发送给自己！");
        return;
    }
    if (m_chatWithId == 0) {
        QMessageBox::warning(this, "提示", "请先在右侧选择一个好友或群聊！");
        return;
    }

    QStringList filePaths =
        QFileDialog::getOpenFileNames(this, "选择要发送的文件", "", "所有文件 (*.*)");
    if (filePaths.isEmpty()) return;

    for (const QString& filePath : filePaths) {
        QFileInfo info(filePath);
        if (!info.exists() || !info.isFile()) continue;
        uint32_t taskId = generateTaskId();
        FileTask task;
        task.taskId      = taskId;
        task.userId      = m_chatWithId;
        task.fileName    = info.fileName();
        task.filePath    = filePath;
        task.totalSize   = info.size();
        task.nowLoadSize = 0;
        task.isSend      = true;
        task.file        = nullptr;
        task.sendTimer   = nullptr;

        m_taskId2FileTasks[taskId] = task;
        // 【关键修复】：获取 Map 里的引用，直接修改 Map 里的值，不要改局部变量！
        FileTask& mapTask = m_taskId2FileTasks[taskId];

        if (m_chatWithId == 9999) {
            m_fileAll->addTask(taskId, "[群上传] " + mapTask.fileName, mapTask.totalSize, true);
            m_fileAll->updateStatus(taskId, "正在上传...");

            mapTask.file = new QFile(filePath);
            mapTask.file->open(QIODevice::ReadOnly);
            mapTask.sendTimer = new QTimer(this);

            connect(mapTask.sendTimer, &QTimer::timeout, this, [this, taskId]() {
                auto it = m_taskId2FileTasks.find(taskId);
                if (it == m_taskId2FileTasks.end()) return;
                FileTask& t = it->second;
                if (!t.file || !t.file->isOpen()) return; // 再次保命判断

                QByteArray chunk = t.file->read(64 * 1024);
                if (chunk.isEmpty()) {
                    t.sendTimer->stop();
                    t.file->close();
                    m_fileAll->updateStatus(taskId, "上传完成");
                    QString msg =
                        QString("[群文件] 我上传了: %1，大家可以去拉取下载了！").arg(t.fileName);
                    m_client->sendGroupText(msg);
                    return;
                }

                QByteArray nameBuf = t.fileName.toUtf8();
                nameBuf.resize(64); // 必须定长64字节
                QByteArray payload;
                payload.append(nameBuf);
                payload.append(chunk);

                m_client->sendFileData(9999, taskId, payload);

                t.nowLoadSize += chunk.size();
                m_fileAll->updateProgress(taskId, t.nowLoadSize, t.totalSize);
            });
            mapTask.sendTimer->start(10);
            continue;
        }

        // 单聊逻辑
        m_fileAll->addTask(taskId, mapTask.fileName, mapTask.totalSize, true);
        m_client->sendFileReq(m_chatWithId, taskId, mapTask.fileName, mapTask.totalSize);
    }

    m_fileAll->show();
    m_fileAll->raise();
}


void MainWindow::fileReqReceived(uint32_t fromId, uint32_t taskId, const QString& fileName,
                                 int fileSize)
{
    FileTask task;
    task.taskId                = taskId;
    task.userId                = fromId;
    task.fileName              = fileName;
    task.totalSize             = fileSize;
    task.nowLoadSize           = 0;
    task.isSend                = false;
    task.file                  = nullptr;
    task.sendTimer             = nullptr;
    m_taskId2FileTasks[taskId] = task;
    m_fileAll->addTask(taskId, fileName, fileSize, false);
    m_fileAll->show();
}

void MainWindow::fileRespReceived(uint32_t fromId, uint32_t taskId, const QString& fileName,
                                  bool accept)
{
    auto it = m_taskId2FileTasks.find(taskId);
    if (it == m_taskId2FileTasks.end()) return;
    FileTask& task = it->second;

    if (!accept) {
        m_fileAll->updateStatus(taskId, "对方已拒绝");
        return;
    }

    m_fileAll->updateStatus(taskId, "正在发送中...");
    task.file = new QFile(task.filePath);
    if (!task.file->open(QIODevice::ReadOnly)) {
        m_fileAll->updateStatus(taskId, "读取本地文件失败！");
        return;
    }

    task.sendTimer = new QTimer(this);
    connect(task.sendTimer, &QTimer::timeout, this, [this, taskId]() {
        auto it = m_taskId2FileTasks.find(taskId);
        if (it == m_taskId2FileTasks.end()) return;
        FileTask& t = it->second;
        if (!t.file || !t.file->isOpen()) {
            t.sendTimer->stop();
            return;
        }
        QByteArray chunk = t.file->read(64 * 1024);
        if (chunk.isEmpty()) return;
        m_client->sendFileData(t.userId, taskId, chunk);
        t.nowLoadSize += chunk.size();
        m_fileAll->updateProgress(taskId, t.nowLoadSize, t.totalSize);
        if (t.nowLoadSize >= t.totalSize) {
            t.sendTimer->stop();
            t.file->close();
            t.file->deleteLater();
            t.file = nullptr;
            m_fileAll->updateStatus(taskId, "发送完成！");
            createSingleMessageWidget(QString("[me] 文件发送完成: %1").arg(t.fileName),
                                      QDateTime::currentDateTime().toString("HH:mm:ss"));
        }
    });

    task.sendTimer->start(1);
}

void MainWindow::fileDenyReceived(uint32_t fromId, uint32_t taskId)
{
    m_fileAll->updateStatus(taskId, "对方已取消");
    auto it = m_taskId2FileTasks.find(taskId);
    if (it != m_taskId2FileTasks.end()) {
        if (it->second.file) {
            it->second.file->close();
            it->second.file->deleteLater();
            it->second.file = nullptr;
        }
        if (it->second.sendTimer) {
            it->second.sendTimer->stop();
            it->second.sendTimer->deleteLater();
            it->second.sendTimer = nullptr;
        }
    }
}

void MainWindow::fileDataReceived(uint32_t fromId, uint32_t taskId, const QByteArray& data)
{
    auto it = m_taskId2FileTasks.find(taskId);
    if (it == m_taskId2FileTasks.end()) return;
    FileTask& task = it->second;

    if (!task.file || !task.file->isOpen()) return;

    // === 新增：如果是下载群文件，注意要把 64 字节头部剔除，并且循环索要下一块！ ===
    if (fromId == 9999 || task.userId == 9999) {
        QByteArray pureData = data.mid(64); // 剔除头部名字
        task.file->write(pureData);
        task.nowLoadSize += pureData.size();
        m_fileAll->updateProgress(taskId, task.nowLoadSize, task.totalSize);

        if (task.nowLoadSize >= task.totalSize) {
            task.file->close();
            task.file->deleteLater();
            task.file = nullptr;
            m_fileAll->updateStatus(taskId, "下载完成！");
            createSingleMessageWidget(QString("[群系统] 群文件接收完毕: %1").arg(task.fileName),
                                      QDateTime::currentDateTime().toString("HH:mm:ss"));
        }
        else {
            // 还没下完，立刻向服务器索要下一块！
            QJsonObject obj;
            obj["cmd"]      = "pull_group_file";
            obj["fileName"] = task.fileName;
            obj["offset"]   = (qint64)task.nowLoadSize; // 接着目前的进度要
            obj["taskId"] =
                (qint64)taskId; // 【关键修复 4】：循环要下一块的时候，依然要带上任务 ID！
            m_client->sendFrame(TYPE::CONTROL, QJsonDocument(obj).toJson(QJsonDocument::Compact));
        }
        return; // 处理完群聊直接退出，不走下面的私聊逻辑
    }

    // === 原来的私聊写入逻辑 ===
    task.file->write(data);
    task.nowLoadSize += data.size();
    m_fileAll->updateProgress(taskId, task.nowLoadSize, task.totalSize);

    if (task.nowLoadSize >= task.totalSize) {
        task.file->close();
        task.file->deleteLater();
        task.file = nullptr;
        m_fileAll->updateStatus(taskId, "接收完成！");
        createSingleMessageWidget(
            QString("[%1] 发来的文件接收完毕: %2").arg(fromId).arg(task.fileName),
            QDateTime::currentDateTime().toString("HH:mm:ss"));
    }
}
void MainWindow::on_pushButton_clicked()
{
    this->m_fileAll->show();
}

void MainWindow::on_btnGroupFiles_clicked()
{
    if (m_chatWithId != 9999) {
        QMessageBox::information(this, "提示", "请先切换到群聊大厅！");
        return;
    }
    QJsonObject obj;
    obj["cmd"] = "get_group_files";
    m_client->sendFrame(TYPE::FILE_CTRL, QJsonDocument(obj).toJson(QJsonDocument::Compact));
    m_fileAll->show();
}
