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
#include "qicon.h"
#include "qmenu.h"
#include "ui_mainwindow.h"
#include "mainwindow.h"
#include "searchdialog.h"
static void parseMsgPrefix(const QString& raw, bool& isMe, QString& plain)
{
    QString s = raw.trimmed();
    isMe      = false;
    plain     = s;
    if (s.startsWith("[me]")) {
        isMe  = true;
        plain = s.mid(4).trimmed();
        return;
    }
    // 匹配 [12345] 前缀
    if (s.startsWith('[')) {
        int r = s.indexOf(']');
        if (r > 1) {
            bool ok = false;
            s.mid(1, r - 1).toUInt(&ok);
            if (ok) {
                isMe  = false;
                plain = s.mid(r + 1).trimmed();
                return;
            }
        }
    }
    // 没前缀当成对方消息
    isMe  = false;
    plain = s;
}
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
    ui->splitter->setSizes(QList<int>{1, 2});
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
    // 更新在线列表逻辑
    connect(m_client, &ChatClient::userListReceived, this,
            [this](const QVector<ChatClient::UserInfo>& users) {
                ui->listWidget->clear();
                QListWidgetItem* groupItem = new QListWidgetItem("[🌐]群聊大厅");
                groupItem->setData(Qt::UserRole, 9999);
                groupItem->setData(Qt::UserRole + 1, "group");
                ui->listWidget->insertItem(0, groupItem);
                for (const auto& u : users) {
                    auto* item = new QListWidgetItem(QString("[%1] %2").arg(u.id).arg(u.name));
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
        m_client->m_history[9999].push_back(message);
        if (m_chatWithId == 9999) { createSingleMessageWidget(message); }
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
                    m_client->m_history[fromId].push_back(message);
                }

                if (m_chatWithId == fromId) { createSingleMessageWidget(message); }
            });
    connect(ui->chat->model(), &QAbstractItemModel::rowsInserted, ui->chat,
            &QListWidget::scrollToBottom);
    ui->textEditInput->installEventFilter(this);
    // 菜单：退出程序
    QAction* exitAction = new QAction("退出程序", this);
    ui->menuExit->addAction(exitAction);
    connect(exitAction, &QAction::triggered, this, []() { QApplication::quit(); });
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
        for (const QString& msg : history) { createSingleMessageWidget(msg); }
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
void MainWindow::createSingleMessageWidget(const QString& otext)
{
    bool    isMe = false;
    QString senderId, text;
    parseMsgDetails(otext, isMe, senderId, text);

    auto*        item       = new QListWidgetItem(ui->chat);
    QWidget*     container  = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(container); // 改用垂直布局
    mainLayout->setContentsMargins(10, 5, 10, 5);
    mainLayout->setSpacing(2); // 信息栏和气泡紧凑一点

    // --- 1. 信息栏 (用户名 + 时间) ---
    QString timeStr   = QDateTime::currentDateTime().toString("HH:mm:ss");
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

    m_client->m_history[m_chatWithId].push_back(message);

    if (m_chatWithId != m_client->m_userId) { createSingleMessageWidget(message); }

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
