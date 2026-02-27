#include "dialog.h"
#include "qsettings.h"
#include "ui_dialog.h"
#include <QMessageBox>
Dialog::Dialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::Dialog), m_client(new ChatClient(this))
{
    ui->setupUi(this);
    connect(m_client, &ChatClient::connected, this,
            [this]() { m_client->login(ui->lineId->text().toUInt(), ui->lineName->text()); });
    connect(m_client, &ChatClient::loginAck, this, [this](bool ok, const QString& reason) {
        if (loginin) loginin->hide();
        if (!ok) {
            QMessageBox::warning(this, "登录失败", reason);
            return;
        }
        accept();
    });
    connect(m_client, &ChatClient::errorOccurred, this, [this](int, const QString& err) {
        QMessageBox::warning(this, "网络错误", err);
        if (loginin) loginin->hide();
    });

    // connect(ui->buttonLogin, &QPushButton::clicked, this,
    //         [this]() { m_client->connectTo(ui->lineIp->text(), ui->linePort->text().toUShort());
    //         });
    QSettings s("chat", "chat");
    bool      remember = s.value("login/remember", false).toBool();
    ui->checkBox->setChecked(remember);
    if (remember) {
        ui->lineIp->setText(s.value("login/host", "").toString());
        ui->linePort->setText(s.value("login/port", "").toString());
        ui->lineId->setText(s.value("login/id", "").toString());
        ui->lineName->setText(s.value("login/name", "").toString());
    }
    ui->buttonLogin->setDefault(true);
}

Dialog::~Dialog()
{
    delete ui;
}

QString Dialog::host() const
{
    return ui->lineIp->text().trimmed();
}
quint16 Dialog::port() const
{
    return (quint16)ui->linePort->text().toUShort();
}
uint32_t Dialog::userId() const
{
    return ui->lineId->text().toUInt();
}
QString Dialog::userName() const
{
    return ui->lineName->text().trimmed();
}

void Dialog::on_buttonLogin_clicked()
{
    const QString  h    = host();
    const quint16  p    = port();
    const uint32_t id   = userId();
    const QString  name = userName();
    if (h.isEmpty()) {
        QMessageBox::warning(this, "参数错误", "host 不能为空");
        return;
    }
    if (p == 0) {
        QMessageBox::warning(this, "参数错误", "port 不能为空");
        return;
    }
    if (id < 10000 || id > 99999) {
        QMessageBox::warning(this, "参数错误", "id 必须在 10000 到 99999 之间");
        return;
    }
    if (name.isEmpty()) {
        QMessageBox::warning(this, "参数错误", "name 不能为空");
        return;
    }
    if (loginin == nullptr) {
        loginin = new QProgressDialog("正在登录...", "取消", 0, 0, this);
        loginin->setWindowModality(Qt::WindowModal);
        loginin->setCancelButton(nullptr);
    }
    loginin->show();
    QSettings s("chat", "chat");
    bool      remember = ui->checkBox->isChecked();
    s.setValue("login/remember", remember);
    if (remember) {
        s.setValue("login/host", ui->lineIp->text().trimmed());
        s.setValue("login/port", ui->linePort->text().trimmed());
        s.setValue("login/id", ui->lineId->text().trimmed());
        s.setValue("login/name", ui->lineName->text().trimmed());
    }
    else {
        s.remove("login/host");
        s.remove("login/port");
        s.remove("login/id");
        s.remove("login/name");
    }
    m_client->connectTo(host(), port());
    m_client->m_userId = userId();
    m_client->m_name   = userName();
}

void Dialog::on_buttonExit_clicked()
{
    reject();
}